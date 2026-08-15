; =============================================================================
; BleskernOS - Stage 2 Bootloader
; Vive en 0x7E00, cargado por Stage 1
; Tareas:
;   1. Detectar memoria RAM (INT 15h E820)
;   2. Habilitar línea A20
;   3. Cargar GDT
;   4. Entrar a Modo Protegido (32-bit)
;   5. Saltar al kernel (en 0x8E00, justo despues de Stage 2)
; =============================================================================

[BITS 16]
[ORG 0x7E00]

; -----------------------------------------------------------------------------
; Constantes
; -----------------------------------------------------------------------------
KERNEL_LOAD_ADDR equ 0x00100000 ; destino final del kernel (1 MiB)
KERNEL_BOUNCE_ADDR equ 0x00010000 ; buffer BIOS temporal de 16 KiB
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 1024     ; Sectores reservados para el kernel (512KB)
%endif
KERNEL_START_LBA equ 9          ; LBA donde empieza el kernel en disco (Stage2 ahora ocupa 8 sectores)
FLOPPY_TRACK_SPAN equ 36
FLOPPY_SECTORS_PER_TRACK equ 18
BOOTINFO_ADDR    equ 0x0700     ; Datos de video que stage2 pasa al kernel
BOOT_MODE_ADDR   equ 0x06F0     ; Magic escrito solo por Stage 1 del CD
INSTALLER_BOOT_MAGIC equ 0x54534E49 ; bytes "INST"
BOOT_PART_LBA_SAVE equ 0x0504   ; BPB_HiddSec guardado por stage1 FAT32
VBE_MODEINFO     equ 0x0800     ; Buffer temporal VBE mode info
VBE_INFO         equ 0x0900     ; Buffer temporal VBE controller info (512 bytes)
VBE_MAX_PRINTED  equ 18         ; Evita scrollear toda la pantalla si la BIOS lista demasiado
VBE_DEFAULT_MODE equ 0x103      ; 800x600x8, modo VESA estandar

; Bootinfo extendido para pedir modos VGA desde el kernel.
; Magic 'VGA1' en little endian: bytes 56 47 41 31.
VGA_BOOTINFO_MAGIC equ 0x31414756
VGA_BOOTINFO_TEXT  equ 0
VGA_BOOTINFO_13H   equ 1
VGA_BOOTINFO_12H   equ 2

SELECTED_VGA_TEXT  equ 0xFFFF
SELECTED_VGA_13H   equ 0xFF13
SELECTED_VGA_12H   equ 0xFF12

; Dirección donde guardamos el mapa de memoria para pasarle al kernel
MEM_MAP_ADDR     equ 0x0500
MEM_MAP_MAX      equ 20         ; Máximo de entradas E820

; =============================================================================
; INICIO DE STAGE 2
; =============================================================================
stage2_start:
    ; Asegurarnos de segmentos correctos
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl
    mov eax, [BOOT_PART_LBA_SAVE]
    mov [boot_part_lba], eax

    mov si, msg_stage2_hello
    call print_string

    ; ----- PASO 1: Detectar RAM -----
    mov si, msg_detecting_ram
    call print_string
    call detect_memory
    mov si, msg_ram_done
    call print_string

    ; ----- PASO 2: Habilitar A20 -----
    mov si, msg_enabling_a20
    call print_string
    call enable_a20
    ; enable_a20 retorna CF=0 si tuvo éxito, CF=1 si falló
    jnc .a20_ok
    mov si, msg_a20_fail
    call print_string
    jmp halt16

.a20_ok:
    mov si, msg_a20_ok
    call print_string

    ; ----- PASO 3: Cargar Kernel -----
    mov si, msg_loading_kernel
    call print_string
    call load_kernel
    jnc .kernel_ok
    mov si, msg_kernel_fail
    call print_string
    jmp halt16

.kernel_ok:
    ; Verificar que al menos el encabezado ejecutable realmente llego a 1 MiB.
    ; Algunas BIOS OEM antiguas devuelven AH=00/CF=0 para INT 15h/87h aunque
    ; el bloque no haya sido movido correctamente; saltarlo acababa en triple
    ; fault sin mensaje alguno. FFFF:0012 corresponde a 0x00100002 con A20.
    push ds
    mov ax, 0xFFFF
    mov ds, ax
    cmp dword [0x0012], 0x314B4C42 ; "BLK1"
    pop ds
    jne .kernel_bad_copy

    mov si, msg_kernel_ok
    call print_string

    ; ----- PASO 4: Entrar a Modo Protegido -----
    mov si, msg_entering_pm
    call print_string

    call setup_vesa

    ; Último momento en Real Mode: cargar GDTR
    cli                         ; Deshabilitar interrupciones definitivamente
    lgdt [gdt_descriptor]       ; Cargar la GDT

    ; Activar bit PE (Protection Enable) en CR0
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump para vaciar pipeline y entrar a modo protegido
    ; Selector 0x08 = code descriptor en nuestra GDT
    jmp 0x08:protected_mode_entry

.kernel_bad_copy:
    mov si, msg_kernel_verify_fail
    call print_string
    jmp halt16

halt16:
    hlt
    jmp halt16

; =============================================================================
; DETECCIÓN DE MEMORIA RAM (INT 15h, EAX=E820h)
; Guarda el mapa en MEM_MAP_ADDR
; Formato de cada entrada (20 bytes):
;   - 8 bytes: dirección base
;   - 8 bytes: longitud
;   - 4 bytes: tipo (1=usable, 2=reserved, ...)
; =============================================================================
detect_memory:
    pusha
    mov di, MEM_MAP_ADDR + 4    ; Reservamos 4 bytes al inicio para el conteo
    xor ebx, ebx                ; EBX=0 para primera llamada
    xor bp, bp                  ; BP = contador de entradas

