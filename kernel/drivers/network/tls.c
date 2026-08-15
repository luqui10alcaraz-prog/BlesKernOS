#include "../../include/driver.h"
#include "../../include/network.h"
#include "../../include/memory.h"
#include "../../include/rtc.h"
#include "../../include/pit.h"
#include "../../include/vga.h"
#include "../../string.h"
#include "bearssl.h"

/* Bundle Mozilla generado con `brssl ta` desde curl.se/ca/cacert.pem. */
#include "tls_trust_anchors.c"

#define TLS_ERROR_BUSY       1000
#define TLS_ERROR_URL        1001
#define TLS_ERROR_DNS        1002
#define TLS_ERROR_CONNECT    1003
#define TLS_ERROR_CLOCK      1004
#define TLS_ERROR_MEMORY     1005
#define TLS_ERROR_IO         1006
#define TLS_ERROR_ENTROPY    1007

typedef struct {
    br_ssl_client_context client;
    br_x509_minimal_context x509;
    br_sslio_context io;
    unsigned char buffer[BR_SSL_BUFSIZE_BIDI];
} tls_context_t;

typedef struct {
    int socket;
    uint32_t timeout_ms;
} tls_transport_t;

typedef struct {
    tls_context_t *tls;
    tls_transport_t transport;
    int socket;
    char host[128];
    uint16_t port;
    bool active;
    volatile uint32_t busy;
} tls_connection_t;

static int g_last_error;
#define TLS_CONNECTION_MAX 4U
static tls_connection_t g_connections[TLS_CONNECTION_MAX] = {
    { .socket = -1 }, { .socket = -1 },
    { .socket = -1 }, { .socket = -1 }
};
static volatile uint32_t g_dns_busy;

static void tls_spin_lock(volatile uint32_t *lock) {
    while (!__sync_bool_compare_and_swap(lock, 0U, 1U))
        __asm__ volatile ("pause");
}

static void tls_spin_unlock(volatile uint32_t *lock) {
    __sync_lock_release(lock);
}

static bool tls_entropy(uint8_t output[32]) {
    uint32_t eax = 1U, ebx, ecx, edx;
    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (ecx & (1U << 30)) {
        bool complete = true;
        for (uint32_t i = 0; i < 8U; i++) {
            uint32_t value = 0;
            bool ready = false;
            for (uint32_t attempt = 0; attempt < 10U; attempt++) {
                uint8_t carry;
                __asm__ volatile ("rdrand %0; setc %1"
                                  : "=r"(value), "=qm"(carry));
                if (carry) { ready = true; break; }
            }
            if (!ready) { complete = false; break; }
            kmemcpy(output + i * 4U, &value, 4U);
        }
        if (complete) return true;
    }

    /* CPUs i686 y algunas configuraciones de QEMU no anuncian RDRAND. Mezclar
     * TSC, PIT, RTC y variación temporal evita que HTTPS quede inutilizable.
     * No reemplaza un CSPRNG de hardware, pero sí aporta una semilla distinta
     * por arranque y por conexión para el PRNG HMAC_DRBG de BearSSL. */
    {
        rtc_datetime_t now;
        uint32_t state = pit_get_ticks() ^ (uint32_t)(uintptr_t)output ^
                         (uint32_t)(uintptr_t)&state ^ 0xB1E57A5DU;
        if (rtc_get_datetime(&now)) {
            state ^= now.date.year * 366U + now.date.month * 31U + now.date.day;
            state ^= ((uint32_t)now.time.hour << 24) |
                     ((uint32_t)now.time.minute << 16) |
                     ((uint32_t)now.time.second << 8);
        }
        for (uint32_t i = 0U; i < 8U; i++) {
            uint32_t lo, hi;
            __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
            state ^= lo ^ (hi << (i & 15U)) ^ pit_get_ticks();
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            kmemcpy(output + i * 4U, &state, 4U);
            for (volatile uint32_t spin = 0U; spin < 97U + i * 13U; spin++)
                __asm__ volatile ("pause");
        }
    }
    return true;
}

