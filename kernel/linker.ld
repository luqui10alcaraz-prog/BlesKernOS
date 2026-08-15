/* linker.ld - Script de linkeo para BleskernOS kernel
 * El kernel se carga en 0x00100000 (1 MiB)
 * El Stage 2 salta a kernel_main() que debe estar al inicio
 */
ENTRY(_start)

PHDRS {
    text PT_LOAD FLAGS(5); /* R-X */
    data PT_LOAD FLAGS(6); /* RW- */
}

SECTIONS {
    . = 0x00100000; /* imagen del kernel: 1 MiB */

    .text : ALIGN(16) {
        /* Stage 2 salta a 0x00100000 directamente; no interpreta ENTRY().
         * Fuerce entry.o al comienzo y deje el trampoline como codigo copiable
         * posterior. Poner .ap_trampoline primero hace que el BSP ejecute codigo
         * real-mode de 16 bits estando ya en protected mode y provoca triple fault. */
        KEEP(*entry.o(.text))
        *(.text)
        *(.text.*)
        KEEP(*(.ap_trampoline))
    } :text

    ASSERT(_start == 0x00100000,
           "_start debe permanecer en 0x00100000: Stage 2 usa salto fijo")

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
    __kernel_file_end = .;
    ASSERT(__kernel_file_end <= 0x00200000,
           "kernel .text/.rodata/.data supera el espacio 1-2 MiB")

    .bss 0x00200000 (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    /*
     * Stage 2 places the bootstrap stack at 0x3FF000. El perfil de 4 MiB usa
     * el hueco posterior al BSS como heap, por lo que no alcanza con evitar la
     * pila: también hay que conservar un margen útil para GUI, VFS y tareas.
     */
    ASSERT(__bss_end <= 0x00340000,
           "kernel .bss deja muy poco heap para el modo de 4 MiB")

    /* Metadata generada por toolchains modernos que no se usa en el kernel.
     * Descartarla evita inflar el binario plano y ocupar el espacio reservado
     * antes de la BSS. */
    /DISCARD/ : {
        *(.eh_frame)
        *(.eh_frame.*)
        *(.eh_frame_hdr)
        *(.gcc_except_table)
        *(.gcc_except_table.*)
        *(.note*)
        *(.comment)
    }
}
