; =============================================================================
; BlesKernOS - Stage 1 Bootloader for FAT32 ATA images
;
; This binary is not written wholesale over the filesystem boot sector.
; tools/build_fat32_ata.py keeps the FAT32 BPB created by mkfs.fat and copies
; only the jump instruction plus the code area starting at FAT32_CODE_OFFSET.
; =============================================================================

[BITS 16]
[ORG 0x7C00]

FAT32_CODE_OFFSET equ 90

jmp start
nop

times FAT32_CODE_OFFSET-($-$$) db 0

STAGE2_LOAD_SEG  equ 0x0000
STAGE2_LOAD_OFF  equ 0x7E00
STAGE2_SECTORS   equ 8
STAGE2_START_LBA equ 1
BOOT_DRIVE_SAVE  equ 0x0500
BOOT_PART_LBA_SAVE equ 0x0504
BPB_HIDDEN_SECTORS equ 0x7C00 + 28

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [BOOT_DRIVE_SAVE], dl
    mov eax, [BPB_HIDDEN_SECTORS]
    mov [BOOT_PART_LBA_SAVE], eax

    call load_stage2_lba
    jnc stage2_loaded

    mov si, msg_boot_fail
    call print_string
    jmp $

stage2_loaded:
    mov dl, [BOOT_DRIVE_SAVE]
    jmp STAGE2_LOAD_SEG:STAGE2_LOAD_OFF

load_stage2_lba:
    pusha

    mov byte [dap_size], 0x10
    mov byte [dap_reserved], 0x00
    mov word [dap_sectors], STAGE2_SECTORS
    mov word [dap_offset], STAGE2_LOAD_OFF
    mov word [dap_segment], STAGE2_LOAD_SEG
    mov eax, [BOOT_PART_LBA_SAVE]
    add eax, STAGE2_START_LBA
    mov dword [dap_lba_lo], eax
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

msg_boot_fail db 0x0D, 0x0A, 'ATA boot error', 0

times 510-($-$$) db 0
dw 0xAA55