.e820_loop:
    mov eax, 0xE820
    mov ecx, 20                 ; Tamaño de cada entrada
    mov edx, 0x534D4150         ; Firma 'SMAP'
    int 0x15
    jc .e820_done               ; CF=1 significa fin de lista o no soportado
    cmp eax, 0x534D4150         ; Verificar que BIOS devuelve 'SMAP'
    jne .e820_done
    test ecx, ecx               ; ¿Entrada de 0 bytes? ignorar
    jz .next_entry
    inc bp                      ; Contar entrada válida
    add di, 20                  ; Avanzar al siguiente slot

.next_entry:
    test ebx, ebx               ; EBX=0 significa que fue la última entrada
    jz .e820_done
    cmp bp, MEM_MAP_MAX         ; ¿Llenamos el buffer?
    jl .e820_loop

.e820_done:
    ; Guardar conteo de entradas en los primeros 4 bytes
    mov [MEM_MAP_ADDR], bp
    popa
    ret

; =============================================================================
; TEST_A20
; Compara 0x0000:0x0500 con su alias 0xFFFF:0x0510 (= dirección física 0x100500)
; Si A20 está OFF, ambas apuntan al mismo lugar físico → wrap-around → iguales
; Retorna: AX=1 si A20 habilitado, AX=0 si deshabilitado
; Preserva todos los registros de segmento (push/pop)
; =============================================================================
test_a20:
    pushf
    push ds
    push es
    push di
    push si
    cli

    xor ax, ax
    mov ds, ax
    mov si, 0x0500              ; DS:SI = 0x0000:0x0500 → físico 0x000500

    not ax                      ; AX = 0xFFFF
    mov es, ax
    mov di, 0x0510              ; ES:DI = 0xFFFF:0x0510 → físico 0x100500

    ; Guardar valores originales antes de pisar
    mov al, [ds:si]
    push ax
    mov al, [es:di]
    push ax

    ; Escribir valores distintos en ambas direcciones
    mov byte [ds:si], 0x00
    mov byte [es:di], 0xFF

    ; Leer de vuelta la dirección baja
    ; Si A20 está OFF, el write a ES:DI pisó DS:SI también
    mov al, [ds:si]
    cmp al, 0xFF                ; ¿Igual a lo que escribimos en ES:DI?
    je .disabled                ; Sí → wrap-around → A20 OFF

    ; A20 está habilitado
    pop ax
    mov byte [es:di], al        ; Restaurar ES:DI
    pop ax
    mov byte [ds:si], al        ; Restaurar DS:SI
    mov ax, 1
    jmp .exit

.disabled:
    pop ax
    mov byte [es:di], al
    pop ax
    mov byte [ds:si], al
    mov ax, 0

.exit:
    pop si
    pop di
    pop es
    pop ds
    popf
    ret

; =============================================================================
; ENABLE_A20
; Orden según la documentación (osdev.org): de menor a mayor riesgo
;   1. Test primero — si ya está habilitado no hacer nada
;   2. BIOS INT 15h AX=2401h
;   3. Keyboard Controller (8042)  ← método clásico y confiable
;   4. Fast A20 (puerto 0x92)      ← último recurso, puede causar problemas
; Retorna: CF=0 si A20 quedó habilitado, CF=1 si falló todo
; =============================================================================
enable_a20:
    pusha

    ; --- Paso 0: ¿Ya está habilitado? (BIOS o emulador lo activaron) ---
    call test_a20
    cmp ax, 1
    je .success

    ; --- Paso 1: BIOS INT 15h AX=2401h ---
    mov ax, 0x2401
    int 0x15
    ; Ignoramos el estado devuelto por BIOS (puede mentir), testeamos directo
    call test_a20
    cmp ax, 1
    je .success

    ; --- Paso 2: Keyboard Controller (8042) ---
    call .enable_via_kbd
    ; Pequeño loop de espera: el KBC puede tardar varios ciclos
    mov cx, 0xFFFF
.kbd_wait_loop:
    call test_a20
    cmp ax, 1
    je .success
    loop .kbd_wait_loop

    ; --- Paso 3: Fast A20 (puerto 0x92) --- último recurso
    in al, 0x92
    test al, 2
    jnz .fast_done              ; Ya estaba seteado el bit (pero A20 sigue off, raro)
    or al, 2
    and al, 0xFE                ; ¡No tocar bit 0! Causa reset del sistema
    out 0x92, al
.fast_done:
    ; Espera y verifica
    mov cx, 0xFFFF
.fast_wait_loop:
    call test_a20
    cmp ax, 1
    je .success
    loop .fast_wait_loop

    ; --- Todos los métodos fallaron ---
    popa
    stc                         ; CF=1 → error
    ret

.success:
    popa
    clc                         ; CF=0 → éxito
    ret

; ---- Subrutina interna: habilitar A20 vía Keyboard Controller (8042) ----
; Protocolo: deshabilitar KBC → leer output port → setear bit 1 → rehabilitar KBC
.enable_via_kbd:
    ; Esperar que el buffer de entrada del KBC esté libre
    call .kbc_wait_input
    mov al, 0xAD                ; Comando: deshabilitar teclado
    out 0x64, al

    call .kbc_wait_input
    mov al, 0xD0                ; Comando: leer output port del KBC
    out 0x64, al

    call .kbc_wait_output       ; Esperar que el dato esté listo
    in al, 0x60                 ; Leer el output port
    push ax                     ; Guardarlo

    call .kbc_wait_input
    mov al, 0xD1                ; Comando: escribir output port del KBC
    out 0x64, al

    call .kbc_wait_input
    pop ax
    or al, 2                    ; Setear bit 1 = A20 enable
    out 0x60, al                ; Escribir nuevo valor

    call .kbc_wait_input
    mov al, 0xAE                ; Comando: rehabilitar teclado
    out 0x64, al

    call .kbc_wait_input
    ret

