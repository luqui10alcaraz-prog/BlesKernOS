[bits 32]

global elf_api_call_raw

; uint64_t elf_api_call_raw(uint32_t target, const uint32_t *args,
;                           uint32_t *callee_cleanup)
; Ejecuta una funcion cdecl o stdcall sobre una copia kernel de 16 argumentos.
; Restaurar ESP desde EBP permite aceptar `ret N` sin conocer la firma.
elf_api_call_raw:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov ebx, [ebp + 8]
    mov esi, [ebp + 12]
    mov ecx, 15
.push_arguments:
    push dword [esi + ecx * 4]
    dec ecx
    jns .push_arguments

    call ebx

    ; Tras el retorno: ESP=(EBP-76)+N, donde N es el `ret N` stdcall.
    mov ecx, esp
    sub ecx, ebp
    add ecx, 76
    mov edi, [ebp + 16]
    mov [edi], ecx

    lea esp, [ebp - 12]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
