#include "../kernel/include/api.h"
#include "../kernel/include/network.h"
#include "../kernel/include/perfmon.h"

#define SHELL_ALIAS_MAX 16
#define SHELL_VAR_MAX 16
#define SHELL_NAME_MAX 24

typedef struct {
    char name[SHELL_NAME_MAX];
    char value[SHELL_MAX_CMD];
} shell_pair_t;

static char history[SHELL_HISTORY_LEN][SHELL_MAX_CMD];
static int history_count;
static int history_idx;
static shell_pair_t aliases[SHELL_ALIAS_MAX];
static shell_pair_t variables[SHELL_VAR_MAX];
static bool exit_requested;
static char previous_cwd[VFS_MAX_PATH] = "/";

static void copy_text(char *dst, uint32_t capacity, const char *src) {
    if (!dst || !capacity) return;
    bk_runtime_strncpy(dst, src ? src : "", capacity - 1U);
    dst[capacity - 1U] = '\0';
}

static bool append_text(char *dst, uint32_t capacity, const char *src) {
    uint32_t used = (uint32_t)bk_runtime_strlen(dst);
    uint32_t add = (uint32_t)bk_runtime_strlen(src ? src : "");
    if (used + add >= capacity) return false;
    bk_runtime_strcat(dst, src ? src : "");
    return true;
}

static int pair_find(shell_pair_t *pairs, int count, const char *name) {
    for (int i = 0; i < count; i++)
        if (pairs[i].name[0] && bk_runtime_strcmp(pairs[i].name, name) == 0)
            return i;
    return -1;
}

static bool pair_set(shell_pair_t *pairs, int count, const char *name,
                     const char *value) {
    int slot = pair_find(pairs, count, name);
    if (slot < 0) {
        for (int i = 0; i < count; i++) {
            if (!pairs[i].name[0]) { slot = i; break; }
        }
    }
    if (slot < 0 || !name || !name[0]) return false;
    copy_text(pairs[slot].name, sizeof(pairs[slot].name), name);
    copy_text(pairs[slot].value, sizeof(pairs[slot].value), value);
    return true;
}

static void pair_remove(shell_pair_t *pairs, int count, const char *name) {
    int slot = pair_find(pairs, count, name);
    if (slot >= 0) bk_runtime_memset(&pairs[slot], 0, sizeof(pairs[slot]));
}

static void history_add(const char *command) {
    int slot;
    if (!command || !command[0]) return;
    if (history_count && bk_runtime_strcmp(
            history[(history_count - 1) % SHELL_HISTORY_LEN], command) == 0)
        return;
    slot = history_count % SHELL_HISTORY_LEN;
    copy_text(history[slot], sizeof(history[slot]), command);
    history_count++;
    history_idx = history_count;
}

static void erase_input(size_t count) {
    while (count--) vga_putchar('\b');
}

static void readline(char *buffer, size_t capacity) {
    size_t pos = 0;
    bk_runtime_memset(buffer, 0, capacity);
    history_idx = history_count;
    for (;;) {
        char c = bk_input_getchar();
        if (c == '\n' || c == '\r') {
            vga_putchar('\n');
            buffer[pos] = '\0';
            return;
        }
        if (c == '\b') {
            if (pos) { buffer[--pos] = '\0'; vga_putchar('\b'); }
            continue;
        }
        if (c == KEY_UP || c == KEY_DOWN) {
            int oldest = history_count > SHELL_HISTORY_LEN
                       ? history_count - SHELL_HISTORY_LEN : 0;
            if (c == KEY_UP && history_idx > oldest) history_idx--;
            if (c == KEY_DOWN && history_idx < history_count) history_idx++;
            erase_input(pos);
            pos = 0;
            buffer[0] = '\0';
            if (history_idx < history_count) {
                const char *item = history[history_idx % SHELL_HISTORY_LEN];
                copy_text(buffer, capacity, item);
                pos = bk_runtime_strlen(buffer);
                vga_puts(buffer);
            }
            continue;
        }
        if (c >= 32 && pos + 1 < capacity) {
            buffer[pos++] = c;
            buffer[pos] = '\0';
            vga_putchar(c);
        }
    }
}