; Esperar a que el buffer de INPUT del KBC esté libre (bit 1 de 0x64 = 0)
.kbc_wait_input:
    in al, 0x64
    test al, 2
    jnz .kbc_wait_input
    ret

; Esperar a que el buffer de OUTPUT del KBC tenga datos (bit 0 de 0x64 = 1)
.kbc_wait_output:
    in al, 0x64
    test al, 1
    jz .kbc_wait_output
    ret

; =============================================================================
; CARGAR KERNEL DESDE DISCO
; Usa LBA en discos duros y CHS en floppy.
; =============================================================================
load_kernel:
    pusha
    mov dl, [boot_drive]
    mov word [kernel_remaining], KERNEL_SECTORS
    mov dword [kernel_destination], KERNEL_LOAD_ADDR
    cmp dl, 0x80
    jb .load_chs

    ; INT 13h no puede direccionar de forma portable todo el espacio superior
    ; a 1 MiB. Leemos hasta 32 sectores en un bounce buffer bajo y luego los
    ; copiamos al destino final usando un descriptor de datos plano temporal.
    mov eax, [boot_part_lba]
    add eax, KERNEL_START_LBA
    mov dword [kern_dap_lba_lo], eax
    mov dword [kern_dap_lba_hi], 0

.next_chunk:
    mov ax, [kernel_remaining]
    test ax, ax
    jz .done
    cmp ax, 32
    jbe .chunk_size_ok
    mov ax, 32

.chunk_size_ok:
    mov word [kernel_chunk_sectors], ax
    mov byte [kern_dap_size], 0x10
    ; El campo de sectores del DAP es entrada/salida y algunas BIOS lo ponen
    ; en cero incluso tras una lectura valida. La copia usa por separado
    ; kernel_chunk_sectors y nunca confia en el valor devuelto por firmware.
    mov ax, [kernel_chunk_sectors]
    mov word [kern_dap_sectors], ax
    mov word [kern_dap_offset], KERNEL_BOUNCE_ADDR & 0x000F
    mov word [kern_dap_segment], KERNEL_BOUNCE_ADDR >> 4
    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, kern_dap
    int 0x13
    pushf
    xor ax, ax
    mov ds, ax
    mov es, ax
    popf
    jc .error

    call copy_kernel_chunk_high
    jc .error

.chunk_loaded:
    mov ax, [kernel_chunk_sectors]
    sub word [kernel_remaining], ax
    movzx edx, ax
    add dword [kern_dap_lba_lo], edx
    shl edx, 9
    add dword [kernel_destination], edx
    jmp .next_chunk

.done:
    popa
    clc
    ret

.load_chs:
    mov si, KERNEL_START_LBA

.chs_next_sector:
    cmp word [kernel_remaining], 0
    je .done

    mov ax, si
    xor dx, dx
    mov bx, FLOPPY_TRACK_SPAN
    div bx
    mov ch, al

    mov ax, dx
    xor dx, dx
    mov bx, FLOPPY_SECTORS_PER_TRACK
    div bx
    mov dh, al
    mov cl, dl
    inc cl

    mov ax, KERNEL_BOUNCE_ADDR >> 4
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, 0x01
    mov dl, [boot_drive]
    int 0x13
    pushf
    xor ax, ax
    mov ds, ax
    mov es, ax
    popf
    jc .error

    mov word [kern_dap_sectors], 1
    mov word [kernel_chunk_sectors], 1
    call copy_kernel_chunk_high
    jc .error
    add dword [kernel_destination], 512
    inc si
    dec word [kernel_remaining]
    jmp .chs_next_sector

.error:
    popa
    stc
    ret

; Copia el bounce buffer a memoria extendida sin depender de INT 15h/AH=87h.
; Algunas BIOS OEM (incluida la TravelMate probada) devuelven CF=0/AH=0 pero
; no mueven los datos. Entramos brevemente en protected mode de 16 bits,
; cargamos DS/ES con el descriptor plano, hacemos REP MOVSD y volvemos.
; Entrada: [kernel_chunk_sectors] y [kernel_destination].
; Salida: CF=0. A20 ya fue comprobado antes de cargar el kernel.
copy_kernel_chunk_high:
    pushf
    cli
    push ds
    push es
    pusha

    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x18:.protected16

.protected16:
    ; Copiar con segmentos planos mientras PE=1. El contador viene de una
    ; variable propia: el BIOS puede modificar el campo equivalente del DAP.
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov esi, KERNEL_BOUNCE_ADDR
    mov edi, [kernel_destination]
    movzx ecx, word [kernel_chunk_sectors]
    shl ecx, 7                    ; sectores * 512 / 4
    a32 rep movsd

    ; No usar la pila entre PE=1 y este far jump: SS conserva el cache de
    ; real mode. La copia tampoco necesita la pila.
    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
    jmp 0x0000:.real16

.real16:
    xor ax, ax
    mov ds, ax
    mov es, ax

    popa
    pop es
    pop ds
    popf
    clc
    ret

; =============================================================================
; LISTAR Y ELEGIR MODO DE VIDEO ANTES DE ACTIVAR GRAFICOS
;
; Nota importante:
; - Los modos VGA clasicos no se descubren con VBE. Se listan como fallback
;   fijo porque vienen del estandar VGA/BIOS: texto 03h, 13h y 12h.
; - Para VESA no dependemos de la lista global 4F00h, porque algunas BIOS la
;   publican raro. En su lugar probamos exactamente la misma tabla que despues
;   usa setup_vesa. Asi, lo que se muestra coincide con lo que BlesKernOS puede
;   intentar usar realmente.
; =============================================================================
select_video_mode:
    pusha
    push es

    mov word [selected_vbe_mode], 0
    mov byte [vbe_menu_count], 0

    mov si, msg_vga_header
    call print_string
    mov si, msg_vga_text_choice
    call print_string
    mov si, msg_vga_13h_note
    call print_string
    mov si, msg_vga_12h_note
    call print_string

    mov si, msg_vesa_header
    call print_string
    xor bp, bp                           ; contador de modos VESA validos
    mov byte [vbe_menu_pass_bpp], 32     ; mostrar true-color primero

