#include "bleskernos.h"

enum {
    SYS_EXIT = 0, SYS_WRITE, SYS_GETPID, SYS_YIELD, SYS_SLEEP,
    SYS_UPTIME_MS, SYS_ABI_VERSION, SYS_WIN32_EXCEPTION_RETURN,
    SYS_GETPPID, SYS_OPEN, SYS_READ, SYS_CLOSE, SYS_GETCWD, SYS_CHDIR,
    SYS_MKDIR, SYS_UNLINK, SYS_RENAME, SYS_GETDENTS, SYS_ALLOC,
    SYS_REALLOC, SYS_FREE, SYS_SPAWN, SYS_WAITPID, SYS_KILL,
    SYS_API_CALL, SYS_UPCALL_RETURN, SYS_NET_RESOLVE, SYS_NET_SOCKET,
    SYS_NET_CONNECT, SYS_NET_SEND, SYS_NET_RECV, SYS_NET_CLOSE,
    SYS_NET_HTTP_GET, SYS_WIN16_RELAY, SYS_THREAD_CREATE, SYS_THREAD_JOIN,
    SYS_THREAD_DETACH, SYS_THREAD_EXIT, SYS_THREAD_SELF,
    SYS_THREAD_TLS_SET, SYS_THREAD_TLS_GET
};

static bk_i32 call0(bk_u32 number) {
    bk_i32 result;
    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(number) : "memory");
    return result;
}

static bk_i32 call1(bk_u32 number, bk_u32 a) {
    bk_i32 result;
    __asm__ volatile ("int $0x80" : "=a"(result)
                      : "a"(number), "b"(a) : "memory");
    return result;
}

static bk_i32 call2(bk_u32 number, bk_u32 a, bk_u32 b) {
    bk_i32 result;
    __asm__ volatile ("int $0x80" : "=a"(result)
                      : "a"(number), "b"(a), "c"(b) : "memory");
    return result;
}

static bk_i32 call3(bk_u32 number, bk_u32 a, bk_u32 b, bk_u32 c) {
    bk_i32 result;
    __asm__ volatile ("int $0x80" : "=a"(result)
                      : "a"(number), "b"(a), "c"(b), "d"(c) : "memory");
    return result;
}

static bk_i32 call4(bk_u32 number, bk_u32 a, bk_u32 b, bk_u32 c, bk_u32 d) {
    bk_i32 result;
    __asm__ volatile ("int $0x80" : "=a"(result)
                      : "a"(number), "b"(a), "c"(b), "d"(c), "S"(d)
                      : "memory");
    return result;
}

bk_i32 bk_abi_version(void) { return call0(SYS_ABI_VERSION); }
bk_i32 bk_write(bk_i32 fd, const void *p, bk_u32 n) {
    return call3(SYS_WRITE, (bk_u32)fd, (bk_u32)p, n);
}
bk_i32 bk_read(bk_i32 fd, void *p, bk_u32 n) {
    return call3(SYS_READ, (bk_u32)fd, (bk_u32)p, n);
}
bk_i32 bk_open(const char *path, bk_u32 flags) {
    return call2(SYS_OPEN, (bk_u32)path, flags);
}
bk_i32 bk_close(bk_i32 fd) { return call1(SYS_CLOSE, (bk_u32)fd); }
bk_i32 bk_getcwd(char *buffer, bk_u32 capacity) {
    return call2(SYS_GETCWD, (bk_u32)buffer, capacity);
}
bk_i32 bk_chdir(const char *path) { return call1(SYS_CHDIR, (bk_u32)path); }
bk_i32 bk_mkdir(const char *path) { return call1(SYS_MKDIR, (bk_u32)path); }
bk_i32 bk_unlink(const char *path) { return call1(SYS_UNLINK, (bk_u32)path); }
bk_i32 bk_rename(const char *a, const char *b) {
    return call2(SYS_RENAME, (bk_u32)a, (bk_u32)b);
}
bk_i32 bk_getdents(const char *path, bk_dirent_t *entries,
                   bk_u32 capacity, bk_u32 *count) {
    return call4(SYS_GETDENTS, (bk_u32)path, (bk_u32)entries,
                 capacity, (bk_u32)count);
}
void *bk_malloc(bk_u32 size) { return (void *)call1(SYS_ALLOC, size); }
void *bk_realloc(void *pointer, bk_u32 size) {
    return (void *)call2(SYS_REALLOC, (bk_u32)pointer, size);
}
bk_i32 bk_free(void *pointer) { return call1(SYS_FREE, (bk_u32)pointer); }
bk_i32 bk_getpid(void) { return call0(SYS_GETPID); }
bk_i32 bk_getppid(void) { return call0(SYS_GETPPID); }