static int parse_args(char *line, char **argv, int maximum) {
    int argc = 0;
    char *read = line;
    char *write = line;
    while (*read && argc < maximum) {
        char quote = 0;
        while (*read == ' ' || *read == '\t' || *read == '\n' || *read == '\r') read++;
        if (!*read) break;
        argv[argc++] = write;
        while (*read) {
            if (!quote && (*read == ' ' || *read == '\t' ||
                           *read == '\n' || *read == '\r')) break;
            if (*read == '\\' && read[1]) {
                read++;
                *write++ = *read++;
                continue;
            }
            if (*read == '\'' || *read == '"') {
                if (!quote) { quote = *read++; continue; }
                if (quote == *read) { quote = 0; read++; continue; }
            }
            *write++ = *read++;
        }
        /* read puede coincidir con write cuando no hubo comillas. Guardar y
           saltar los separadores antes de escribir el NUL evita perder todos
           los argumentos posteriores al primero. */
        char *next = read;
        while (*next == ' ' || *next == '\t' || *next == '\n' || *next == '\r') next++;
        *write++ = '\0';
        read = next;
    }
    argv[argc] = NULL;
    return argc;
}

static void expand_variables(const char *source, char *output,
                             uint32_t capacity) {
    uint32_t out = 0;
    for (uint32_t i = 0; source && source[i] && out + 1 < capacity;) {
        if (source[i] != '$') { output[out++] = source[i++]; continue; }
        i++;
        char name[SHELL_NAME_MAX];
        uint32_t n = 0;
        while (source[i] && n + 1 < sizeof(name) &&
               ((source[i] >= 'A' && source[i] <= 'Z') ||
                (source[i] >= 'a' && source[i] <= 'z') ||
                (source[i] >= '0' && source[i] <= '9') || source[i] == '_'))
            name[n++] = source[i++];
        name[n] = '\0';
        int slot = pair_find(variables, SHELL_VAR_MAX, name);
        const char *value = slot >= 0 ? variables[slot].value : "";
        while (*value && out + 1 < capacity) output[out++] = *value++;
    }
    output[out] = '\0';
}

static int cmd_cd(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/";
    char old[VFS_MAX_PATH];
    if (argc > 2) { kprintf("cd: uso: cd [directorio]\n"); return 2; }
    if (bk_runtime_strcmp(path, "~") == 0) path = "/";
    else if (bk_runtime_strcmp(path, "-") == 0) path = previous_cwd;
    copy_text(old, sizeof(old), bk_file_getcwd());
    if (!bk_file_chdir(path)) {
        kprintf("cd: '%s': directorio inexistente o inaccesible\n", path);
        return 1;
    }
    copy_text(previous_cwd, sizeof(previous_cwd), old);
    (void)pair_set(variables, SHELL_VAR_MAX, "OLDPWD", previous_cwd);
    (void)pair_set(variables, SHELL_VAR_MAX, "PWD", bk_file_getcwd());
    if (argc > 1 && bk_runtime_strcmp(argv[1], "-") == 0)
        kprintf("%s\n", bk_file_getcwd());
    return 0;
}

static int cmd_pwd(int argc UNUSED, char **argv UNUSED) {
    kprintf("%s\n", bk_file_getcwd());
    return 0;
}

static int cmd_clear(int argc UNUSED, char **argv UNUSED) { vga_clear(); return 0; }

static int cmd_history(int argc UNUSED, char **argv UNUSED) {
    int first = history_count > SHELL_HISTORY_LEN
              ? history_count - SHELL_HISTORY_LEN : 0;
    for (int i = first; i < history_count; i++)
        kprintf("%u  %s\n", i + 1, history[i % SHELL_HISTORY_LEN]);
    return 0;
}