.vesa_pass:
    mov si, vbe_mode_candidates

.vesa_loop:
    lodsw
    test ax, ax
    jz .vesa_pass_done
    mov [vbe_current_mode], ax

    ; Consultar el modo candidato. Guardamos SI/DS porque algunas BIOS tocan registros.
    push si
    push ds
    push es
    xor ax, ax
    mov es, ax
    mov di, VBE_MODEINFO
    mov ax, 0x4F01
    mov cx, [vbe_current_mode]
    int 0x10
    pop es
    pop ds
    pop si
    cmp ax, 0x004F
    jne .vesa_loop

    ; Filtrar: modo soportado + Linear Framebuffer disponible.
    test word [VBE_MODEINFO], 0x0001
    jz .vesa_loop
    test word [VBE_MODEINFO], 0x0080
    jz .vesa_loop

    ; Hacer varias pasadas: 32, 24, 16 y 8 bpp. De esta forma los
    ; modos true-color no quedan escondidos detras de los nueve modos indexados
    ; que suelen aparecer primero en BIOS VBE antiguas.
    mov al, [VBE_MODEINFO + 25]
    cmp al, [vbe_menu_pass_bpp]
    jne .vesa_loop

.store_and_print:
    ; Guardar modo en tabla de opciones: opcion 1 => indice 0.
    cmp bp, 9
    jae .vesa_loop                       ; menu simple: maximo 9 opciones numericas
    mov bx, bp
    shl bx, 1
    mov ax, [vbe_current_mode]
    mov [vbe_menu_modes + bx], ax

    ; IMPORTANTE: SI apunta al proximo candidato de vbe_mode_candidates.
    ; print_string tambien usa SI, asi que hay que guardarlo durante todo el print.
    push si
    mov si, msg_choice_prefix
    call print_string
    mov ax, bp
    inc ax
    call print_dec16
    mov si, msg_choice_mid
    call print_string
    mov ax, [vbe_current_mode]
    call print_hex16
    mov si, msg_mode_sep
    call print_string
    mov ax, [VBE_MODEINFO + 18]          ; XResolution
    call print_dec16
    mov al, 'x'
    call print_char
    mov ax, [VBE_MODEINFO + 20]          ; YResolution
    call print_dec16
    mov al, 'x'
    call print_char
    xor ah, ah
    mov al, [VBE_MODEINFO + 25]          ; BitsPerPixel
    call print_dec16
    mov si, msg_bpp_suffix
    call print_string
    pop si

    inc bp
    mov ax, bp
    mov [vbe_menu_count], al
    jmp .vesa_loop

.vesa_pass_done:
    cmp bp, 9
    jae .vesa_done
    cmp byte [vbe_menu_pass_bpp], 32
    je .menu_pass_24
    cmp byte [vbe_menu_pass_bpp], 24
    je .menu_pass_16
    cmp byte [vbe_menu_pass_bpp], 16
    je .menu_pass_8
    jmp .vesa_done

.menu_pass_24:
    mov byte [vbe_menu_pass_bpp], 24
    jmp .vesa_pass
.menu_pass_16:
    mov byte [vbe_menu_pass_bpp], 16
    jmp .vesa_pass
.menu_pass_8:
    mov byte [vbe_menu_pass_bpp], 8
    jmp .vesa_pass

.vesa_done:
    test bp, bp
    jnz .ask_choice
    mov si, msg_no_vesa_modes
    call print_string

.ask_choice:
    mov si, msg_choose_mode
    call print_string
    xor ax, ax
    int 0x16                              ; AL = tecla ASCII

    cmp al, '0'
    je .auto_mode
    cmp al, 't'
    je .text_mode
    cmp al, 'T'
    je .text_mode
    cmp al, 'g'
    je .vga_13h_mode
    cmp al, 'G'
    je .vga_13h_mode
    cmp al, 'h'
    je .vga_12h_mode
    cmp al, 'H'
    je .vga_12h_mode

    ; Elegir VESA por numero 1..9.
    cmp al, '1'
    jb .invalid_choice
    cmp al, '9'
    ja .invalid_choice
    sub al, '1'                           ; AL = indice 0..8

    mov bl, [vbe_menu_count]
    cmp al, bl
    jae .invalid_choice

    xor bx, bx
    mov bl, al
    shl bx, 1
    mov ax, [vbe_menu_modes + bx]
    mov [selected_vbe_mode], ax

    mov si, msg_selected_vesa
    call print_string
    mov ax, [selected_vbe_mode]
    call print_hex16
    mov si, msg_newline
    call print_string
    jmp .done

.auto_mode:
    mov word [selected_vbe_mode], 0
    mov si, msg_selected_auto
    call print_string
    jmp .done

.text_mode:
    ; Seguro: deja BIOS en modo texto 03h y pide texto al kernel.
    mov ax, 0x0003
    int 0x10
    mov word [selected_vbe_mode], SELECTED_VGA_TEXT
    mov si, msg_selected_text
    call print_string
    jmp .done

.vga_13h_mode:
    ; No usamos BIOS aca: el kernel aplicara los registros VGA desde gfx_init().
    mov word [selected_vbe_mode], SELECTED_VGA_13H
    mov si, msg_selected_13h
    call print_string
    jmp .done

.vga_12h_mode:
    ; No usamos BIOS aca: el kernel aplicara los registros VGA desde gfx_init().
    mov word [selected_vbe_mode], SELECTED_VGA_12H
    mov si, msg_selected_12h
    call print_string
    jmp .done

.invalid_choice:
    mov si, msg_invalid_choice
    call print_string
    jmp .ask_choice

.done:
    pop es
    popa
    ret