bk_i32 bk_thread_sys_create(void (*entry)(void *), void *argument,
                            bk_u32 stack_size, const char *name) {
    return call4(SYS_THREAD_CREATE, (bk_u32)entry, (bk_u32)argument,
                 stack_size, (bk_u32)name);
}

bk_i32 bk_thread_sys_join(bk_u32 tid, bk_i32 *result) {
    return call2(SYS_THREAD_JOIN, tid, (bk_u32)result);
}

bk_i32 bk_thread_sys_detach(bk_u32 tid) {
    return call1(SYS_THREAD_DETACH, tid);
}

void bk_thread_sys_exit(bk_i32 result) {
    (void)call1(SYS_THREAD_EXIT, (bk_u32)result);
    for (;;) (void)call0(SYS_YIELD);
}

bk_u32 bk_thread_sys_self(void) {
    return (bk_u32)call0(SYS_THREAD_SELF);
}

bk_i32 bk_thread_sys_tls_set(void *base) {
    return call1(SYS_THREAD_TLS_SET, (bk_u32)base);
}

void *bk_thread_sys_tls_get(void) {
    return (void *)(bk_u32)call0(SYS_THREAD_TLS_GET);
}
bk_u32 bk_uptime_ms(void) { return (bk_u32)call0(SYS_UPTIME_MS); }
void bk_yield(void) { (void)call0(SYS_YIELD); }
void bk_sleep(bk_u32 ticks) { (void)call1(SYS_SLEEP, ticks); }
bk_i32 bk_spawn(const char *path, const char *argument) {
    return call2(SYS_SPAWN, (bk_u32)path, (bk_u32)argument);
}
bk_i32 bk_waitpid_nohang(bk_u32 pid, bk_i32 *status) {
    return call2(SYS_WAITPID, pid, (bk_u32)status);
}
bk_i32 bk_waitpid(bk_u32 pid, bk_i32 *status) {
    bk_i32 result;
    do {
        result = bk_waitpid_nohang(pid, status);
        if (result == 0) bk_yield();
    } while (result == 0);
    return result;
}
bk_i32 bk_kill(bk_u32 pid) { return call1(SYS_KILL, pid); }
bk_i32 bk_dns_resolve(const char *hostname, bk_u8 address[4],
                      bk_u32 timeout_ms) {
    return call3(SYS_NET_RESOLVE, (bk_u32)hostname, (bk_u32)address,
                 timeout_ms);
}
bk_i32 bk_socket(bk_u32 type) { return call1(SYS_NET_SOCKET, type); }
bk_i32 bk_connect(bk_i32 socket, const bk_u8 address[4], bk_u16 port,
                  bk_u32 timeout_ms) {
    return call4(SYS_NET_CONNECT, (bk_u32)socket, (bk_u32)address,
                 (bk_u32)port, timeout_ms);
}
bk_i32 bk_send(bk_i32 socket, const void *data, bk_u32 length,
               bk_u32 timeout_ms) {
    return call4(SYS_NET_SEND, (bk_u32)socket, (bk_u32)data, length,
                 timeout_ms);
}
bk_i32 bk_recv(bk_i32 socket, void *data, bk_u32 capacity,
               bk_u32 timeout_ms) {
    return call4(SYS_NET_RECV, (bk_u32)socket, (bk_u32)data, capacity,
                 timeout_ms);
}
bk_i32 bk_socket_close(bk_i32 socket) {
    return call1(SYS_NET_CLOSE, (bk_u32)socket);
}
bk_i32 bk_http_get(const char *url, void *data, bk_u32 capacity,
                   bk_u32 timeout_ms) {
    return call4(SYS_NET_HTTP_GET, (bk_u32)url, (bk_u32)data, capacity,
                 timeout_ms);
}
void bk_exit(bk_i32 status) {
    (void)call1(SYS_EXIT, (bk_u32)status);
    for (;;) bk_yield();
}
bk_u32 bk_strlen(const char *text) {
    bk_u32 length = 0;
    if (text) while (text[length]) length++;
    return length;
}
bk_i32 bk_puts(const char *text) {
    bk_i32 result = bk_write(1, text, bk_strlen(text));
    if (result >= 0) (void)bk_write(1, "\n", 1);
    return result;
}