static int cmd_alias(int argc, char **argv) {
    if (argc == 1) {
        for (int i = 0; i < SHELL_ALIAS_MAX; i++)
            if (aliases[i].name[0])
                kprintf("alias %s='%s'\n", aliases[i].name, aliases[i].value);
        return 0;
    }
    char *equal = argv[1];
    while (*equal && *equal != '=') equal++;
    if (*equal == '=') {
        *equal++ = '\0';
        return pair_set(aliases, SHELL_ALIAS_MAX, argv[1], equal) ? 0 : 1;
    }
    if (argc < 3) { kprintf("Uso: alias nombre comando\n"); return 1; }
    char value[SHELL_MAX_CMD] = "";
    for (int i = 2; i < argc; i++) {
        if (i > 2) append_text(value, sizeof(value), " ");
        append_text(value, sizeof(value), argv[i]);
    }
    return pair_set(aliases, SHELL_ALIAS_MAX, argv[1], value) ? 0 : 1;
}

static int cmd_unalias(int argc, char **argv) {
    if (argc != 2) { kprintf("Uso: unalias nombre\n"); return 1; }
    pair_remove(aliases, SHELL_ALIAS_MAX, argv[1]);
    return 0;
}

static int cmd_set(int argc, char **argv) {
    if (argc == 1) {
        for (int i = 0; i < SHELL_VAR_MAX; i++)
            if (variables[i].name[0])
                kprintf("%s=%s\n", variables[i].name, variables[i].value);
        return 0;
    }
    char *equal = argv[1];
    while (*equal && *equal != '=') equal++;
    if (*equal == '=') {
        *equal++ = '\0';
        return pair_set(variables, SHELL_VAR_MAX, argv[1], equal) ? 0 : 1;
    }
    if (argc < 3) { kprintf("Uso: set NOMBRE=valor\n"); return 1; }
    return pair_set(variables, SHELL_VAR_MAX, argv[1], argv[2]) ? 0 : 1;
}

static int cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) vga_putchar(' ');
        vga_puts(argv[i]);
    }
    vga_putchar('\n');
    return 0;
}

static int cmd_ver(int argc UNUSED, char **argv UNUSED) {
    kprintf("BlesKernOS 0.6 - Shell 1.0 - API %u\n", bk_sys_api_version());
    return 0;
}


static bool shell_parse_perf_interval(const char *text, uint32_t *value_out) {
    uint32_t value = 0U;
    bool has_digit = false;
    if (!text || !value_out) return false;
    while (*text >= '0' && *text <= '9') {
        has_digit = true;
        value = value * 10U + (uint32_t)(*text - '0');
        text++;
    }
    if (!has_digit || *text) return false;
    *value_out = value;
    return true;
}

static int cmd_perfmon(int argc, char **argv) {
    uint32_t seconds;
    if (argc == 1 || (argc == 2 && bk_runtime_strcmp(argv[1], "status") == 0)) {
        kprintf("perfmon: %s, modo manual; use 'perfmon now' para capturar\n",
                perfmon_enabled() ? "activo" : "inactivo");
        return 0;
    }
    if (argc == 2 && bk_runtime_strcmp(argv[1], "on") == 0) {
        perfmon_set_enabled(true);
        kprintf("perfmon: activado y contadores reiniciados\n");
        return 0;
    }
    if (argc == 2 && bk_runtime_strcmp(argv[1], "off") == 0) {
        perfmon_set_enabled(false);
        kprintf("perfmon: desactivado\n");
        return 0;
    }
    if (argc == 2 && bk_runtime_strcmp(argv[1], "reset") == 0) {
        perfmon_reset();
        kprintf("perfmon: contadores reiniciados\n");
        return 0;
    }
    if (argc == 2 && bk_runtime_strcmp(argv[1], "now") == 0) {
        if (!perfmon_enabled()) {
            kprintf("perfmon: esta inactivo; use 'perfmon on' primero\n");
            return 1;
        }
        perfmon_force_report();
        kprintf("perfmon: snapshot solicitado; saldra por COM1 desde el GUI\n");
        return 0;
    }
    if (argc == 3 && bk_runtime_strcmp(argv[1], "interval") == 0 &&
        shell_parse_perf_interval(argv[2], &seconds)) {
        perfmon_set_interval_seconds(seconds);
        kprintf("perfmon: intervalo guardado en %u s; los reportes "
                "automaticos estan desactivados\n",
                perfmon_interval_seconds());
        return 0;
    }
    kprintf("Uso: perfmon [status|on|off|now|reset|interval <10..600>]\n");
    return 2;
}