; =============================================================================
; CONFIGURAR VESA LFB ANTES DE MODO PROTEGIDO
; Bootinfo en 0x0700:
;   +0 dword magic 'GUI1'
;   +4 dword framebuffer fisico
;   +8 word  width
;   +10 word height
;   +12 word pitch
;   +14 byte bpp
;   +15 word vbe mode
; =============================================================================
setup_vesa:
    pusha
    push es

    ; El CD instalador no muestra el selector VESA ni activa framebuffer.
    ; Fuerza modo 03h para que Setup sea una interfaz de texto estable.
    cmp dword [BOOT_MODE_ADDR], INSTALLER_BOOT_MAGIC
    jne .normal_video_boot
    mov ax, 0x0003
    int 0x10
    mov dword [BOOTINFO_ADDR], VGA_BOOTINFO_MAGIC
    mov word [BOOTINFO_ADDR + 4], VGA_BOOTINFO_TEXT
    jmp .done

.normal_video_boot:
    mov dword [BOOTINFO_ADDR], 0
    mov dword [BOOTINFO_ADDR + 4], 0

    ; Si el usuario eligio un modo VGA clasico, no activar VESA.
    ; Dejamos una marca para que kernel/gfx_init() cambie al modo pedido.
    cmp word [selected_vbe_mode], SELECTED_VGA_TEXT
    je .bootinfo_text
    cmp word [selected_vbe_mode], SELECTED_VGA_13H
    je .bootinfo_13h
    cmp word [selected_vbe_mode], SELECTED_VGA_12H
    je .bootinfo_12h

    xor ax, ax
    mov es, ax

    ; Si el usuario eligio un modo VESA concreto, probar ese primero.
    mov ax, [selected_vbe_mode]
    test ax, ax
    jz .auto_list
    mov [BOOTINFO_ADDR + 15], ax
    jmp .query_mode

.auto_list:
    ; La BIOS Phoenix de TravelMate 250 ya configuraba correctamente este
    ; modo en BlesKernOS 0.6. No reinterpretar sus registros Intel: adoptar
    ; literalmente el LFB 0x114 que entrega el firmware.
    call detect_intel_852gme
    test al, al
    jz .auto_generic
    call setup_legacy_852_mode
    test al, al
    jnz .done

.auto_generic:
    ; Preferencia historica y compatible: 640x480 con 256 colores.
    ; Se prueba antes que cualquier modo de mayor resolucion/profundidad para
    ; que el arranque automatico sea estable tambien con poca RAM y solo CPU.
    mov word [vbe_search_width], 640
    mov word [vbe_search_height], 480
    mov byte [vbe_search_bpp], 8
    call find_vbe_mode_exact
    test ax, ax
    jnz .auto_found

    ; Si no existe ese modo, conservar candidatos de mejor calidad.
    mov word [vbe_search_width], 1024
    mov word [vbe_search_height], 768
    mov byte [vbe_search_bpp], 32
    call find_vbe_mode_exact
    test ax, ax
    jnz .auto_found

    mov word [vbe_search_width], 800
    mov word [vbe_search_height], 600
    call find_vbe_mode_exact
    test ax, ax
    jnz .auto_found

    mov word [vbe_search_width], 640
    mov word [vbe_search_height], 480
    call find_vbe_mode_exact
    test ax, ax
    jnz .auto_found

    ; Ultimo recurso: un modo SVGA de 800x600 con 256 colores.
    mov word [vbe_search_width], 800
    mov word [vbe_search_height], 600
    mov byte [vbe_search_bpp], 8
    call find_vbe_mode_exact
    test ax, ax
    jz .done

.auto_found:
    mov [BOOTINFO_ADDR + 15], ax
    jmp .query_mode

.try_mode:
    ; Conservado para el camino de fallo de una seleccion manual.
    lodsw
    test ax, ax
    jz .done
    mov [BOOTINFO_ADDR + 15], ax

.query_mode:

    mov di, VBE_MODEINFO
    mov ax, 0x4F01
    mov cx, [BOOTINFO_ADDR + 15]
    push si
    int 0x10
    pop si
    cmp ax, 0x004F
    jne .mode_failed

    test word [VBE_MODEINFO], 0x0001
    jz .mode_failed
    test word [VBE_MODEINFO], 0x0080
    jz .mode_failed
    mov al, [VBE_MODEINFO + 25]
    cmp al, 8
    jne .check_truecolor
    cmp byte [VBE_MODEINFO + 27], 4  ; Packed Pixel
    je .set_mode
    jmp .mode_failed
.check_truecolor:
    cmp al, 16
    je .require_direct_color
    cmp al, 24
    je .require_direct_color
    cmp al, 32
    jne .mode_failed
.require_direct_color:
    cmp byte [VBE_MODEINFO + 27], 6  ; Direct Color
    je .set_mode
    jmp .mode_failed

.mode_failed:
    ; find_vbe_mode_exact ya valido el modo automatico. Si aun asi 4F02 falla,
    ; no recorrer una tabla con SI indefinido: dejar que el kernel use VGA.
    jmp .done

.set_mode:
    push si
    mov si, msg_vesa_using
    call print_string
    mov ax, [BOOTINFO_ADDR + 15]
    call print_hex16
    mov si, msg_mode_sep
    call print_string
    mov ax, [VBE_MODEINFO + 18]
    call print_dec16
    mov al, 'x'
    call print_char
    mov ax, [VBE_MODEINFO + 20]
    call print_dec16
    mov al, 'x'
    call print_char
    xor ah, ah
    mov al, [VBE_MODEINFO + 25]
    call print_dec16
    mov si, msg_bpp_suffix
    call print_string
    pop si

    mov ax, 0x4F02
    mov bx, [BOOTINFO_ADDR + 15]
    or bx, 0x4000
    push si
    int 0x10
    pop si
    cmp ax, 0x004F
    jne .mode_failed

    ; En modos indexados la paleta debe configurarse mediante VBE. Escribir
    ; 3C8/3C9 desde protected mode no funciona en varios chips de portatiles
    ; aunque anuncien un framebuffer lineal valido.
    cmp byte [VBE_MODEINFO + 25], 8
    jne .refresh_mode_info
    call set_vbe_palette_332