static bool leap_year(uint32_t year) {
    return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

static bool tls_set_clock(br_x509_minimal_context *x509) {
    static const uint16_t month_days[12] =
        { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    rtc_datetime_t now;
    uint32_t days = 0;
    if (!rtc_get_datetime(&now) || now.date.year < 2000U ||
        now.date.month < 1U || now.date.month > 12U ||
        now.date.day < 1U || now.date.day > 31U) return false;
    for (uint32_t year = 0; year < now.date.year; year++)
        days += leap_year(year) ? 366U : 365U;
    for (uint32_t month = 1; month < now.date.month; month++) {
        days += month_days[month - 1U];
        if (month == 2U && leap_year(now.date.year)) days++;
    }
    days += now.date.day - 1U;
    br_x509_minimal_set_time(x509, days,
        (uint32_t)now.time.hour * 3600U +
        (uint32_t)now.time.minute * 60U + now.time.second);
    return true;
}

static int tls_read(void *context, unsigned char *data, size_t length) {
    tls_transport_t *transport = (tls_transport_t *)context;
    int32_t received = network_socket_receive(transport->socket, data,
                                               (uint32_t)length,
                                               transport->timeout_ms);
    return received > 0 ? (int)received : -1;
}

static int tls_write(void *context, const unsigned char *data, size_t length) {
    tls_transport_t *transport = (tls_transport_t *)context;
    int32_t sent = network_socket_send(transport->socket, data,
                                       (uint32_t)length,
                                       transport->timeout_ms);
    return sent > 0 ? (int)sent : -1;
}

static bool append(char *output, uint32_t capacity, uint32_t *used,
                   const char *text) {
    while (text && *text) {
        if (*used + 1U >= capacity) return false;
        output[(*used)++] = *text++;
    }
    output[*used] = '\0';
    return true;
}

static bool tls_parse_https_endpoint(const char *url, char host[128],
                                     uint16_t *port) {
    const char *cursor;
    uint32_t host_length = 0U;
    uint32_t parsed = 0U;
    if (!url || !host || !port || kstrncmp(url, "https://", 8U) != 0)
        return false;
    cursor = url + 8U;
    while (*cursor && *cursor != '/' && *cursor != ':' && *cursor != '?' &&
           *cursor != '#') {
        if (host_length + 1U >= 128U) return false;
        host[host_length++] = *cursor++;
    }
    host[host_length] = '\0';
    if (!host_length) return false;
    *port = 443U;
    if (*cursor == ':') {
        cursor++;
        if (*cursor < '0' || *cursor > '9') return false;
        while (*cursor >= '0' && *cursor <= '9') {
            parsed = parsed * 10U + (uint32_t)(*cursor++ - '0');
            if (parsed > 65535U) return false;
        }
        *port = (uint16_t)parsed;
    }
    return true;
}

static char ascii_lower(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

static bool range_equal_nocase(const uint8_t *data, uint32_t length,
                               const char *text) {
    uint32_t i = 0U;
    while (text[i]) {
        if (i >= length || ascii_lower((char)data[i]) != ascii_lower(text[i]))
            return false;
        i++;
    }
    return i == length;
}

static uint32_t response_header_end(const uint8_t *data, uint32_t length) {
    uint32_t i;
    for (i = 0U; i + 3U < length; i++)
        if (data[i] == '\r' && data[i + 1U] == '\n' &&
            data[i + 2U] == '\r' && data[i + 3U] == '\n') return i + 4U;
    return 0U;
}

static bool response_header(const uint8_t *data, uint32_t header_end,
                            const char *name, const uint8_t **value,
                            uint32_t *value_length) {
    uint32_t at = 0U;
    while (at < header_end && data[at] != '\n') at++;
    if (at < header_end) at++;
    while (at < header_end) {
        uint32_t start = at, end, colon;
        while (at < header_end && data[at] != '\n') at++;
        end = at;
        if (at < header_end) at++;
        while (end > start && (data[end - 1U] == '\r' ||
               data[end - 1U] == ' ' || data[end - 1U] == '\t')) end--;
        if (end == start) break;
        colon = start;
        while (colon < end && data[colon] != ':') colon++;
        if (colon == end || !range_equal_nocase(data + start, colon - start,
                                                name)) continue;
        colon++;
        while (colon < end && (data[colon] == ' ' || data[colon] == '\t'))
            colon++;
        *value = data + colon;
        *value_length = end - colon;
        return true;
    }
    return false;
}

static bool value_contains_nocase(const uint8_t *value, uint32_t length,
                                  const char *needle) {
    uint32_t n = (uint32_t)strlen(needle), i, j;
    if (!n || n > length) return false;
    for (i = 0U; i + n <= length; i++) {
        for (j = 0U; j < n; j++)
            if (ascii_lower((char)value[i + j]) != ascii_lower(needle[j]))
                break;
        if (j == n) return true;
    }
    return false;
}

static bool chunked_response_complete(const uint8_t *data, uint32_t length,
                                      uint32_t body) {
    uint32_t at = body;
    while (at < length) {
        uint32_t chunk = 0U;
        bool digit = false;
        while (at < length && data[at] != '\r' && data[at] != '\n') {
            uint8_t c = data[at++];
            uint32_t value;
            if (c == ';') {
                while (at < length && data[at] != '\r' && data[at] != '\n')
                    at++;
                break;
            }
            if (c >= '0' && c <= '9') value = c - '0';
            else if (c >= 'a' && c <= 'f') value = c - 'a' + 10U;
            else if (c >= 'A' && c <= 'F') value = c - 'A' + 10U;
            else if (c == ' ' || c == '\t') continue;
            else return false;
            if (chunk > 0x0FFFFFFFU) return false;
            chunk = (chunk << 4U) | value;
            digit = true;
        }
        if (!digit || at >= length) return false;
        if (data[at] == '\r') {
            if (at + 1U >= length || data[at + 1U] != '\n') return false;
            at += 2U;
        } else at++;
        if (chunk == 0U) {
            /* El bloque vacío puede terminar inmediatamente o llevar trailers. */
            if (at + 1U < length && data[at] == '\r' &&
                data[at + 1U] == '\n') return true;
            if (at < length && data[at] == '\n') return true;
            for (; at + 3U < length; at++)
                if (data[at] == '\r' && data[at + 1U] == '\n' &&
                    data[at + 2U] == '\r' && data[at + 3U] == '\n') return true;
            return at == length;
        }
        if (chunk > length - at) return false;
        at += chunk;
        if (at + 1U >= length || data[at] != '\r' || data[at + 1U] != '\n')
            return false;
        at += 2U;
    }
    return false;
}

static bool response_complete(const uint8_t *data, uint32_t length,
                              bool *must_close) {
    const uint8_t *value;
    uint32_t value_length, header_end, declared = 0U, i;
    bool have_length = false, no_body = false;
    header_end = response_header_end(data, length);
    if (!header_end) return false;
    *must_close = false;
    /* 204 y 304 nunca tienen cuerpo, aunque no anuncien Content-Length. */
    for (i = 0U; i + 3U < header_end && i < 24U; i++) {
        if (data[i] == ' ' && data[i + 1U] >= '0' && data[i + 1U] <= '9') {
            uint32_t status = (uint32_t)(data[i + 1U] - '0') * 100U +
                              (uint32_t)(data[i + 2U] - '0') * 10U +
                              (uint32_t)(data[i + 3U] - '0');
            if (status == 204U || status == 304U) no_body = true;
            break;
        }
    }
    if (response_header(data, header_end, "Connection", &value,
                        &value_length) &&
        value_contains_nocase(value, value_length, "close")) *must_close = true;
    if (no_body) return true;
    if (response_header(data, header_end, "Transfer-Encoding", &value,
                        &value_length) &&
        value_contains_nocase(value, value_length, "chunked"))
        return chunked_response_complete(data, length, header_end);
    if (response_header(data, header_end, "Content-Length", &value,
                        &value_length)) {
        have_length = true;
        for (i = 0U; i < value_length && value[i] >= '0' && value[i] <= '9'; i++)
            declared = declared * 10U + (uint32_t)(value[i] - '0');
    }
    if (have_length) return length - header_end >= declared;
    /* Sin delimitador el único final válido es el cierre del servidor. */
    *must_close = true;
    return false;
}

static void tls_connection_close(tls_connection_t *connection) {
    uint32_t busy;
    if (!connection) return;
    busy = connection->busy;
    if (connection->tls) kfree(connection->tls);
    if (connection->socket >= 0) network_socket_close(connection->socket);
    kmemset(connection, 0, sizeof(*connection));
    connection->socket = -1;
    connection->busy = busy;
}

static bool tls_connection_open(tls_connection_t *connection,
                                const char *host, uint16_t port,
                                uint32_t timeout_ms) {
    uint8_t address[4];
    uint32_t i;
    tls_context_t *tls;
    tls_spin_lock(&g_dns_busy);
    i = network_resolve(host, address, timeout_ms) ? 1U : 0U;
    tls_spin_unlock(&g_dns_busy);
    if (!i) {
        g_last_error = TLS_ERROR_DNS;
        return false;
    }
    connection->socket = network_socket_open(NET_SOCKET_TCP);
    if (connection->socket < 0 ||
        !network_socket_connect(connection->socket, address, port, timeout_ms)) {
        g_last_error = TLS_ERROR_CONNECT;
        tls_connection_close(connection);
        return false;
    }
    tls = (tls_context_t *)kmalloc(sizeof(*tls));
    if (!tls) { g_last_error = TLS_ERROR_MEMORY; tls_connection_close(connection); return false; }
    connection->tls = tls;
    kmemset(tls, 0, sizeof(*tls));
    br_ssl_client_init_full(&tls->client, &tls->x509, TAs, TAs_NUM);
    {
        uint8_t entropy[32];
        if (!tls_entropy(entropy)) {
            g_last_error = TLS_ERROR_ENTROPY;
            tls_connection_close(connection);
            return false;
        }
        br_ssl_engine_inject_entropy(&tls->client.eng, entropy, sizeof(entropy));
        kmemset(entropy, 0, sizeof(entropy));
    }
    if (!tls_set_clock(&tls->x509)) {
        g_last_error = TLS_ERROR_CLOCK;
        tls_connection_close(connection);
        return false;
    }
    br_ssl_engine_set_buffer(&tls->client.eng, tls->buffer,
                             sizeof(tls->buffer), 1);
    if (!br_ssl_client_reset(&tls->client, host, 0)) {
        g_last_error = br_ssl_engine_last_error(&tls->client.eng);
        tls_connection_close(connection);
        return false;
    }
    connection->transport.socket = connection->socket;
    connection->transport.timeout_ms = timeout_ms;
    br_sslio_init(&tls->io, &tls->client.eng, tls_read, &connection->transport,
                  tls_write, &connection->transport);
    for (i = 0U; host[i] && i + 1U < sizeof(connection->host); i++)
        connection->host[i] = host[i];
    connection->host[i] = '\0';
    connection->port = port;
    connection->active = true;
    return true;
}

static tls_connection_t *tls_connection_acquire(const char *host,
                                                uint16_t port) {
    uint32_t pass, i;
    /* Primero preferimos una sesión keep-alive del mismo origen. */
    for (pass = 0U; pass < 2U; pass++) {
        for (i = 0U; i < TLS_CONNECTION_MAX; i++) {
            tls_connection_t *connection = &g_connections[i];
            bool match = connection->active && connection->port == port &&
                         strcmp(connection->host, host) == 0;
            if ((pass == 0U && !match) || (pass == 1U && match)) continue;
            if (__sync_bool_compare_and_swap(&connection->busy, 0U, 1U))
                return connection;
        }
    }
    return NULL;
}

static int32_t tls_https_exchange(const char *url, const void *request,
                                  uint32_t request_length, void *data,
                                  uint32_t capacity, uint32_t timeout_ms) {
    char host[128];
    uint16_t port;
    int32_t result = -1;
    uint32_t total = 0U;
    bool reused = false, must_close = false, complete = false;
    uint8_t attempt;
    tls_connection_t *connection = NULL;

    g_last_error = 0;
    if (!request || !request_length || !data || !capacity ||
        !tls_parse_https_endpoint(url, host, &port)) {
        g_last_error = TLS_ERROR_URL;
        return -1;
    }
    connection = tls_connection_acquire(host, port);
    if (!connection) { g_last_error = TLS_ERROR_BUSY; return -1; }
    for (attempt = 0U; attempt < 2U; attempt++) {
        if (connection->active &&
            (strcmp(connection->host, host) != 0 || connection->port != port))
            tls_connection_close(connection);
        reused = connection->active;
        if (!connection->active &&
            !tls_connection_open(connection, host, port, timeout_ms)) goto done;
        connection->transport.timeout_ms = timeout_ms;
        if (br_sslio_write_all(&connection->tls->io, request,
                               request_length) < 0 ||
            br_sslio_flush(&connection->tls->io) < 0) {
            g_last_error = br_ssl_engine_last_error(&connection->tls->client.eng);
            if (!g_last_error) g_last_error = TLS_ERROR_IO;
            tls_connection_close(connection);
            if (reused && attempt == 0U) continue;
            goto done;
        }
        total = 0U;
        must_close = false;
        complete = false;
        while (total < capacity) {
            int received = br_sslio_read(&connection->tls->io,
                                         (uint8_t *)data + total,
                                         capacity - total);
            if (received <= 0) {
                int engine_error = br_ssl_engine_last_error(
                    &connection->tls->client.eng);
                /* Una respuesta sin Content-Length termina por cierre. Si sí
                 * tenía delimitador y faltan bytes, es un timeout parcial y
                 * no debe entregarse al parser como una respuesta completa. */
                if (total && must_close &&
                    response_header_end((const uint8_t *)data, total))
                    complete = true;
                if (!complete && engine_error) g_last_error = engine_error;
                kprintf("[TLS] lectura terminada bytes=%u completa=%u "
                        "engine=%d close=%u\n", total, complete ? 1U : 0U,
                        engine_error, must_close ? 1U : 0U);
                tls_connection_close(connection);
                break;
            }
            total += (uint32_t)received;
            complete = response_complete((const uint8_t *)data, total,
                                         &must_close);
            if (complete)
                break;
        }
        if (total == capacity) complete = true; /* truncado, pero parseable */
        if ((!total || !complete) && attempt == 0U) continue;
        if (!complete) {
            if (!g_last_error) g_last_error = TLS_ERROR_IO;
            total = 0U;
        }
        if (must_close) tls_connection_close(connection);
        break;
    }
    if (!total) {
        if (!g_last_error) g_last_error = TLS_ERROR_IO;
        goto done;
    }
    result = (int32_t)total;

done:
    if (connection) tls_spin_unlock(&connection->busy);
    return result;
}

static int32_t tls_https_get(const char *url, void *data, uint32_t capacity,
                             uint32_t timeout_ms) {
    char host[128], path[256], request[640];
    const char *cursor;
    uint32_t path_length = 0U, request_length = 0U;
    uint16_t ignored_port = 443U;
    if (!url || !tls_parse_https_endpoint(url, host, &ignored_port)) {
        g_last_error = TLS_ERROR_URL;
        return -1;
    }
    cursor = url + 8U;
    while (*cursor && *cursor != '/' && *cursor != ':' && *cursor != '?' &&
           *cursor != '#') cursor++;
    if (*cursor == ':') {
        cursor++;
        while (*cursor >= '0' && *cursor <= '9') cursor++;
    }
    path[path_length++] = '/';
    if (*cursor == '/') path_length = 0U;
    if (*cursor == '/' || *cursor == '?') {
        if (*cursor == '?') path[path_length++] = '/';
        while (*cursor && *cursor != '#' && path_length + 1U < sizeof(path))
            path[path_length++] = *cursor++;
        if (*cursor && *cursor != '#') {
            g_last_error = TLS_ERROR_URL;
            return -1;
        }
    }
    path[path_length] = '\0';
    request[0] = '\0';
    if (!append(request, sizeof(request), &request_length, "GET ") ||
        !append(request, sizeof(request), &request_length, path) ||
        !append(request, sizeof(request), &request_length,
                " HTTP/1.1\r\nHost: ") ||
        !append(request, sizeof(request), &request_length, host) ||
        !append(request, sizeof(request), &request_length,
                "\r\nUser-Agent: NetSurf/3.11 (BlesKernOS 0.8)\r\n"
                "Accept: text/html,text/css,image/png,image/jpeg,image/gif,image/bmp,*/*;q=0.2\r\n"
                "Accept-Encoding: identity\r\nConnection: close\r\n\r\n")) {
        g_last_error = TLS_ERROR_URL;
        return -1;
    }
    return tls_https_exchange(url, request, request_length, data, capacity,
                              timeout_ms);
}

static int tls_last_error(void) { return g_last_error; }
static const net_tls_ops_t g_tls_ops = {
    tls_https_get, tls_https_exchange, tls_last_error
};

static bool tls_init(void) {
    if (!network_register_tls(&g_tls_ops)) return false;
    kprintf("  [TLS] BearSSL TLS 1.2 + validacion X.509 instalado (%u CA)\n",
            (uint32_t)TAs_NUM);
    return true;
}

static void tls_shutdown(void) {
    uint32_t i;
    for (i = 0U; i < TLS_CONNECTION_MAX; i++)
        tls_connection_close(&g_connections[i]);
    network_unregister_tls(&g_tls_ops);
}

const bk_driver_module_t *bleskernos_driver_query(void) {
    static const bk_driver_module_t module = {
        BK_DRIVER_ABI_VERSION, sizeof(bk_driver_module_t),
        "tls-bearssl", "TLS 1.2 con BearSSL y CA de Mozilla",
        tls_init, tls_shutdown
    };
    return &module;
}