static int cmd_benchmark(int argc, char **argv) {
    if (argc != 1) {
        kprintf("Uso: benchmark\n");
        return 2;
    }
    (void)argv;
    return perfmon_run_benchmark();
}

static int cmd_savecom1(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/COM1LOG.TXT";
    uint32_t size;
    uint32_t dropped;

    if (argc > 2) {
        kprintf("Uso: savecom1 [archivo.txt]\n");
        return 2;
    }
    size = vga_com1_log_size();
    dropped = vga_com1_log_dropped();
    if (!vga_com1_log_save(path)) {
        kprintf("savecom1: no se pudo escribir %s\n", path);
        return 1;
    }
    kprintf("savecom1: %u bytes guardados en %s\n", size, path);
    if (dropped)
        kprintf("savecom1: AVISO: el buffer se lleno; %u bytes finales se perdieron\n",
                dropped);
    return 0;
}

static int cmd_exit(int argc UNUSED, char **argv UNUSED) {
    exit_requested = true;
    return 0;
}

static bool shell_parse_ipv4(const char *text, uint8_t address[4]) {
    uint32_t value = 0;
    uint8_t part = 0;
    bool has_digit = false;
    if (!text || !address) return false;
    for (;;) {
        char c = *text++;
        if (c >= '0' && c <= '9') {
            value = value * 10U + (uint32_t)(c - '0');
            if (value > 255U) return false;
            has_digit = true;
        } else if (c == '.' || c == '\0') {
            if (!has_digit || part >= 4U) return false;
            address[part++] = (uint8_t)value;
            value = 0;
            has_digit = false;
            if (!c) return part == 4U;
        } else return false;
    }
}

static void shell_print_ipv4(const uint8_t address[4]) {
    kprintf("%u.%u.%u.%u", address[0], address[1], address[2], address[3]);
}

static bool shell_parse_port(const char *text, uint16_t *port) {
    uint32_t value = 0;
    if (!text || !*text || !port) return false;
    while (*text) {
        if (*text < '0' || *text > '9') return false;
        value = value * 10U + (uint32_t)(*text++ - '0');
        if (value > 65535U) return false;
    }
    if (!value) return false;
    *port = (uint16_t)value;
    return true;
}

static bool shell_resolve_target(const char *text, uint8_t address[4]) {
    return shell_parse_ipv4(text, address) ||
           network_resolve(text, address, 5000U);
}

static int cmd_ipconfig(int argc UNUSED, char **argv UNUSED) {
    net_info_t info;
    network_get_info(&info);
    kprintf("NIC registrada: %s  enlace: %s  MTU=%u\n",
            info.device[0] ? info.device : "ninguno",
            info.link_up ? "activo" : "inactivo", info.mtu);
    kprintf("Modulos: NETSTACK=%s TLS=%s  IPv4=%s\n",
            info.stack_ready ? "cargado" : "ausente",
            info.tls_ready ? "cargado" : "ausente",
            info.configured ? "configurada" : "@HAFA97F45");
    kprintf("MAC %x:%x:%x:%x:%x:%x  RX=%u TX=%u drop=%u\n",
            info.mac[0], info.mac[1], info.mac[2], info.mac[3], info.mac[4], info.mac[5],
            info.rx_packets, info.tx_packets, info.rx_dropped);
    kprintf("IPv4 "); shell_print_ipv4(info.address);
    kprintf("  mascara "); shell_print_ipv4(info.netmask);
    kprintf("\ngateway "); shell_print_ipv4(info.gateway);
    kprintf("  DNS "); shell_print_ipv4(info.dns); kprintf("\n");
    return info.device[0] ? 0 : 1;
}