.refresh_mode_info:
    ; Consultar otra vez despues de 4F02: BIOS OEM antiguas actualizan pitch o
    ; PhysBasePtr solamente cuando el modo ya esta activo.
    push ds
    push es
    xor ax, ax
    mov es, ax
    mov di, VBE_MODEINFO
    mov ax, 0x4F01
    mov cx, [BOOTINFO_ADDR + 15]
    int 0x10
    pop es
    pop ds
    cmp ax, 0x004F
    jne .mode_active_failed

    mov eax, [VBE_MODEINFO + 40]
    test eax, eax
    jz .mode_active_failed

    mov dword [BOOTINFO_ADDR], 0x31495547
    mov eax, [VBE_MODEINFO + 40]
    mov [BOOTINFO_ADDR + 4], eax
    mov ax, [VBE_MODEINFO + 18]
    mov [BOOTINFO_ADDR + 8], ax
    mov ax, [VBE_MODEINFO + 20]
    mov [BOOTINFO_ADDR + 10], ax
    ; BytesPerScanLine (+16) es la fuente segura para BIOS VBE 1.x/2.x. En
    ; VBE 3.0 aceptar LinBytesPerScanLine (+50) solo si es al menos el ancho
    ; minimo; buffers parciales de BIOS viejas pueden dejar basura en +50.
    xor ah, ah
    mov al, [VBE_MODEINFO + 25]
    add ax, 7
    shr ax, 3
    mov bx, ax                     ; bytes por pixel
    mov ax, [VBE_MODEINFO + 18]
    mul bx
    mov bx, ax                     ; pitch minimo
    mov ax, [VBE_MODEINFO + 16]
    cmp ax, bx
    jb .mode_active_failed
    mov cx, [VBE_MODEINFO + 50]
    cmp cx, bx
    jb .pitch_ready
    cmp cx, 32768
    ja .pitch_ready
    mov ax, cx
.pitch_ready:
    mov [BOOTINFO_ADDR + 12], ax
    mov al, [VBE_MODEINFO + 25]
    mov [BOOTINFO_ADDR + 14], al
    jmp .done

.mode_active_failed:
    ; Si un BIOS acepto 4F02 pero devolvio metadata incoherente, no dejar la
    ; pantalla en un modo grafico que el kernel no puede describir. VGA 12h es
    ; el respaldo grafico universal y el kernel lo activara correctamente.
    mov ax, 0x0003
    int 0x10
    mov dword [BOOTINFO_ADDR], VGA_BOOTINFO_MAGIC
    mov word [BOOTINFO_ADDR + 4], VGA_BOOTINFO_12H
    jmp .done

.bootinfo_text:
    mov dword [BOOTINFO_ADDR], VGA_BOOTINFO_MAGIC
    mov word [BOOTINFO_ADDR + 4], VGA_BOOTINFO_TEXT
    jmp .done

.bootinfo_13h:
    mov dword [BOOTINFO_ADDR], VGA_BOOTINFO_MAGIC
    mov word [BOOTINFO_ADDR + 4], VGA_BOOTINFO_13H
    jmp .done

.bootinfo_12h:
    mov dword [BOOTINFO_ADDR], VGA_BOOTINFO_MAGIC
    mov word [BOOTINFO_ADDR + 4], VGA_BOOTINFO_12H

.done:
    pop es
    popa
    ret

; Construir y aplicar una paleta RGB332 mediante VBE 4F09h.
; La tabla B,G,R,0 usa componentes DAC de 6 bits, compatibles con VBE 1.2+.
set_vbe_palette_332:
    pusha
    push ds
    push es
    xor ax, ax
    mov es, ax
    mov di, VBE_INFO
    xor si, si
.palette_loop:
    mov ax, si
    and ax, 3
    mov bl, 21
    mul bl                         ; azul: 0,21,42,63
    stosb

    mov ax, si
    shr ax, 2
    and ax, 7
    mov bl, 9
    mul bl                         ; verde: 0..63
    stosb

    mov ax, si
    shr ax, 5
    and ax, 7
    mov bl, 9
    mul bl                         ; rojo: 0..63
    stosb

    xor al, al
    stosb
    inc si
    cmp si, 256
    jb .palette_loop

    mov ax, 0x4F09
    xor bx, bx                     ; BL=0: establecer paleta
    mov cx, 256
    xor dx, dx                     ; primer indice = 0
    mov di, VBE_INFO
    int 0x10
    pop es
    pop ds
    popa
    ret

; Ruta deliberadamente igual a 0.6 para Intel 852GME/Phoenix:
; consulta 0x114, lo activa como LFB y conserva sin reinterpretar PhysBasePtr
; y BytesPerScanLine devueltos antes del cambio. AL=1 si quedo listo.
setup_legacy_852_mode:
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov di, VBE_MODEINFO
    mov ax, 0x4F01
    mov cx, 0x0114
    int 0x10
    cmp ax, 0x004F
    jne .legacy_fail
    test word [VBE_MODEINFO], 0x0001
    jz .legacy_fail
    test word [VBE_MODEINFO], 0x0080
    jz .legacy_fail
    cmp byte [VBE_MODEINFO + 25], 16
    jne .legacy_fail
    mov eax, [VBE_MODEINFO + 40]
    test eax, eax
    jz .legacy_fail
    mov ax, [VBE_MODEINFO + 16]
    cmp ax, 1600
    jb .legacy_fail
    mov ax, 0x4F02
    mov bx, 0x4114
    int 0x10
    cmp ax, 0x004F
    jne .legacy_fail
    mov dword [BOOTINFO_ADDR], 0x31495547
    mov eax, [VBE_MODEINFO + 40]
    mov [BOOTINFO_ADDR + 4], eax
    mov ax, [VBE_MODEINFO + 18]
    mov [BOOTINFO_ADDR + 8], ax
    mov ax, [VBE_MODEINFO + 20]
    mov [BOOTINFO_ADDR + 10], ax
    mov ax, [VBE_MODEINFO + 16]
    mov [BOOTINFO_ADDR + 12], ax
    mov al, [VBE_MODEINFO + 25]
    mov [BOOTINFO_ADDR + 14], al
    mov word [BOOTINFO_ADDR + 15], 0x0114
    mov al, 1
    jmp .legacy_done
