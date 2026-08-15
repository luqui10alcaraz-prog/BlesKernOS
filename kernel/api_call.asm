[bits 32]

%define KERNEL_DATA_SELECTOR 0x10
%define API_STACK_CANARY     0xB1E5CA11
%define API_ARGUMENT_BYTES   64
%define API_FRAME_BYTES      80
%define API_MAX_CLEANUP      64

global elf_api_call_raw
global elf_api_call_raw_after_target
global elf_api_call_raw_store_cleanup
global elf_api_call_raw_end

; uint64_t elf_api_call_raw(uint32_t target, const uint32_t *args,
;                           uint32_t *callee_cleanup)
; Ejecuta una funcion cdecl o stdcall sobre una copia kernel de 16 argumentos.
; Restaurar ESP desde EBP permite aceptar `ret N` sin conocer la firma.
;
; A public API implementation is still third-party-facing kernel code.  Do
; not trust it to leave DF or the visible data selectors untouched.  A stale
; selector used to make the cleanup write below raise #GP after long 3D loads.
elf_api_call_raw:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 4
    mov dword [ss:ebp - 16], API_STACK_CANARY

    mov ebx, [ss:ebp + 8]
    mov esi, [ss:ebp + 12]
    mov ecx, 15
.push_arguments:
    push dword [ds:esi + ecx * 4]
    dec ecx
    jns .push_arguments

    call ebx

elf_api_call_raw_after_target:
    ; Preserve EAX:EDX without touching memory until flat kernel selectors are
    ; restored. EBX/ESI are private scratch now; their caller values remain in
    ; the wrapper frame.
    mov ecx, eax
    mov edi, edx
    cld
    mov ax, KERNEL_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ebx, ecx
    mov esi, edi

    cmp dword [ss:ebp - 16], API_STACK_CANARY
    jne .corrupt_frame

    ; Tras el retorno: ESP=(EBP-80)+N, donde N es el `ret N` stdcall.
    mov ecx, esp
    sub ecx, ebp
    add ecx, API_FRAME_BYTES
    test ecx, 3
    jnz .corrupt_frame
    cmp ecx, API_MAX_CLEANUP
    ja .corrupt_frame
    jmp elf_api_call_raw_store_cleanup

.corrupt_frame:
    ; The syscall layer treats this sentinel as an invalid ABI and terminates
    ; only the offending process instead of continuing with a damaged frame.
    mov ecx, 0xFFFFFFFF
    xor ebx, ebx
    xor esi, esi

elf_api_call_raw_store_cleanup:
.store_cleanup:
    mov edi, [ss:ebp + 16]
    mov [ss:edi], ecx
    mov eax, ebx
    mov edx, esi

    lea esp, [ebp - 12]
    pop edi
    pop esi
    pop ebx
    pop ebp
elf_api_call_raw_end:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