static int cmd_ping(int argc, char **argv) {
    uint8_t address[4] = {0, 0, 0, 0};
    uint32_t elapsed;
    if (argc != 2) {
        kprintf("Uso: ping <ipv4|nombre>\n");
        return 1;
    }
    if (!shell_resolve_target(argv[1], address)) {
        kprintf("ping: DNS no pudo resolver '%s'\n", argv[1]);
        return 1;
    }
    if (!network_ping(address, 3000U, &elapsed)) {
        kprintf("Sin respuesta de %s\n", argv[1]);
        return 1;
    }
    kprintf("@HA8B294EC", argv[1]); shell_print_ipv4(address);
    kprintf("): tiempo=%u ms\n", elapsed);
    return 0;
}

static int cmd_dns(int argc, char **argv) {
    uint8_t address[4];
    if (argc != 2) { kprintf("Uso: dns <nombre>\n"); return 1; }
    if (!network_resolve(argv[1], address, 5000U)) {
        kprintf("DNS: no se pudo resolver %s\n", argv[1]);
        return 1;
    }
    kprintf("%s = ", argv[1]); shell_print_ipv4(address); kprintf("\n");
    return 0;
}

static uint32_t http_body_offset(const uint8_t *response, uint32_t length) {
    for (uint32_t i = 0; i + 3U < length; i++)
        if (response[i] == '\r' && response[i + 1U] == '\n' &&
            response[i + 2U] == '\r' && response[i + 3U] == '\n')
            return i + 4U;
    return 0;
}

static int32_t shell_fetch(const char *url, void *buffer, uint32_t capacity,
                           uint32_t timeout_ms) {
    if (bk_runtime_strncmp(url, "https://", 8U) == 0)
        return network_https_get(url, buffer, capacity, timeout_ms);
    return network_http_get(url, buffer, capacity, timeout_ms);
}

static void shell_print_http_headers(const uint8_t *response,
                                     uint32_t length) {
    uint32_t end = http_body_offset(response, length);
    if (!end || end > length) end = length;
    for (uint32_t i = 0; i < end; i++) vga_putchar((char)response[i]);
    if (!end || response[end - 1U] != '\n') vga_putchar('\n');
}

static int cmd_tcp(int argc, char **argv) {
    uint8_t address[4];
    uint16_t port;
    int socket;
    if (argc != 3 || !shell_parse_port(argv[2], &port) ||
        !shell_resolve_target(argv[1], address)) {
        kprintf("Uso: tcp <ipv4|nombre> <puerto>\n");
        return 1;
    }
    socket = network_socket_open(NET_SOCKET_TCP);
    if (socket < 0) { kprintf("TCP: no hay socket disponible\n"); return 1; }
    if (!network_socket_connect(socket, address, port, 7000U)) {
        kprintf("TCP: conexion fallida a %s:%u\n", argv[1], port);
        network_socket_close(socket);
        return 1;
    }
    kprintf("@HD51565F1", argv[1]); shell_print_ipv4(address);
    kprintf(") puerto %u mediante la NIC registrada\n", port);
    network_socket_close(socket);
    return 0;
}

static int cmd_httphead(int argc, char **argv) {
    uint8_t *response;
    int32_t received;
    if (argc != 2) { kprintf("Uso: httphead <http[s]://url>\n"); return 1; }
    response = (uint8_t *)bk_sys_alloc(16384U);
    if (!response) return 1;
    received = shell_fetch(argv[1], response, 16384U, 15000U);
    if (received <= 0) {
        kprintf("httphead: fallo (TLS=%d)\n", network_tls_last_error());
        bk_sys_free(response);
        return 1;
    }
    shell_print_http_headers(response, (uint32_t)received);
    kprintf("[OK] recibidos %d bytes\n", received);
    bk_sys_free(response);
    return 0;
}

