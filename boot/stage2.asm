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
KERNEL_START_LBA equ 5          ; LBA donde empieza el kernel en disco
FLOPPY_TRACK_SPAN equ 36
FLOPPY_SECTORS_PER_TRACK equ 18
BOOTINFO_ADDR    equ 0x0700     ; Datos de video que stage2 pasa al kernel
VBE_MODEINFO     equ 0x0800     ; Buffer temporal VBE mode info
VBE_DEFAULT_MODE equ 0x114      ; 800x600x16, modo VESA estandar

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
    mov dword [kern_dap_lba_lo], KERNEL_START_LBA
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
    xor ax, ax
    mov es, ax
    mov si, vbe_mode_candidates

.try_mode:
    lodsw
    test ax, ax
    jz .done
    mov [BOOTINFO_ADDR + 15], ax

    mov di, VBE_MODEINFO
    mov ax, 0x4F01
    mov cx, [BOOTINFO_ADDR + 15]
    push si
    int 0x10
    pop si
    cmp ax, 0x004F
    jne .try_mode

    test word [VBE_MODEINFO], 0x0001
    jz .try_mode
    test word [VBE_MODEINFO], 0x0080
    jz .try_mode
    mov al, [VBE_MODEINFO + 25]
    cmp al, 16
    je .set_mode
    cmp al, 24
    je .set_mode
    cmp al, 32
    jne .try_mode

.set_mode:
    mov ax, 0x4F02
    mov bx, [BOOTINFO_ADDR + 15]
    or bx, 0x4000
    push si
    int 0x10
    pop si
    cmp ax, 0x004F
    jne .try_mode

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

.done:
    pop es
    popa
    ret

vbe_mode_candidates:
    dw VBE_DEFAULT_MODE, 0x115, 0x117, 0x118, 0x111, 0x112, 0

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

; Rellenar hasta el limite de Stage 2 (4 sectores = 2048 bytes)
times (4 * 512) - ($ - $$) db 0
