; =============================================================================
; BlesKernOS - USB-HDD MBR chainloader
; Loads the active partition boot sector to 0000:7C00 and jumps to it.
; =============================================================================

[BITS 16]
[ORG 0x7C00]

RELOC_BASE equ 0x0600
PARTITION_TABLE equ 0x01BE

%define R(label) (RELOC_BASE + (label - $$))

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    cld
    mov si, 0x7C00
    mov di, RELOC_BASE
    mov cx, 256
    rep movsw
    jmp 0x0000:R(relocated)

relocated:
    xor ax, ax
    mov ds, ax
    mov es, ax

    mov si, RELOC_BASE + PARTITION_TABLE
    mov cx, 4

.find_active:
    cmp byte [si], 0x80
    je .found
    add si, 16
    loop .find_active

    mov si, RELOC_BASE + PARTITION_TABLE
    cmp byte [si + 4], 0
    jne .found
    mov si, msg_no_partition
    call print_string
    jmp halt

.found:
    mov eax, [si + 8]
    mov [R(part_lba)], eax

    mov byte [R(dap_size)], 0x10
    mov byte [R(dap_reserved)], 0
    mov word [R(dap_sectors)], 1
    mov word [R(dap_offset)], 0x7C00
    mov word [R(dap_segment)], 0
    mov eax, [R(part_lba)]
    mov [R(dap_lba_lo)], eax
    mov dword [R(dap_lba_hi)], 0

    mov ah, 0x42
    mov dl, [R(boot_drive)]
    mov si, R(dap)
    int 0x13
    jc .read_error

    mov dl, [R(boot_drive)]
    jmp 0x0000:0x7C00

.read_error:
    mov si, msg_read_error
    call print_string

halt:
    hlt
    jmp halt

print_string:
    pusha
    mov ah, 0x0E
    mov bh, 0
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

boot_drive db 0
part_lba dd 0

dap:
dap_size db 0x10
dap_reserved db 0
dap_sectors dw 1
dap_offset dw 0x7C00
dap_segment dw 0
dap_lba_lo dd 0
dap_lba_hi dd 0

msg_no_partition db 0x0D, 0x0A, 'No active partition', 0
msg_read_error db 0x0D, 0x0A, 'Partition boot error', 0

times 446-($-$$) db 0
times 64 db 0
dw 0xAA55