static int cmd_nettest(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "example.com";
    net_info_t before, after;
    uint8_t address[4] = {0, 0, 0, 0};
    uint8_t *response;
    uint32_t elapsed = 0;
    int socket;
    int32_t http_bytes, https_bytes;
    bool ok = true, resolved = false;
    char http_url[192] = "http://";
    char https_url[192] = "https://";
    if (argc > 2) { kprintf("Uso: nettest [nombre]\n"); return 1; }
    network_get_info(&before);
    kprintf("=== Diagnostico de red ===\n");
    kprintf("@H1957E064", before.device[0] ? before.device : "AUSENTE");
    if (before.device[0] && before.link_up) kprintf(" [OK]\n");
    else { kprintf(" [FALLO: no registrada o sin enlace]\n"); ok = false; }
    kprintf("    Esta NIC, no solo el PCI, expone RX=%u TX=%u drop=%u\n",
            before.rx_packets, before.tx_packets, before.rx_dropped);
    kprintf("[2] NETSTACK: %s  TLS: %s\n",
            before.stack_ready ? "OK" : "FALLO",
            before.tls_ready ? "OK" : "FALLO");
    if (!before.stack_ready) ok = false;
    if (!before.configured) {
        kprintf("[3] DHCP: solicitando...\n");
        if (!network_dhcp(10000U)) { kprintf("    [FALLO]\n"); ok = false; }
    }
    network_get_info(&after);
    kprintf("[3] IPv4: "); shell_print_ipv4(after.address);
    kprintf(" gateway="); shell_print_ipv4(after.gateway);
    kprintf(" DNS="); shell_print_ipv4(after.dns);
    kprintf(after.configured ? " [OK]\n" : " [FALLO]\n");
    if (!after.configured) ok = false;
    if (after.configured && network_ping(after.gateway, 3000U, &elapsed))
        kprintf("[4] ICMP gateway: %u ms [OK]\n", elapsed);
    else { kprintf("[4] ICMP gateway: [FALLO]\n"); ok = false; }
    if (network_resolve(host, address, 5000U)) {
        resolved = true;
        kprintf("[5] DNS %s = ", host); shell_print_ipv4(address);
        kprintf(" [OK]\n");
    } else { kprintf("[5] DNS %s: [FALLO]\n", host); ok = false; }
    socket = network_socket_open(NET_SOCKET_TCP);
    if (resolved && socket >= 0 &&
        network_socket_connect(socket, address, 80U, 7000U))
        kprintf("[6] TCP %s:80: [OK]\n", host);
    else { kprintf("[6] TCP %s:80: [FALLO]\n", host); ok = false; }
    if (socket >= 0) network_socket_close(socket);
    response = (uint8_t *)bk_sys_alloc(16384U);
    if (!append_text(http_url, sizeof(http_url), host) ||
        !append_text(http_url, sizeof(http_url), "/") ||
        !append_text(https_url, sizeof(https_url), host) ||
        !append_text(https_url, sizeof(https_url), "/")) {
        if (response) bk_sys_free(response);
        return 1;
    }
    http_bytes = response ? shell_fetch(http_url, response, 16384U, 10000U) : -1;
    kprintf("[7] HTTP: %d bytes %s\n", http_bytes,
            http_bytes > 0 ? "[OK]" : "[FALLO]");
    if (http_bytes <= 0) ok = false;
    https_bytes = response ? shell_fetch(https_url, response, 16384U, 15000U) : -1;
    kprintf("[8] HTTPS/TLS: %d bytes, codigo TLS=%d %s\n",
            https_bytes, network_tls_last_error(),
            https_bytes > 0 ? "[OK]" : "[FALLO]");
    if (https_bytes <= 0) ok = false;
    if (response) bk_sys_free(response);
    network_get_info(&after);
    kprintf("[9] Trafico real por %s: RX +%u, TX +%u %s\n",
            after.device[0] ? after.device : "@H065039C5",
            after.rx_packets - before.rx_packets,
            after.tx_packets - before.tx_packets,
            after.rx_packets > before.rx_packets && after.tx_packets > before.tx_packets
                ? "[OK]" : "[FALLO]");
    if (after.rx_packets <= before.rx_packets ||
        after.tx_packets <= before.tx_packets) ok = false;
    kprintf("=== Resultado: %s ===\n", ok ? "@HE380A975" : "@HE72BF574");
    return ok ? 0 : 1;
}

