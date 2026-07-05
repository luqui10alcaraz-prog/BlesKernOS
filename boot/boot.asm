; =============================================================================
; BlesKernOS - Stage 1 Bootloader FAT12
; Sector de arranque de floppy con BPB valida.
; =============================================================================

[BITS 16]
[ORG 0x7C00]

jmp short start
nop

%ifndef RESERVED_SECTORS
%define RESERVED_SECTORS 517
%endif

oem_name            db 'BLESFAT '
bytes_per_sector    dw 512
sectors_per_cluster db 1
reserved_sectors    dw RESERVED_SECTORS
fat_count           db 2
root_entries        dw 224
total_sectors       dw 2880
media_descriptor    db 0xF0
fat_sectors         dw 9
sectors_per_track   dw 18
head_count          dw 2
hidden_sectors      dd 0
large_total_secs    dd 0
drive_number        db 0
reserved_nt         db 0
boot_signature      db 0x29
volume_id           dd 0x53454C42
volume_label        db 'BLESKERNOS '
fs_name             db 'FAT12   '

STAGE2_LOAD_SEG  equ 0x0000
STAGE2_LOAD_OFF  equ 0x7E00
STAGE2_SECTORS   equ 4
STAGE2_START_LBA equ 1
BOOT_DRIVE_SAVE  equ 0x0500

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [BOOT_DRIVE_SAVE], dl

    call load_stage2_lba
    jnc stage2_loaded
    call load_stage2_chs
    jnc stage2_loaded

    mov si, msg_boot_fail
    call print_string
    jmp $

stage2_loaded:
    mov dl, [BOOT_DRIVE_SAVE]
    jmp STAGE2_LOAD_SEG:STAGE2_LOAD_OFF

load_stage2_lba:
    pusha

    mov ax, STAGE2_LOAD_SEG
    mov [dap_segment], ax
    mov ax, STAGE2_LOAD_OFF
    mov [dap_offset], ax
    mov byte [dap_size], 0x10
    mov byte [dap_reserved], 0x00
    mov word [dap_sectors], STAGE2_SECTORS
    mov dword [dap_lba_lo], STAGE2_START_LBA
    mov dword [dap_lba_hi], 0

    mov ah, 0x42
    mov dl, [BOOT_DRIVE_SAVE]
    mov si, dap
    int 0x13
    jc .error

    popa
    clc
    ret

.error:
    popa
    stc
    ret

load_stage2_chs:
    pusha

    mov ah, 0x02
    mov al, STAGE2_SECTORS
    mov ch, 0x00
    mov cl, 0x02
    mov dh, 0x00
    mov dl, [BOOT_DRIVE_SAVE]
    mov bx, STAGE2_LOAD_OFF
    int 0x13
    jc .error

    popa
    clc
    ret

.error:
    popa
    stc
    ret

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

dap:
dap_size      db 0x10
dap_reserved  db 0x00
dap_sectors   dw STAGE2_SECTORS
dap_offset    dw STAGE2_LOAD_OFF
dap_segment   dw STAGE2_LOAD_SEG
dap_lba_lo    dd STAGE2_START_LBA
dap_lba_hi    dd 0

msg_boot_fail db 0x0D, 0x0A, 'Boot error', 0

times 510-($-$$) db 0
dw 0xAA55
