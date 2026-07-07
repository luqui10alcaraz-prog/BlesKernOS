; =============================================================================
; BleskernOS - Kernel entry stub
; Este punto de entrada queda al inicio del binario del kernel para que Stage 2
; pueda saltar directamente a 0x10000 y redirigir a kernel_main().
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
    mov ax, 0x28
    ltr ax
    ret
