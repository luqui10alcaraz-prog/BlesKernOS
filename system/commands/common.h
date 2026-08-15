#ifndef BK_COMMAND_COMMON_H
#define BK_COMMAND_COMMON_H

#include "../../sdk/include/bleskernos_api.h"

#define COMMAND_ARG_MAX 256
#define COMMAND_MAX_ARGS 16
#define COMMAND_DIR_MAX BK_DIRECTORY_MAX

typedef struct {
    int argc;
    char *argv[COMMAND_MAX_ARGS + 1];
    char storage[COMMAND_ARG_MAX];
} command_args_t;

void command_load_args(command_args_t *args);
bool command_is(const char *left, const char *right);
uint32_t command_number(const char *text, bool *valid);
int command_error(const char *name, const char *reason);
int command_list_directory(const char *path);
int command_print_file(const char *path, bool paged);
int command_copy_file(const char *from, const char *to, bool remove_source);
void command_tree(const char *path);
void command_find(const char *path, const char *pattern);
int command_show_processes(bool detailed);
int command_show_pci(bool usb_only);
int command_hexdump(const char *path);
int command_strings(const char *path);
int command_checksum(const char *path);

#define BK_COMMAND_MAIN(handler)                                      \
    void bleskernos_program_main(void *desktop UNUSED) {              \
        command_args_t command_args;                                  \
        (void)desktop;                                                \
        command_load_args(&command_args);                             \
        if (command_args.argc)                                        \
            (void)handler(command_args.argc, command_args.argv);      \
        bk_proc_exit();                                               \
    }

#endif