static int cmd_http_get(int argc, char **argv) {
    uint8_t *response;
    int32_t received;
    uint32_t offset;
    bool save = argc > 0 && bk_runtime_strcmp(argv[0], "wget") == 0;
    if (argc < 2 || argc > 3 || (save && argc != 3) || (!save && argc != 2)) {
        kprintf(save ? "Uso: wget <http://url> [archivo]\n"
                     : "Uso: curl <http://url>\n");
        return 1;
    }
    response = (uint8_t *)bk_sys_alloc(65536U);
    if (!response) { kprintf("HTTP: memoria insuficiente\n"); return 1; }
    received = shell_fetch(argv[1], response, 65536U, 15000U);
    if (received <= 0) {
        kprintf("HTTP/TLS: fallo la descarga de %s (TLS=%d)\n",
                argv[1], network_tls_last_error());
        bk_sys_free(response);
        return 1;
    }
    offset = http_body_offset(response, (uint32_t)received);
    if (save) {
        if (!bk_file_write_all(argv[2], response + offset,
                               (uint32_t)received - offset)) {
            kprintf("wget: no se pudo escribir %s\n", argv[2]);
            bk_sys_free(response);
            return 1;
        }
        kprintf("Guardados %u bytes en %s\n",
                (uint32_t)received - offset, argv[2]);
    } else {
        for (uint32_t i = offset; i < (uint32_t)received; i++)
            vga_putchar((char)response[i]);
        if (!received || response[received - 1] != '\n') vga_putchar('\n');
    }
    bk_sys_free(response);
    return 0;
}

static int cmd_net(int argc, char **argv) {
    if (argc < 2 || bk_runtime_strcmp(argv[1], "info") == 0)
        return cmd_ipconfig(0, NULL);
    if (bk_runtime_strcmp(argv[1], "dhcp") == 0) {
        kprintf("Solicitando configuracion DHCP...\n");
        if (!network_dhcp(10000U)) {
            kprintf("DHCP fallo o no hay driver/pila de red.\n");
            return 1;
        }
        return cmd_ipconfig(0, NULL);
    }
    if (bk_runtime_strcmp(argv[1], "test") == 0)
        return cmd_nettest(argc - 1, argv + 1);
    if (bk_runtime_strcmp(argv[1], "static") == 0) {
        uint8_t ip[4], mask[4], gateway[4], dns[4];
        if (argc < 6 || !shell_parse_ipv4(argv[2], ip) ||
            !shell_parse_ipv4(argv[3], mask) ||
            !shell_parse_ipv4(argv[4], gateway) ||
            !shell_parse_ipv4(argv[5], dns)) {
            kprintf("Uso: net static <ip> <mascara> <gateway> <dns>\n");
            return 1;
        }
        return network_configure(ip, mask, gateway, dns) ? 0 : 1;
    }
    kprintf("Uso: net [info|dhcp|test [nombre]|static <ip> <mascara> <gateway> <dns>]\n");
    return 1;
}

bool shell_take_exit_request(void) {
    bool requested = exit_requested;
    exit_requested = false;
    return requested;
}

static const shell_cmd_t builtins[] = {
    {"cd", "@H8EBC4DC7", cmd_cd},
    {"pwd", "@H4294EF07", cmd_pwd},
    {"exit", "@H5AA123D8", cmd_exit},
    {"clear", "@H2E1891D8", cmd_clear},
    {"history", "@HA7C2CA78", cmd_history},
    {"alias", "@HD9155830", cmd_alias},
    {"unalias", "@HBD387DAB", cmd_unalias},
    {"set", "@HAE9CD123", cmd_set},
    {"echo", "@H4F69F172", cmd_echo},
    {"ver", "@HB5268117", cmd_ver},
    {"savecom1", "Guarda el log de arranque/COM1 en un archivo TXT", cmd_savecom1},
    {"perfmon", "Controla el perfilador de rendimiento por COM1", cmd_perfmon},
    {"benchmark", "Prueba CPU, RAM, video, disco y scheduler", cmd_benchmark},
    {"bench", "Alias de benchmark", cmd_benchmark},
    {"ifconfig", "@HC2EAD0ED", cmd_ipconfig},
    {"ipconfig", "@HC2EAD0ED", cmd_ipconfig},
    {"ping", "@H0E301E9F", cmd_ping},
    {"dns", "@H9E8B08E6", cmd_dns},
    {"tcp", "@H69AA9098", cmd_tcp},
    {"httphead", "@HD9794107", cmd_httphead},
    {"nettest", "@H96534E9C", cmd_nettest},
    {"curl", "@HA1435780", cmd_http_get},
    {"wget", "@HA378DE6A", cmd_http_get},
    {"net", "@H45500667", cmd_net},
    {NULL, NULL, NULL}
};

