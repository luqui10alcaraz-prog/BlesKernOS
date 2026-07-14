#include "../../kernel/include/api.h"
#include "../../kernel/include/syscall.h"

/* Deliberadamente usa la API v3 normal: el ELF loader debe convertir cada
 * simbolo externo en una transicion int 0x80. */
void bleskernos_program_main(gui_desktop_t *desktop UNUSED) {
    register uint32_t preserved_ebx __asm__("ebx") = 0xB16B00B5U;
    void *memory;
    vfs_dir_entry_t *entries;
    uint32_t count = 0;

    if (bk_sys_api_version() != BK_API_VERSION)
        (void)syscall1(SYS_EXIT, 1U);
    __asm__ volatile ("" : "+r"(preserved_ebx));
    (void)bk_sys_getpid();
    __asm__ volatile ("" : "+r"(preserved_ebx));
    if (preserved_ebx != 0xB16B00B5U)
        (void)syscall1(SYS_EXIT, 6U);
    if (bk_sys_getpid() == 0U) (void)syscall1(SYS_EXIT, 2U);
    memory = bk_sys_alloc_zero(256U);
    if (!memory) (void)syscall1(SYS_EXIT, 3U);
    bk_sys_free(memory);
    entries = (vfs_dir_entry_t *)bk_sys_alloc_zero(
        sizeof(vfs_dir_entry_t) * 4U);
    if (!entries) (void)syscall1(SYS_EXIT, 4U);
    if (!bk_file_list_dir("/", entries, 4U, &count) || count == 0U)
        (void)syscall1(SYS_EXIT, 5U);
    bk_sys_free(entries);
    (void)syscall1(SYS_EXIT, 0U);
    for (;;) (void)syscall0(SYS_YIELD);
}
