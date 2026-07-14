; =============================================================================
; BleskernOS - Stage 2 Bootloader
; Vive en 0x7E00, cargado por Stage 1
; Tareas:
;   1. Detectar memoria RAM (INT 15h E820)
;   2. Habilitar línea A20
;   3. Cargar GDT
;   4. Entrar a Modo Protegido (32-bit)
;   5. Saltar al kernel (en 0x10000 = 64KB)
; =============================================================================

[BITS 16]
[ORG 0x7E00]

; -----------------------------------------------------------------------------
; Constantes
; -----------------------------------------------------------------------------
KERNEL_LOAD_ADDR equ 0x10000    ; Donde cargamos el kernel (64KB mark)
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 1024     ; Sectores reservados para el kernel (512KB)
%endif
KERNEL_START_LBA equ 9          ; LBA donde empieza el kernel en disco (Stage2 ahora ocupa 8 sectores)
FLOPPY_TRACK_SPAN equ 36
FLOPPY_SECTORS_PER_TRACK equ 18
BOOTINFO_ADDR    equ 0x0700     ; Datos de video que stage2 pasa al kernel
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
    cmp dl, 0x80
    jb .load_chs

    ; Leer en bloques pequenos para no cruzar limites de 64 KiB del BIOS.
    mov cx, KERNEL_SECTORS
    mov bx, KERNEL_LOAD_ADDR >> 4
    mov eax, [boot_part_lba]
    add eax, KERNEL_START_LBA
    mov dword [kern_dap_lba_lo], eax
    mov dword [kern_dap_lba_hi], 0

.next_chunk:
    mov ax, cx
    cmp ax, 32
    jbe .chunk_size_ok
    mov ax, 32

.chunk_size_ok:
    mov word [kern_dap_sectors], ax
    mov word [kern_dap_offset], 0
    mov word [kern_dap_segment], bx

    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, kern_dap
    int 0x13
    jc .error

    mov ax, [kern_dap_sectors]
    sub cx, ax
    movzx edx, ax
    add dword [kern_dap_lba_lo], edx
    shl ax, 5                   ; sectores * 512 / 16 = parrafos
    add bx, ax
    test cx, cx
    jnz .next_chunk

    popa
    clc
    ret

.load_chs:
    mov ax, KERNEL_LOAD_ADDR >> 4
    mov es, ax
    mov si, KERNEL_START_LBA
    mov cx, KERNEL_SECTORS

.chs_next_sector:
    push cx
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
    xor bx, bx
    mov ah, 0x02
    mov al, 0x01
    mov dl, [boot_drive]
    int 0x13
    jc .error_chs

    mov ax, es
    add ax, 32
    mov es, ax
    inc si
    pop cx
    loop .chs_next_sector

    popa
    clc
    ret

.error_chs:
    pop cx
.error:
    popa
    stc
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
    mov si, vbe_mode_candidates

.vesa_loop:
    lodsw
    test ax, ax
    jz .vesa_done
    mov [vbe_current_mode], ax

    ; Consultar el modo candidato. Guardamos SI/DS porque algunas BIOS tocan registros.
    push si
    push ds
    xor ax, ax
    mov es, ax
    mov di, VBE_MODEINFO
    mov ax, 0x4F01
    mov cx, [vbe_current_mode]
    int 0x10
    pop ds
    pop si
    cmp ax, 0x004F
    jne .vesa_loop

    ; Filtrar: modo soportado + Linear Framebuffer disponible.
    test word [VBE_MODEINFO], 0x0001
    jz .vesa_loop
    test word [VBE_MODEINFO], 0x0080
    jz .vesa_loop

    ; El kernel ya maneja estos bpp en vesa_attach_lfb().
    mov al, [VBE_MODEINFO + 25]
    cmp al, 8
    je .store_and_print
    cmp al, 16
    je .store_and_print
    cmp al, 24
    je .store_and_print
    cmp al, 32
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
    mov si, vbe_mode_candidates

.try_mode:
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
    je .set_mode
    cmp al, 16
    je .set_mode
    cmp al, 24
    je .set_mode
    cmp al, 32
    je .set_mode
    jmp .mode_failed

.mode_failed:
    ; Si era seleccion manual, no seguir leyendo la tabla con SI indefinido.
    cmp word [selected_vbe_mode], 0
    jne .done
    jmp .try_mode

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

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; Tamaño GDT - 1
    dd gdt_start                 ; Dirección física de la GDT

; DAP para el kernel
kern_dap:
kern_dap_size     db 0x10
kern_dap_res      db 0x00
kern_dap_sectors  dw KERNEL_SECTORS
kern_dap_offset   dw KERNEL_LOAD_ADDR & 0xFFFF
kern_dap_segment  dw KERNEL_LOAD_ADDR >> 4
kern_dap_lba_lo   dd KERNEL_START_LBA
kern_dap_lba_hi   dd 0
boot_drive        db 0
boot_part_lba     dd 0

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
vbe_menu_modes      times 9 dw 0

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

    ; La 0.4 tiene un .bss mucho mayor que la 0.3 (principalmente por DOOM).
    ; 0x9FFFF ahora cae dentro del .bss y la pila corrompe VFS, block devices,
    ; tareas e IDT. El heap comienza en 0x200000, por lo que dejamos la pila
    ; justo debajo de él, con espacio holgado para crecer hacia abajo.
    mov esp, 0x001FF000

    ; Escribir mensaje directo en video memory (0xB8000)
    ; Ya no tenemos BIOS, pero sí VGA text mode buffer
    mov edi, 0x000B8000
    mov esi, pm_msg
    call pm_print

    ; Saltar al kernel
    jmp KERNEL_LOAD_ADDR

pm_print:
    push eax
    push edi
.loop:
    mov al, [esi]
    test al, al
    jz .done
    mov byte [edi], al
    mov byte [edi+1], 0x0A  ; Verde sobre negro
    add edi, 2
    inc esi
    jmp .loop
.done:
    pop edi
    pop eax
    ret

pm_msg db '  [32-bit] Modo Protegido OK! Saltando al kernel...', 0

; Rellenar hasta el limite de Stage 2 (8 sectores = 4096 bytes)
times (8 * 512) - ($ - $$) db 0