.legacy_fail:
    xor al, al
.legacy_done:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; AL=1 si PCI BIOS encuentra Intel 852GME/Montara-GT (8086:3582).
detect_intel_852gme:
%ifdef TEST_FORCE_852
    mov al, 1
    ret
%endif
    push ds
    push es
    push bx
    push cx
    push dx
    push si
    mov ax, 0xB102
    mov cx, 0x3582
    mov dx, 0x8086
    xor si, si
    int 0x1A
    jc .not_852
    test ah, ah
    jnz .not_852
    mov al, 1
    jmp .detect_done
.not_852:
    xor al, al
.detect_done:
    pop si
    pop dx
    pop cx
    pop bx
    pop es
    pop ds
    ret

; Buscar en la tabla un modo LFB que coincida exactamente con las variables
; vbe_search_*. Devuelve AX=numero VBE o AX=0. Se consulta la informacion real
; de la BIOS: los numeros 0x11x/0x14x no tienen el mismo formato en todas las
; implementaciones OEM.
find_vbe_mode_exact:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, vbe_mode_candidates
.find_loop:
    lodsw
    test ax, ax
    jz .not_found
    mov [vbe_current_mode], ax

    push si
    push ds
    push es
    xor ax, ax
    mov es, ax
    mov di, VBE_MODEINFO
    mov ax, 0x4F01
    mov cx, [vbe_current_mode]
    int 0x10
    pop es
    pop ds
    pop si
    cmp ax, 0x004F
    jne .find_loop
    test word [VBE_MODEINFO], 0x0001
    jz .find_loop
    test word [VBE_MODEINFO], 0x0080
    jz .find_loop

    mov ax, [VBE_MODEINFO + 18]
    cmp ax, [vbe_search_width]
    jne .find_loop
    mov ax, [VBE_MODEINFO + 20]
    cmp ax, [vbe_search_height]
    jne .find_loop
    mov al, [VBE_MODEINFO + 25]
    cmp al, [vbe_search_bpp]
    jne .find_loop
    ; 32 bpp debe ser Direct Color (memory model 6); 8 bpp puede ser packed.
    cmp al, 32
    jne .found
    cmp byte [VBE_MODEINFO + 27], 6
    jne .find_loop
.found:
    mov ax, [vbe_current_mode]
    jmp .find_done
.not_found:
    xor ax, ax
.find_done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

vbe_mode_candidates:
    dw VBE_DEFAULT_MODE
    dw 0x101, 0x105, 0x107
    dw 0x111, 0x112, 0x114, 0x115, 0x117, 0x118
    dw 0x100, 0x102, 0x104, 0x106
    dw 0x110, 0x113, 0x116, 0x119
    dw 0x11A, 0x11B, 0x11C, 0x11D, 0x11E, 0x11F
    dw 0x120, 0x121, 0x122, 0x123, 0x124, 0x125
    dw 0x140, 0x141, 0x142, 0x143, 0x144, 0x145
    dw 0

; =============================================================================
; PRINT_STRING (16-bit, BIOS TTY)
; =============================================================================
print_string:
    pusha
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x0F
.loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    popa
    ret


print_char:
    pusha
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x0F
    int 0x10
    popa
    ret

print_hex16:
    pusha
    mov dx, ax
    mov cx, 4
.hex_loop:
    rol dx, 4
    mov al, dl
    and al, 0x0F
    cmp al, 9
    jbe .digit
    add al, 'A' - 10
    jmp .emit
.digit:
    add al, '0'
.emit:
    ; Algunas BIOS no preservan CX/BX en int 10h.
    ; Guardamos CX porque lo usa LOOP y no usamos BX para guardar el numero.
    push cx
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x0F
    int 0x10
    pop cx
    loop .hex_loop
    popa
    ret

print_dec16:
    pusha
    cmp ax, 0
    jne .convert
    mov al, '0'
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x0F
    int 0x10
    jmp .done
.convert:
    xor cx, cx
    mov bx, 10
.div_loop:
    xor dx, dx
    div bx
    push dx
    inc cx
    test ax, ax
    jnz .div_loop
.print_loop:
    pop ax
    add al, '0'
    push cx
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x0F
    int 0x10
    pop cx
    loop .print_loop
.done:
    popa
    ret

; =============================================================================
; GDT - Global Descriptor Table
; Descriptores mínimos para entrar a modo protegido flat 4GB
; =============================================================================
align 8
gdt_start:

; Descriptor 0: Nulo (obligatorio)
gdt_null:
    dq 0

; Descriptor 1 (selector 0x08): Code Segment - 32 bit, flat, ring 0
gdt_code:
    dw 0xFFFF               ; Límite bits 0-15
    dw 0x0000               ; Base bits 0-15
    db 0x00                 ; Base bits 16-23
    db 10011010b            ; Access: Present|Ring0|Code|Exec|Read
    db 11001111b            ; Flags: 4KB granularidad, 32-bit | límite bits 16-19
    db 0x00                 ; Base bits 24-31