static bool append_argument(char *output, uint32_t capacity,
                            const char *argument) {
    bool quote = false;
    const char *p = argument ? argument : "";
    for (; *p; p++)
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '"') quote = true;
    if (!append_text(output, capacity, " ")) return false;
    if (!quote) return append_text(output, capacity, argument ? argument : "");
    if (!append_text(output, capacity, "\"")) return false;
    for (p = argument ? argument : ""; *p; p++) {
        char part[3] = {0, 0, 0};
        if (*p == '"' || *p == '\\') part[0] = '\\', part[1] = *p;
        else part[0] = *p;
        if (!append_text(output, capacity, part)) return false;
    }
    return append_text(output, capacity, "\"");
}

static bool launch_external(const char *name, int argc, char **argv) {
    char path[VFS_MAX_PATH] = "/SYSTEM/COMMANDS/";
    char argument[SHELL_MAX_CMD] = "";
    append_text(path, sizeof(path), name);
    append_text(path, sizeof(path), ".BEX");
    append_text(argument, sizeof(argument), name);
    if (!append_argument(argument, sizeof(argument), "--cwd") ||
        !append_argument(argument, sizeof(argument), bk_file_getcwd()))
        return false;
    for (int i = 1; i < argc; i++)
        if (!append_argument(argument, sizeof(argument), argv[i])) return false;
    return bk_app_execute_path_arg(bk_gui_get_desktop(), path, argument);
}

static void execute(char *line) {
    char original[SHELL_MAX_CMD];
    char expanded[SHELL_MAX_CMD];
    char aliased[SHELL_MAX_CMD];
    char *argv[SHELL_MAX_ARGS + 1];
    int argc;

    copy_text(original, sizeof(original), line);
    history_add(original);
    expand_variables(original, expanded, sizeof(expanded));
    copy_text(aliased, sizeof(aliased), expanded);
    argc = parse_args(aliased, argv, SHELL_MAX_ARGS);
    if (!argc) return;

    int alias = pair_find(aliases, SHELL_ALIAS_MAX, argv[0]);
    if (alias >= 0) {
        char merged[SHELL_MAX_CMD];
        copy_text(merged, sizeof(merged), aliases[alias].value);
        for (int i = 1; i < argc; i++) {
            append_text(merged, sizeof(merged), " ");
            append_text(merged, sizeof(merged), argv[i]);
        }
        copy_text(aliased, sizeof(aliased), merged);
        argc = parse_args(aliased, argv, SHELL_MAX_ARGS);
        if (!argc) return;
    }

    for (int i = 0; builtins[i].name; i++) {
        if (bk_runtime_strcmp(argv[0], builtins[i].name) == 0) {
            builtins[i].func(argc, argv);
            return;
        }
    }
    if (!launch_external(argv[0], argc, argv))
        kprintf("%s: comando no encontrado en /SYSTEM/COMMANDS\n", argv[0]);
}

void shell_execute_line(const char *line) {
    char command[SHELL_MAX_CMD];
    if (!line) return;
    copy_text(command, sizeof(command), line);
    execute(command);
}

void shell_run(void) {
    char line[SHELL_MAX_CMD];
    vga_clear();
    kprintf("BlesKernOS Terminal 1.1 - escriba 'help' para obtener ayuda\n");
    while (!exit_requested) {
        kprintf("bles@bleskernos:%s> ", bk_file_getcwd());
        readline(line, sizeof(line));
        execute(line);
    }
    exit_requested = false;
}
