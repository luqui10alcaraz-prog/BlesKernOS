; =============================================================================
; BleskernOS - Kernel entry stub
; Este punto de entrada queda al inicio del binario del kernel para que Stage 2
; pueda saltar directamente a 0x00100000 y redirigir a kernel_main().
; =============================================================================

[BITS 32]
[GLOBAL _start]
[EXTERN kernel_main]
[EXTERN __bss_start]
[EXTERN __bss_end]
[GLOBAL gdt_flush]
[GLOBAL tss_flush]

section .text
_start:
    ; Stage 2 verifica esta firma antes de entrar al kernel. El salto corto
    ; mantiene _start ejecutable en 0x00100000 y deja una marca estable que
    ; permite detectar BIOS que informan exito en INT 15h/87h pero copian mal.
    jmp short .entry
    dd 0x314B4C42                  ; "BLK1" little endian
.entry:
    ; Hasta que kernel_main instale la IDT completa, cualquier #UD/#GP/#PF
    ; causaba doble y luego triple fault. Construir 32 puertas de emergencia
    ; en RAM convencional antes incluso de limpiar BSS cierra esa ventana.
    cli
    mov edi, 0x00070000
    mov ecx, 32
    mov eax, early_fault_handler
.build_early_idt:
    mov word [edi + 0], ax
    mov word [edi + 2], 0x0008
    mov byte [edi + 4], 0
    mov byte [edi + 5], 0x8E
    push eax
    shr eax, 16
    mov word [edi + 6], ax
    pop eax
    add edi, 8
    loop .build_early_idt
    lidt [early_idt_ptr]

    ; NOLOAD BSS lives above the legacy VGA/BIOS hole.  Clear it explicitly;
    ; firmware is not required to leave that RAM initialized to zero.
    cld
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    mov edx, ecx
    shr ecx, 2
    rep stosd
    mov ecx, edx
    and ecx, 3
    rep stosb

    jmp kernel_main

; Handler deliberadamente independiente de C/BSS/paging. Publica una marca
; por COM1, por el puerto de depuracion de QEMU/Bochs y por POST 80h, y se
; detiene. No intenta retornar porque los frames con/sin error code difieren.
early_fault_handler:
    cli
    mov al, 0xE1
    out 0x80, al
    mov dx, 0x00E9
    mov al, 'E'
    out dx, al
    mov dx, 0x03F8
    out dx, al
.early_halt:
    hlt
    jmp .early_halt

; Si el kernel retorna, quedamos en un bucle seguro.
.hang:
    cli
    hlt
    jmp .hang

gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.gdt_reloaded
.gdt_reloaded:
    ret

tss_flush:
    mov ax, [esp + 4]
    ltr ax
    ret

section .rodata
early_idt_ptr:
    dw (32 * 8) - 1
    dd 0x00070000
