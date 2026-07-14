#ifndef BLESKERNOS_USER_API_H
#define BLESKERNOS_USER_API_H

/* ABI publica de user-space. Este header no incluye ningun header del kernel. */
typedef unsigned char bk_u8;
typedef unsigned short bk_u16;
typedef unsigned int bk_u32;
typedef signed int bk_i32;
typedef unsigned int bk_size_t;

#define BK_SYSCALL_ABI_VERSION 2U
#define BK_O_RDONLY 0x0001U
#define BK_O_WRONLY 0x0002U
#define BK_O_RDWR   0x0003U
#define BK_PATH_MAX 260U
#define BK_NAME_MAX 256U
#define BK_DIRENT_MAX 64U

typedef enum {
    BK_NODE_NONE = 0,
    BK_NODE_FILE,
    BK_NODE_DIR
} bk_node_type_t;

typedef struct {
    char name[BK_NAME_MAX];
    bk_u32 size;
    bk_node_type_t type;
    bk_u8 attributes;
} bk_dirent_t;

bk_i32 bk_abi_version(void);
bk_i32 bk_write(bk_i32 fd, const void *buffer, bk_u32 length);
bk_i32 bk_read(bk_i32 fd, void *buffer, bk_u32 length);
bk_i32 bk_open(const char *path, bk_u32 flags);
bk_i32 bk_close(bk_i32 fd);
bk_i32 bk_getcwd(char *buffer, bk_u32 capacity);
bk_i32 bk_chdir(const char *path);
bk_i32 bk_mkdir(const char *path);
bk_i32 bk_unlink(const char *path);
bk_i32 bk_rename(const char *old_path, const char *new_path);
bk_i32 bk_getdents(const char *path, bk_dirent_t *entries,
                   bk_u32 capacity, bk_u32 *count);
void *bk_malloc(bk_u32 size);
void *bk_realloc(void *pointer, bk_u32 size);
bk_i32 bk_free(void *pointer);
bk_i32 bk_getpid(void);
bk_i32 bk_getppid(void);
bk_u32 bk_uptime_ms(void);
void bk_yield(void);
void bk_sleep(bk_u32 ticks);
bk_i32 bk_spawn(const char *path, const char *argument);
bk_i32 bk_waitpid(bk_u32 pid, bk_i32 *status);
bk_i32 bk_waitpid_nohang(bk_u32 pid, bk_i32 *status);
bk_i32 bk_kill(bk_u32 pid);
void bk_exit(bk_i32 status) __attribute__((noreturn));
bk_u32 bk_strlen(const char *text);
bk_i32 bk_puts(const char *text);

#endif