; Descriptor 2 (selector 0x10): Data Segment - 32 bit, flat, ring 0
gdt_data:
    dw 0xFFFF               ; Límite bits 0-15
    dw 0x0000               ; Base bits 0-15
    db 0x00                 ; Base bits 16-23
    db 10010010b            ; Access: Present|Ring0|Data|Write
    db 11001111b            ; Flags
    db 0x00                 ; Base bits 24-31

; Descriptor 3 (selector 0x18): Code Segment - 16 bit, flat, ring 0
gdt_code16:
    dw 0xFFFF               ; Límite bits 0-15
    dw 0x0000               ; Base bits 0-15
    db 0x00                 ; Base bits 16-23
    db 10011010b            ; Access: Present|Ring0|Code|Exec|Read
    db 10001111b            ; Flags: 4KB granularidad, 16-bit | límite bits 16-19
    db 0x00                 ; Base bits 24-31

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; Tamaño GDT - 1
    dd gdt_start                 ; Dirección física de la GDT

; DAP para el kernel
kern_dap:
kern_dap_size     db 0x10
kern_dap_res      db 0x00
kern_dap_sectors  dw KERNEL_SECTORS
kern_dap_offset   dw KERNEL_BOUNCE_ADDR & 0x000F
kern_dap_segment  dw KERNEL_BOUNCE_ADDR >> 4
kern_dap_lba_lo   dd KERNEL_START_LBA
kern_dap_lba_hi   dd 0
boot_drive        db 0
boot_part_lba     dd 0

kernel_remaining   dw 0
kernel_chunk_sectors dw 0
kernel_destination dd KERNEL_LOAD_ADDR
; Mensajes
msg_stage2_hello    db 0x0D, 0x0A, '  [Stage 2] BleskernOS cargando...', 0x0D, 0x0A, 0
msg_detecting_ram   db '  > Detectando RAM (E820)...', 0x0D, 0x0A, 0
msg_ram_done        db '  [OK] Mapa de memoria obtenido.', 0x0D, 0x0A, 0
msg_enabling_a20    db '  > Habilitando linea A20...', 0x0D, 0x0A, 0
msg_a20_ok          db '  [OK] A20 habilitada.', 0x0D, 0x0A, 0
msg_a20_fail        db '  [ERROR] A20 no se pudo habilitar!', 0x0D, 0x0A, 0
msg_loading_kernel  db '  > Cargando kernel...', 0x0D, 0x0A, 0
msg_kernel_ok       db '  [OK] Kernel en memoria.', 0x0D, 0x0A, 0
msg_kernel_fail     db '  [ERROR] No se pudo cargar kernel!', 0x0D, 0x0A, 0
msg_kernel_verify_fail db '  [ERROR] BIOS copio mal el kernel en memoria alta!', 0x0D, 0x0A, 0
msg_entering_pm     db '  > Entrando a Modo Protegido...', 0x0D, 0x0A, 0
msg_vga_header      db '  > Modos VGA compatibles:', 0x0D, 0x0A, 0
msg_vga_text_choice db '    T. VGA 03h: 80x25 texto', 0x0D, 0x0A, 0
msg_vga_13h_note    db '    G. VGA 13h: 320x200x8', 0x0D, 0x0A, 0
msg_vga_12h_note    db '    H. VGA 12h: 640x480x4', 0x0D, 0x0A, 0
msg_vesa_header     db '  > Modos VESA LFB compatibles:', 0x0D, 0x0A, 0
msg_choice_prefix   db '    ', 0
msg_choice_mid      db '. VESA 0x', 0
msg_mode_prefix     db '    - VESA 0x', 0
msg_mode_sep        db ': ', 0
msg_bpp_suffix      db ' bpp', 0x0D, 0x0A, 0
msg_no_vesa_modes   db '    No se encontraron modos VESA LFB compatibles.', 0x0D, 0x0A, 0
msg_choose_mode     db '  Elegi modo: 1-9 VESA, 0 auto, T texto, G 13h, H 12h: ', 0
msg_invalid_choice  db 0x0D, 0x0A, '  Opcion invalida.', 0x0D, 0x0A, 0
msg_selected_vesa   db 0x0D, 0x0A, '  > Seleccionado VESA 0x', 0
msg_selected_auto   db 0x0D, 0x0A, '  > Seleccion automatica VESA.', 0x0D, 0x0A, 0
msg_selected_text   db 0x0D, 0x0A, '  > Seleccionado VGA texto.', 0x0D, 0x0A, 0
msg_selected_13h    db 0x0D, 0x0A, '  > Seleccionado VGA 13h. El kernel aplicara el modo.', 0x0D, 0x0A, 0
msg_selected_12h    db 0x0D, 0x0A, '  > Seleccionado VGA 12h. El kernel aplicara el modo.', 0x0D, 0x0A, 0
msg_newline         db 0x0D, 0x0A, 0
msg_vesa_using      db '  > Usando modo VESA 0x', 0
vbe_current_mode    dw 0
selected_vbe_mode   dw 0       ; 0=auto, 0xFFFF=VGA texto, otro=modo VESA elegido
vbe_menu_count      db 0
vbe_menu_pass_bpp   db 32
vbe_menu_modes      times 9 dw 0
vbe_search_width    dw 0
vbe_search_height   dw 0
vbe_search_bpp      db 0

; =============================================================================
; CÓDIGO DE 32 BITS — Modo Protegido
; =============================================================================
[BITS 32]
protected_mode_entry:
    ; Configurar todos los selectores de datos con el descriptor 0x10
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; La BSS termina como máximo en 0x340000. Reservar 64 KiB inmediatamente
    ; después libera 0.7 MiB para el heap del perfil recomendado de 8 MiB.
    mov esp, 0x00350000
    ; Saltar al kernel
    jmp KERNEL_LOAD_ADDR

; Rellenar hasta el limite de Stage 2 (8 sectores = 4096 bytes)
times (8 * 512) - ($ - $$) db 0
