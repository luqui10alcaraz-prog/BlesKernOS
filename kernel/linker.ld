/* linker.ld - Script de linkeo para BleskernOS kernel
 * El kernel se carga en 0x10000 (64KB)
 * El Stage 2 salta a kernel_main() que debe estar al inicio
 */

ENTRY(kernel_main)

PHDRS {
    text PT_LOAD FLAGS(5); /* R-X */
    data PT_LOAD FLAGS(6); /* RW- */
}

SECTIONS {
    . = 0x10000;    /* Direccion de carga = donde Stage 2 nos pone */

    .text : ALIGN(16) {
        *(.text)
        *(.text.*)
    } :text

    .rodata : ALIGN(16) {
        *(.rodata)
        *(.rodata.*)
    } :text

    . = ALIGN(0x1000);

    .data : ALIGN(16) {
        *(.data)
        *(.data.*)
    } :data

    /*
     * 0xA0000..0xFFFFF is not general-purpose RAM on a PC: it contains the
     * VGA aperture and BIOS ROMs.  The 0.4 kernel's BSS no longer fits below
     * that hole, so keep all zero-initialized globals above 1 MiB.
     */
    .bss 0x00100000 (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    /*
     * Stage 2 places the bootstrap stack at 0x1FF000 and the heap starts at
     * 0x200000. Fail the link instead of silently allowing globals to grow
     * into the bootstrap stack.
     */
    ASSERT(__bss_end < 0x001F0000,
           "kernel .bss overlaps bootstrap stack")

    /DISCARD/ : {
        *(.note.GNU-stack)
        *(.comment)
    }
}
