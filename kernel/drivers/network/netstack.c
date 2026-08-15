/* NETSURF_FORCE_ACCEPT_ENCODING_IDENTITY
 * No anunciar codificaciones que este port aún no decodifica.
 */
#include "../../include/types.h"
#include "../../include/memory.h"
#include "../../include/network.h"
#include "../../include/driver.h"
#include "../../include/task.h"
#include "../../include/pit.h"
#include "../../include/vga.h"

#define ETH_ARP  0x0806U
#define ETH_IPV4 0x0800U
#define IP_ICMP  1U
#define IP_UDP   17U
#define IP_TCP   6U
#define DHCP_CLIENT_PORT 68U
#define DHCP_SERVER_PORT 67U
#define DHCP_MAGIC 0x63825363U

typedef struct { uint8_t dst[6], src[6]; uint16_t type; } PACKED eth_header_t;
typedef struct {
    uint16_t htype, ptype; uint8_t hlen, plen; uint16_t operation;
    uint8_t sha[6], spa[4], tha[6], tpa[4];
} PACKED arp_packet_t;
typedef struct {
    uint8_t version_ihl, tos; uint16_t length, identification, fragment;
    uint8_t ttl, protocol; uint16_t checksum; uint8_t source[4], destination[4];
} PACKED ipv4_header_t;
typedef struct { uint16_t source, destination, length, checksum; } PACKED udp_header_t;
typedef struct {
    uint16_t source, destination;
    uint32_t sequence, acknowledgment;
    uint8_t data_offset, flags;
    uint16_t window, checksum, urgent;
} PACKED tcp_header_t;
typedef struct {
    uint16_t id, flags, questions, answers, authority, additional;
} PACKED dns_header_t;
typedef struct {
    uint8_t op, htype, hlen, hops; uint32_t xid; uint16_t secs, flags;
    uint8_t ciaddr[4], yiaddr[4], siaddr[4], giaddr[4];
    uint8_t chaddr[16], sname[64], file[128]; uint32_t magic;
    uint8_t options[312];
} PACKED dhcp_packet_t;

static uint8_t g_ip[4], g_mask[4], g_gateway[4], g_dns[4];
static uint8_t g_mac[6];
static uint8_t g_arp_ip[4], g_arp_mac[6];
static bool g_configured, g_arp_valid;
static volatile uint8_t g_dhcp_type;
static uint8_t g_dhcp_offer[4], g_dhcp_server[4];
static uint8_t g_dhcp_mask[4], g_dhcp_gateway[4], g_dhcp_dns[4];
static uint32_t g_dhcp_xid;
static volatile bool g_ping_reply;
static uint16_t g_ping_id, g_ping_sequence;
static uint8_t g_ping_target[4];
static uint16_t g_ip_identification;
static volatile bool g_dns_ready;
static volatile bool g_dns_done;
static uint16_t g_dns_id;
static uint16_t g_dns_port;
static uint8_t g_dns_answer[4];
static char g_dns_cname[256];

#define DNS_CACHE_MAX 8U
typedef struct {
    char hostname[128];
    uint8_t address[4];
    uint32_t last_used;
    bool used;
} dns_cache_entry_t;
static dns_cache_entry_t g_dns_cache[DNS_CACHE_MAX];
static uint32_t g_dns_cache_clock;
static volatile uint32_t g_dns_lock;
static volatile uint32_t g_socket_lock;

static uint32_t net_irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void net_irq_restore(uint32_t flags) {
    if (flags & (1U << 9)) __asm__ volatile ("sti" : : : "memory");
}

static void net_spin_lock(volatile uint32_t *lock) {
    while (!__sync_bool_compare_and_swap(lock, 0U, 1U))
        __asm__ volatile ("pause");
}

static void net_spin_unlock(volatile uint32_t *lock) {
    __sync_lock_release(lock);
}

typedef enum {
    TCP_CLOSED = 0, TCP_ALLOCATED, TCP_SYN_SENT, TCP_ESTABLISHED,
    TCP_CLOSE_WAIT, TCP_FIN_WAIT, TCP_LISTEN, TCP_SYN_RECEIVED
} tcp_state_t;

#define TCP_RECEIVE_SIZE 8192U
#define TCP_MSS 1200U
#define TCP_FLAG_FIN 0x01U
#define TCP_FLAG_SYN 0x02U
#define TCP_FLAG_RST 0x04U
#define TCP_FLAG_PSH 0x08U
#define TCP_FLAG_ACK 0x10U

typedef struct {
    tcp_state_t state;
    uint16_t local_port, remote_port;
    uint8_t remote_ip[4];
    uint32_t send_next, send_unacknowledged, receive_next;
    uint8_t receive[TCP_RECEIVE_SIZE];
    volatile uint32_t receive_length;
    volatile bool reset;
    int8_t listener;
    int8_t pending;
    int8_t peer;
} tcp_socket_t;

#define UDP_HANDLE_BASE 0x100
#define UDP_RECEIVE_SIZE 1472U
typedef struct {
    bool used, connected;
    uint16_t local_port, remote_port, receive_port;
    uint8_t remote_ip[4], receive_ip[4];
    uint8_t receive[UDP_RECEIVE_SIZE];
    volatile uint16_t receive_length;
} udp_socket_t;

static tcp_socket_t g_sockets[NET_SOCKET_MAX];
static udp_socket_t g_udp_sockets[NET_SOCKET_MAX];
static uint16_t g_next_ephemeral = 49152U;

static bool refresh_device_mac(void) {
    net_info_t info;
    if (!netdev_present()) return false;
    netdev_get_info(&info);
    kmemcpy(g_mac, info.mac, 6);
    return (g_mac[0] | g_mac[1] | g_mac[2] | g_mac[3] | g_mac[4] | g_mac[5]) != 0U;
}

static uint16_t be16(uint16_t value) { return (uint16_t)((value << 8) | (value >> 8)); }
static uint32_t be32(uint32_t value) {
    return ((value & 0x000000FFU) << 24) | ((value & 0x0000FF00U) << 8) |
           ((value & 0x00FF0000U) >> 8) | ((value & 0xFF000000U) >> 24);
}
static bool bytes_equal(const uint8_t *a, const uint8_t *b, uint32_t n) {
    return kmemcmp(a, b, n) == 0;
}
static bool ip_zero(const uint8_t ip[4]) { return !(ip[0] | ip[1] | ip[2] | ip[3]); }
static bool ip_broadcast(const uint8_t ip[4]) {
    return ip[0] == 255U && ip[1] == 255U && ip[2] == 255U && ip[3] == 255U;
}
static uint16_t checksum(const void *data, uint32_t length) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    while (length > 1U) { sum += ((uint16_t)p[0] << 8) | p[1]; p += 2; length -= 2; }
    if (length) sum += (uint16_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)~sum;
}
static uint32_t timeout_ticks(uint32_t milliseconds) {
    uint32_t hz = pit_get_frequency_hz();
    uint32_t ticks = (milliseconds * hz + 999U) / 1000U;
    return ticks ? ticks : 1U;
}
static void wait_one_tick(void) {
    /* No depender exclusivamente de la tarea RX del driver: durante una
       syscall larga o con la GUI cargada debemos vaciar la NIC activamente. */
    netdev_poll();
    task_sleep(1U);
    netdev_poll();
}

static bool ethernet_send(const uint8_t destination[6], uint16_t type,
                          const void *payload, uint16_t length) {
    uint8_t frame[NET_FRAME_MAX];
    eth_header_t *eth = (eth_header_t *)frame;
    uint16_t total = (uint16_t)(sizeof(*eth) + length);
    if (total > sizeof(frame)) return false;
    kmemcpy(eth->dst, destination, 6); kmemcpy(eth->src, g_mac, 6);
    eth->type = be16(type);
    kmemcpy(frame + sizeof(*eth), payload, length);
    if (total < 60U) { kmemset(frame + total, 0, 60U - total); total = 60U; }
    return netdev_send(frame, total);
}

static void arp_send(uint16_t operation, const uint8_t target_ip[4],
                     const uint8_t target_mac[6]) {
    static const uint8_t broadcast[6] = {255,255,255,255,255,255};
    arp_packet_t arp;
    kmemset(&arp, 0, sizeof(arp));
    arp.htype = be16(1); arp.ptype = be16(ETH_IPV4); arp.hlen = 6; arp.plen = 4;
    arp.operation = be16(operation); kmemcpy(arp.sha, g_mac, 6);
    kmemcpy(arp.spa, g_ip, 4); kmemcpy(arp.tpa, target_ip, 4);
    if (target_mac) kmemcpy(arp.tha, target_mac, 6);
    ethernet_send(operation == 1U ? broadcast : target_mac, ETH_ARP, &arp, sizeof(arp));
}

static bool arp_resolve(const uint8_t ip[4], uint8_t mac[6], uint32_t milliseconds) {
    uint32_t start, limit;
    if (g_arp_valid && bytes_equal(g_arp_ip, ip, 4)) { kmemcpy(mac, g_arp_mac, 6); return true; }
    g_arp_valid = false;
    arp_send(1, ip, NULL);
    start = pit_get_ticks(); limit = timeout_ticks(milliseconds);
    while ((uint32_t)(pit_get_ticks() - start) < limit) {
        if (g_arp_valid && bytes_equal(g_arp_ip, ip, 4)) { kmemcpy(mac, g_arp_mac, 6); return true; }
        wait_one_tick();
    }
    return false;
}

static bool ipv4_send_raw(const uint8_t destination[4], uint8_t protocol,
                          const void *payload, uint16_t payload_length,
                          bool force_broadcast) {
    uint8_t packet[NET_FRAME_MAX - 14U], destination_mac[6], next_hop[4];
    static const uint8_t broadcast_mac[6] = {255,255,255,255,255,255};
    ipv4_header_t *ip = (ipv4_header_t *)packet;
    uint16_t total = (uint16_t)(sizeof(*ip) + payload_length);
    if (total > sizeof(packet)) return false;
    kmemset(ip, 0, sizeof(*ip)); ip->version_ihl = 0x45; ip->length = be16(total);
    ip->identification = be16(++g_ip_identification); ip->fragment = be16(0x4000);
    ip->ttl = 64; ip->protocol = protocol; kmemcpy(ip->source, g_ip, 4);
    kmemcpy(ip->destination, destination, 4); ip->checksum = be16(checksum(ip, sizeof(*ip)));
    kmemcpy(packet + sizeof(*ip), payload, payload_length);
    if (force_broadcast || ip_broadcast(destination))
        kmemcpy(destination_mac, broadcast_mac, 6);
    else {
        bool local = true;
        for (uint8_t i = 0; i < 4; i++)
            if ((destination[i] & g_mask[i]) != (g_ip[i] & g_mask[i])) local = false;
        kmemcpy(next_hop, local || ip_zero(g_gateway) ? destination : g_gateway, 4);
        if (!arp_resolve(next_hop, destination_mac, 1500U)) return false;
    }
    return ethernet_send(destination_mac, ETH_IPV4, packet, total);
}

static bool udp_send(const uint8_t destination[4], uint16_t source_port,
                     uint16_t destination_port, const void *payload,
                     uint16_t payload_length, bool broadcast) {
    uint8_t packet[NET_FRAME_MAX - 34U];
    udp_header_t *udp = (udp_header_t *)packet;
    uint16_t total = (uint16_t)(sizeof(*udp) + payload_length);
    if (total > sizeof(packet)) return false;
    udp->source = be16(source_port); udp->destination = be16(destination_port);
    udp->length = be16(total); udp->checksum = 0; /* Permitido en UDP/IPv4. */
    kmemcpy(packet + sizeof(*udp), payload, payload_length);
    return ipv4_send_raw(destination, IP_UDP, packet, total, broadcast);
}

static bool dns_skip_name(const uint8_t *message, uint16_t length,
                          uint16_t *offset) {
    uint16_t pos = *offset;
    while (pos < length) {
        uint8_t part = message[pos++];
        if (!part) { *offset = pos; return true; }
        if ((part & 0xC0U) == 0xC0U) {
            if (pos >= length) return false;
            *offset = (uint16_t)(pos + 1U);
            return true;
        }
        if (part > 63U || pos + part > length) return false;
        pos = (uint16_t)(pos + part);
    }
    return false;
}

static bool dns_decode_name(const uint8_t *message, uint16_t length,
                            uint16_t offset, char *output,
                            uint16_t capacity) {
    uint16_t used = 0;
    uint16_t jumps = 0;
    if (!output || capacity < 2U) return false;
    while (offset < length && jumps++ < 32U) {
        uint8_t part = message[offset++];
        if (!part) {
            output[used] = '\0';
            return used != 0U;
        }
        if ((part & 0xC0U) == 0xC0U) {
            uint16_t pointer;
            if (offset >= length) return false;
            pointer = (uint16_t)(((uint16_t)(part & 0x3FU) << 8) |
                                 message[offset]);
            if (pointer >= length) return false;
            offset = pointer;
            continue;
        }
        if (part > 63U || offset + part > length) return false;
        if (used && used + 1U >= capacity) return false;
        if (used) output[used++] = '.';
        if (used + part >= capacity) return false;
        kmemcpy(output + used, message + offset, part);
        used = (uint16_t)(used + part);
        offset = (uint16_t)(offset + part);
    }
    return false;
}

static void dns_receive(const uint8_t *message, uint16_t length) {
    const dns_header_t *header;
    uint16_t offset, questions, answers;
    if (length < sizeof(dns_header_t)) return;
    header = (const dns_header_t *)message;
    if (be16(header->id) != g_dns_id || !(be16(header->flags) & 0x8000U)) return;
    if ((be16(header->flags) & 0x000FU) != 0U) {
        g_dns_done = true;
        return;
    }
    questions = be16(header->questions);
    answers = be16(header->answers);
    offset = sizeof(*header);
    for (uint16_t i = 0; i < questions; i++) {
        if (!dns_skip_name(message, length, &offset) || offset + 4U > length) return;
        offset = (uint16_t)(offset + 4U);
    }
    for (uint16_t i = 0; i < answers; i++) {
        uint16_t type, class_code, data_length;
        if (!dns_skip_name(message, length, &offset) || offset + 10U > length) return;
        type = (uint16_t)(((uint16_t)message[offset] << 8) | message[offset + 1U]);
        class_code = (uint16_t)(((uint16_t)message[offset + 2U] << 8) | message[offset + 3U]);
        data_length = (uint16_t)(((uint16_t)message[offset + 8U] << 8) | message[offset + 9U]);
        offset = (uint16_t)(offset + 10U);
        if (offset + data_length > length) return;
        if (type == 1U && class_code == 1U && data_length == 4U) {
            kmemcpy(g_dns_answer, message + offset, 4);
            g_dns_ready = true;
            g_dns_done = true;
            return;
        }
        if (type == 5U && class_code == 1U && data_length &&
            !g_dns_cname[0])
            (void)dns_decode_name(message, length, offset, g_dns_cname,
                                  sizeof(g_dns_cname));
        offset = (uint16_t)(offset + data_length);
    }
    g_dns_done = true;
}

static bool parse_numeric_ip(const char *text, uint8_t address[4]) {
    uint32_t value = 0;
    uint8_t part = 0;
    bool digit = false;
    if (!text || !address) return false;
    for (;;) {
        char c = *text++;
        if (c >= '0' && c <= '9') {
            value = value * 10U + (uint32_t)(c - '0');
            if (value > 255U) return false;
            digit = true;
        } else if (c == '.' || !c) {
            if (!digit || part >= 4U) return false;
            address[part++] = (uint8_t)value;
            value = 0;
            digit = false;
            if (!c) return part == 4U;
        } else return false;
    }
}

static int dns_query_once(const uint8_t server[4], const char *hostname,
                          uint8_t address[4], uint32_t timeout_ms) {
    uint8_t query[300];
    dns_header_t *header = (dns_header_t *)query;
    uint16_t length = sizeof(*header);
    const char *label, *cursor;
    uint32_t start, limit;
    if (!server || !hostname || !address || !g_configured || ip_zero(server))
        return 0;
    kmemset(query, 0, sizeof(query));
    g_dns_id = (uint16_t)(pit_get_ticks() ^ 0xB1E5U);
    g_dns_port = g_next_ephemeral++;
    header->id = be16(g_dns_id);
    header->flags = be16(0x0100U);
    header->questions = be16(1U);
    label = hostname;
    cursor = hostname;
    while (true) {
        uint32_t label_length;
        while (*cursor && *cursor != '.') cursor++;
        label_length = (uint32_t)(cursor - label);
        if (!label_length || label_length > 63U ||
            length + 1U + label_length + 5U > sizeof(query)) return 0;
        query[length++] = (uint8_t)label_length;
        kmemcpy(query + length, label, label_length);
        length = (uint16_t)(length + label_length);
        if (!*cursor) break;
        label = ++cursor;
    }
    query[length++] = 0;
    query[length++] = 0; query[length++] = 1; /* A */
    query[length++] = 0; query[length++] = 1; /* IN */
    g_dns_ready = false;
    g_dns_done = false;
    g_dns_cname[0] = '\0';
    if (!udp_send(server, g_dns_port, 53U, query, length, false)) return 0;
    start = pit_get_ticks(); limit = timeout_ticks(timeout_ms);
    while ((uint32_t)(pit_get_ticks() - start) < limit && !g_dns_done)
        wait_one_tick();
    if (g_dns_ready) {
        kmemcpy(address, g_dns_answer, 4);
        return 1;
    }
    return g_dns_done && g_dns_cname[0] ? 2 : 0;
}

static bool stack_resolve(const char *hostname, uint8_t address[4],
                          uint32_t timeout_ms) {
    static const uint8_t fallbacks[2][4] = {{1,1,1,1}, {8,8,8,8}};
    char current[256];
    uint32_t per_try;
    bool resolved = false;
    uint32_t oldest = 0U;
    if (!hostname || !address || !g_configured || ip_zero(g_dns)) return false;
    if (parse_numeric_ip(hostname, address)) return true;
    if (kstrlen(hostname) >= sizeof(current)) return false;
    net_spin_lock(&g_dns_lock);
    for (uint32_t i = 0U; i < DNS_CACHE_MAX; i++) {
        if (g_dns_cache[i].used &&
            kstrcmp(g_dns_cache[i].hostname, hostname) == 0) {
            kmemcpy(address, g_dns_cache[i].address, 4U);
            g_dns_cache[i].last_used = ++g_dns_cache_clock;
            net_spin_unlock(&g_dns_lock);
            return true;
        }
        if (!g_dns_cache[i].used ||
            g_dns_cache[i].last_used < g_dns_cache[oldest].last_used)
            oldest = i;
    }
    kstrcpy(current, hostname);
    per_try = timeout_ms / 3U;
    if (per_try < 500U) per_try = 500U;
    for (uint8_t server_index = 0; server_index < 3U; server_index++) {
        const uint8_t *server = server_index == 0U
                              ? g_dns : fallbacks[server_index - 1U];
        for (uint8_t cname_depth = 0; cname_depth < 4U; cname_depth++) {
            int result = dns_query_once(server, current, address, per_try);
            if (result == 1) { resolved = true; goto done; }
            if (result != 2 || kstrlen(g_dns_cname) >= sizeof(current)) break;
            kstrcpy(current, g_dns_cname);
        }
        kstrcpy(current, hostname);
    }
done:
    if (resolved) {
        dns_cache_entry_t *entry = &g_dns_cache[oldest];
        kmemset(entry, 0, sizeof(*entry));
        kstrncpy(entry->hostname, hostname, sizeof(entry->hostname) - 1U);
        kmemcpy(entry->address, address, 4U);
        entry->last_used = ++g_dns_cache_clock;
        entry->used = true;
    }
    net_spin_unlock(&g_dns_lock);
    return resolved;
}

static bool tcp_send_segment(tcp_socket_t *socket, uint8_t flags,
                             const void *data, uint16_t data_length,
                             uint32_t sequence) {
    uint8_t segment[sizeof(tcp_header_t) + 4U + TCP_MSS];
    uint8_t check[12U + sizeof(segment)];
    tcp_header_t *tcp = (tcp_header_t *)segment;
    uint16_t header_length = sizeof(tcp_header_t);
    uint16_t segment_length;
    uint16_t sum;
    if (!socket || data_length > TCP_MSS) return false;
    kmemset(segment, 0, sizeof(segment));
    tcp->source = be16(socket->local_port);
    tcp->destination = be16(socket->remote_port);
    tcp->sequence = be32(sequence);
    tcp->acknowledgment = be32(socket->receive_next);
    tcp->flags = flags;
    tcp->window = be16((uint16_t)(TCP_RECEIVE_SIZE - socket->receive_length));
    if (flags & TCP_FLAG_SYN) {
        /* MSS option: evita fragmentación y simplifica el receptor inicial. */
        segment[20] = 2; segment[21] = 4;
        segment[22] = (uint8_t)(TCP_MSS >> 8); segment[23] = (uint8_t)TCP_MSS;
        header_length = 24U;
    }
    tcp->data_offset = (uint8_t)((header_length / 4U) << 4);
    if (data_length) kmemcpy(segment + header_length, data, data_length);
    segment_length = (uint16_t)(header_length + data_length);
    kmemcpy(check, g_ip, 4); kmemcpy(check + 4, socket->remote_ip, 4);
    check[8] = 0; check[9] = IP_TCP;
    check[10] = (uint8_t)(segment_length >> 8); check[11] = (uint8_t)segment_length;
    kmemcpy(check + 12, segment, segment_length);
    sum = checksum(check, (uint32_t)segment_length + 12U);
    tcp->checksum = be16(sum);
    return ipv4_send_raw(socket->remote_ip, IP_TCP, segment, segment_length, false);
}

static uint8_t *dhcp_option(uint8_t *option, uint8_t code, const void *value, uint8_t length) {
    *option++ = code; *option++ = length; kmemcpy(option, value, length); return option + length;
}
static bool dhcp_send(uint8_t message_type, bool request) {
    uint8_t storage[sizeof(dhcp_packet_t)];
    dhcp_packet_t *dhcp = (dhcp_packet_t *)storage;
    uint8_t *option, requested[] = {1,3,6};
    static const uint8_t broadcast[4] = {255,255,255,255};
    uint16_t length;
    kmemset(dhcp, 0, sizeof(*dhcp)); dhcp->op = 1; dhcp->htype = 1; dhcp->hlen = 6;
    dhcp->xid = be32(g_dhcp_xid); dhcp->flags = be16(0x8000); kmemcpy(dhcp->chaddr, g_mac, 6);
    dhcp->magic = be32(DHCP_MAGIC); option = dhcp->options;
    option = dhcp_option(option, 53, &message_type, 1);
    if (request) {
        option = dhcp_option(option, 50, g_dhcp_offer, 4);
        option = dhcp_option(option, 54, g_dhcp_server, 4);
    }
    option = dhcp_option(option, 55, requested, sizeof(requested)); *option++ = 255;
    length = (uint16_t)((uintptr_t)option - (uintptr_t)dhcp);
    if (length < 300U) length = 300U;
    return udp_send(broadcast, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, dhcp, length, true);
}

static void dhcp_receive(const uint8_t *payload, uint16_t length) {
    const dhcp_packet_t *dhcp = (const dhcp_packet_t *)payload;
    const uint8_t *option, *end; uint8_t type = 0;
    if (length < 240U || dhcp->op != 2 || be32(dhcp->xid) != g_dhcp_xid ||
        be32(dhcp->magic) != DHCP_MAGIC || !bytes_equal(dhcp->chaddr, g_mac, 6)) return;
    kmemset(g_dhcp_mask, 0, 4); kmemset(g_dhcp_gateway, 0, 4); kmemset(g_dhcp_dns, 0, 4);
    option = dhcp->options; end = payload + length;
    while (option < end) {
        uint8_t code = *option++;
        uint8_t size;
        if (code == 255U) break;
        if (code == 0U) continue;
        if (option >= end) break;
        size = *option++;
        if ((uint32_t)(end - option) < size) break;
        if (code == 53U && size >= 1U) type = option[0];
        else if (code == 1U && size >= 4U) kmemcpy(g_dhcp_mask, option, 4);
        else if (code == 3U && size >= 4U) kmemcpy(g_dhcp_gateway, option, 4);
        else if (code == 6U && size >= 4U) kmemcpy(g_dhcp_dns, option, 4);
        else if (code == 54U && size >= 4U) kmemcpy(g_dhcp_server, option, 4);
        option += size;
    }
    if (type == 2U || type == 5U || type == 6U) {
        kmemcpy(g_dhcp_offer, dhcp->yiaddr, 4); g_dhcp_type = type;
    }
}

static void icmp_receive(const ipv4_header_t *ip, const uint8_t *payload, uint16_t length) {
    uint8_t reply[NET_FRAME_MAX - 34U];
    uint16_t received;
    if (length < 8U || checksum(payload, length) != 0U) return;
    received = ((uint16_t)payload[4] << 8) | payload[5];
    if (payload[0] == 0U && received == g_ping_id &&
        (((uint16_t)payload[6] << 8) | payload[7]) == g_ping_sequence &&
        bytes_equal(ip->source, g_ping_target, 4)) { g_ping_reply = true; return; }
    if (payload[0] != 8U || !g_configured || length > sizeof(reply)) return;
    kmemcpy(reply, payload, length); reply[0] = 0; reply[2] = reply[3] = 0;
    { uint16_t sum = checksum(reply, length); reply[2] = (uint8_t)(sum >> 8); reply[3] = (uint8_t)sum; }
    ipv4_send_raw(ip->source, IP_ICMP, reply, length, false);
}

static void tcp_receive(const ipv4_header_t *ip, const uint8_t *segment,
                        uint16_t length) {
    const tcp_header_t *tcp;
    tcp_socket_t *socket = NULL;
    uint8_t check[12U + NET_FRAME_MAX];
    uint16_t header_length, source_port, destination_port;
    uint32_t sequence, acknowledgment;
    uint16_t payload_length;
    if (length < sizeof(tcp_header_t) || length + 12U > sizeof(check)) return;
    tcp = (const tcp_header_t *)segment;
    header_length = (uint16_t)((tcp->data_offset >> 4) * 4U);
    if (header_length < sizeof(*tcp) || header_length > length) return;
    kmemcpy(check, ip->source, 4); kmemcpy(check + 4, ip->destination, 4);
    check[8] = 0; check[9] = IP_TCP;
    check[10] = (uint8_t)(length >> 8); check[11] = (uint8_t)length;
    kmemcpy(check + 12, segment, length);
    if (checksum(check, (uint32_t)length + 12U) != 0U) return;
    source_port = be16(tcp->source);
    destination_port = be16(tcp->destination);
    for (uint8_t i = 0; i < NET_SOCKET_MAX; i++) {
        if (g_sockets[i].state != TCP_CLOSED &&
            g_sockets[i].local_port == destination_port &&
            g_sockets[i].remote_port == source_port &&
            bytes_equal(g_sockets[i].remote_ip, ip->source, 4)) {
            socket = &g_sockets[i];
            break;
        }
    }
    sequence = be32(tcp->sequence);
    acknowledgment = be32(tcp->acknowledgment);
    payload_length = (uint16_t)(length - header_length);
    if (!socket && (tcp->flags & TCP_FLAG_SYN)) {
        int listener = -1, child = -1;
        for (uint8_t i = 0; i < NET_SOCKET_MAX; i++) {
            if (g_sockets[i].state == TCP_LISTEN &&
                g_sockets[i].local_port == destination_port) listener = i;
            else if (child < 0 && g_sockets[i].state == TCP_CLOSED) child = i;
        }
        if (listener >= 0 && child >= 0 && g_sockets[listener].pending < 0) {
            socket = &g_sockets[child];
            kmemset(socket, 0, sizeof(*socket));
            socket->local_port = destination_port;
            socket->remote_port = source_port;
            kmemcpy(socket->remote_ip, ip->source, 4);
            socket->receive_next = sequence + 1U;
            socket->send_unacknowledged = (pit_get_ticks() << 12) ^
                ((uint32_t)destination_port << 16) ^ (uint32_t)source_port;
            socket->send_next = socket->send_unacknowledged + 1U;
            socket->state = TCP_SYN_RECEIVED;
            socket->listener = (int8_t)listener;
            socket->pending = -1;
            g_sockets[listener].pending = (int8_t)child;
            tcp_send_segment(socket, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0,
                             socket->send_unacknowledged);
        }
        return;
    }
    if (!socket) return;
    if (tcp->flags & TCP_FLAG_RST) {
        socket->reset = true;
        socket->state = TCP_CLOSED;
        return;
    }
    if (tcp->flags & TCP_FLAG_ACK) {
        if (acknowledgment > socket->send_unacknowledged &&
            acknowledgment <= socket->send_next)
            socket->send_unacknowledged = acknowledgment;
    }
    if (socket->state == TCP_SYN_SENT) {
        if ((tcp->flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) ==
            (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
            acknowledgment == socket->send_next) {
            socket->receive_next = sequence + 1U;
            socket->state = TCP_ESTABLISHED;
            tcp_send_segment(socket, TCP_FLAG_ACK, NULL, 0, socket->send_next);
        }
        return;
    }
    if (socket->state == TCP_SYN_RECEIVED) {
        if ((tcp->flags & TCP_FLAG_ACK) && acknowledgment == socket->send_next)
            socket->state = TCP_ESTABLISHED;
        return;
    }
    if (socket->state != TCP_ESTABLISHED && socket->state != TCP_FIN_WAIT) return;
    if (payload_length && sequence == socket->receive_next) {
        uint32_t flags = net_irq_save();
        uint32_t available = TCP_RECEIVE_SIZE - socket->receive_length;
        uint32_t accepted = payload_length < available ? payload_length : available;
        if (accepted) {
            kmemcpy(socket->receive + socket->receive_length,
                    segment + header_length, accepted);
            socket->receive_length += accepted;
            socket->receive_next += accepted;
        }
        net_irq_restore(flags);
        tcp_send_segment(socket, TCP_FLAG_ACK, NULL, 0, socket->send_next);
    } else if (payload_length) {
        /* ACK duplicado pide la retransmisión del primer byte ausente. */
        tcp_send_segment(socket, TCP_FLAG_ACK, NULL, 0, socket->send_next);
    }
    if ((tcp->flags & TCP_FLAG_FIN) &&
        sequence + payload_length == socket->receive_next) {
        socket->receive_next++;
        tcp_send_segment(socket, TCP_FLAG_ACK, NULL, 0, socket->send_next);
        socket->state = TCP_CLOSE_WAIT;
    }
}

static void netstack_receive(const uint8_t *frame, uint16_t length) {
    const eth_header_t *eth;
    if (length < sizeof(eth_header_t)) return;
    eth = (const eth_header_t *)frame;
    if (be16(eth->type) == ETH_ARP) {
        const arp_packet_t *arp;
        if (length < sizeof(*eth) + sizeof(*arp)) return;
        arp = (const arp_packet_t *)(frame + sizeof(*eth));
        if (be16(arp->htype) != 1U || be16(arp->ptype) != ETH_IPV4 || arp->hlen != 6 || arp->plen != 4) return;
        kmemcpy(g_arp_ip, arp->spa, 4); kmemcpy(g_arp_mac, arp->sha, 6); g_arp_valid = true;
        if (be16(arp->operation) == 1U && g_configured && bytes_equal(arp->tpa, g_ip, 4))
            arp_send(2, arp->spa, arp->sha);
        return;
    }
    if (be16(eth->type) == ETH_IPV4) {
        const ipv4_header_t *ip; const uint8_t *payload; uint16_t ip_length, header_length, payload_length;
        if (length < sizeof(*eth) + sizeof(*ip)) return;
        ip = (const ipv4_header_t *)(frame + sizeof(*eth));
        header_length = (uint16_t)((ip->version_ihl & 0x0FU) * 4U); ip_length = be16(ip->length);
        if ((ip->version_ihl >> 4) != 4U || header_length < 20U || ip_length < header_length ||
            sizeof(*eth) + ip_length > length || checksum(ip, header_length) != 0U) return;
        if (g_configured && !bytes_equal(ip->destination, g_ip, 4) && !ip_broadcast(ip->destination)) return;
        payload = (const uint8_t *)ip + header_length; payload_length = (uint16_t)(ip_length - header_length);
        if (ip->protocol == IP_ICMP) icmp_receive(ip, payload, payload_length);
        else if (ip->protocol == IP_TCP) tcp_receive(ip, payload, payload_length);
        else if (ip->protocol == IP_UDP && payload_length >= sizeof(udp_header_t)) {
            const udp_header_t *udp = (const udp_header_t *)payload;
            uint16_t udp_length = be16(udp->length);
            if (be16(udp->destination) == DHCP_CLIENT_PORT && udp_length >= sizeof(*udp) && udp_length <= payload_length)
                dhcp_receive(payload + sizeof(*udp), (uint16_t)(udp_length - sizeof(*udp)));
            else if (be16(udp->source) == 53U &&
                     be16(udp->destination) == g_dns_port &&
                     udp_length >= sizeof(*udp) && udp_length <= payload_length)
                dns_receive(payload + sizeof(*udp),
                            (uint16_t)(udp_length - sizeof(*udp)));
            else if (udp_length >= sizeof(*udp) && udp_length <= payload_length) {
                uint16_t destination = be16(udp->destination);
                uint16_t source = be16(udp->source);
                for (uint8_t i = 0; i < NET_SOCKET_MAX; i++) {
                    udp_socket_t *socket = &g_udp_sockets[i];
                    uint16_t data_length;
                    if (!socket->used || socket->local_port != destination ||
                        socket->receive_length) continue;
                    if (socket->connected && (socket->remote_port != source ||
                        !bytes_equal(socket->remote_ip, ip->source, 4))) continue;
                    data_length = (uint16_t)(udp_length - sizeof(*udp));
                    if (data_length > UDP_RECEIVE_SIZE) data_length = UDP_RECEIVE_SIZE;
                    kmemcpy(socket->receive, payload + sizeof(*udp), data_length);
                    kmemcpy(socket->receive_ip, ip->source, 4);
                    socket->receive_port = source;
                    socket->receive_length = data_length;
                    break;
                }
            }
        }
    }
}

static bool stack_configure(const uint8_t address[4], const uint8_t mask[4],
                            const uint8_t gateway[4], const uint8_t dns[4]) {
    if (!refresh_device_mac() || !address || !mask || !gateway || !dns || ip_zero(address)) return false;
    kmemcpy(g_ip, address, 4); kmemcpy(g_mask, mask, 4);
    kmemcpy(g_gateway, gateway, 4); kmemcpy(g_dns, dns, 4);
    g_configured = true; g_arp_valid = false; return true;
}

static bool stack_dhcp(uint32_t timeout_ms) {
    uint32_t start, limit;
    if (!refresh_device_mac()) return false;
    kmemset(g_ip, 0, 4); g_configured = false; g_dhcp_type = 0;
    g_dhcp_xid = 0xB1E50000U ^ pit_get_ticks();
    if (!dhcp_send(1, false)) return false;
    start = pit_get_ticks(); limit = timeout_ticks(timeout_ms / 2U);
    while ((uint32_t)(pit_get_ticks() - start) < limit && g_dhcp_type != 2U) wait_one_tick();
    if (g_dhcp_type != 2U) return false;
    g_dhcp_type = 0; if (!dhcp_send(3, true)) return false;
    start = pit_get_ticks();
    while ((uint32_t)(pit_get_ticks() - start) < limit && g_dhcp_type != 5U && g_dhcp_type != 6U) wait_one_tick();
    if (g_dhcp_type != 5U) return false;
    if (ip_zero(g_dhcp_mask)) { g_dhcp_mask[0] = g_dhcp_mask[1] = g_dhcp_mask[2] = 255; }
    return stack_configure(g_dhcp_offer, g_dhcp_mask, g_dhcp_gateway, g_dhcp_dns);
}

static bool stack_ping(const uint8_t address[4], uint32_t timeout_ms, uint32_t *elapsed_ms) {
    uint8_t packet[40]; uint16_t sum; uint32_t start, limit, hz;
    if (!g_configured || !address) return false;
    kmemset(packet, 0, sizeof(packet)); packet[0] = 8;
    g_ping_id = 0xB1E5U; g_ping_sequence++; packet[4] = (uint8_t)(g_ping_id >> 8); packet[5] = (uint8_t)g_ping_id;
    packet[6] = (uint8_t)(g_ping_sequence >> 8); packet[7] = (uint8_t)g_ping_sequence;
    for (uint8_t i = 8; i < sizeof(packet); i++) packet[i] = i;
    sum = checksum(packet, sizeof(packet)); packet[2] = (uint8_t)(sum >> 8); packet[3] = (uint8_t)sum;
    kmemcpy(g_ping_target, address, 4); g_ping_reply = false; start = pit_get_ticks();
    if (!ipv4_send_raw(address, IP_ICMP, packet, sizeof(packet), false)) return false;
    limit = timeout_ticks(timeout_ms);
    while ((uint32_t)(pit_get_ticks() - start) < limit && !g_ping_reply) wait_one_tick();
    if (!g_ping_reply) return false;
    hz = pit_get_frequency_hz(); if (elapsed_ms) *elapsed_ms = ((pit_get_ticks() - start) * 1000U) / (hz ? hz : 1U);
    return true;
}

static int stack_socket_open(uint8_t type) {
    /* Los sockets locales no dependen de una placa Ethernet.  Esto permite
     * que cliente y servidor del mismo equipo se comuniquen por 127.0.0.1
     * incluso en una VM sin NIC; las operaciones remotas siguen exigiendo
     * una interfaz configurada. */
    if (type != NET_SOCKET_TCP && type != NET_SOCKET_UDP) return -1;
    net_spin_lock(&g_socket_lock);
    if (type == NET_SOCKET_UDP) {
        for (uint8_t i = 0; i < NET_SOCKET_MAX; i++) {
            if (!g_udp_sockets[i].used) {
                kmemset(&g_udp_sockets[i], 0, sizeof(g_udp_sockets[i]));
                g_udp_sockets[i].used = true;
                g_udp_sockets[i].local_port = g_next_ephemeral++;
                net_spin_unlock(&g_socket_lock);
                return UDP_HANDLE_BASE + i;
            }
        }
        net_spin_unlock(&g_socket_lock);
        return -1;
    }
    for (uint8_t i = 0; i < NET_SOCKET_MAX; i++) {
        if (g_sockets[i].state == TCP_CLOSED) {
            kmemset(&g_sockets[i], 0, sizeof(g_sockets[i]));
            g_sockets[i].local_port = g_next_ephemeral++;
            g_sockets[i].listener = g_sockets[i].pending =
                g_sockets[i].peer = -1;
            if (g_next_ephemeral < 49152U) g_next_ephemeral = 49152U;
            g_sockets[i].state = TCP_ALLOCATED;
            net_spin_unlock(&g_socket_lock);
            return i;
        }
    }
    net_spin_unlock(&g_socket_lock);
    return -1;
}

static bool socket_valid(int handle) {
    return handle >= 0 && handle < (int)NET_SOCKET_MAX;
}

static int32_t stack_socket_receivefrom(int handle, uint8_t address[4],
    uint16_t *port, void *data, uint32_t capacity, uint32_t timeout_ms);

static bool udp_socket_valid(int handle) {
    int slot = handle - UDP_HANDLE_BASE;
    return slot >= 0 && slot < (int)NET_SOCKET_MAX && g_udp_sockets[slot].used;
}

static bool stack_socket_bind(int handle, const uint8_t address[4], uint16_t port) {
    bool loopback = address && address[0] == 127U && address[1] == 0U &&
                    address[2] == 0U && address[3] == 1U;
    if (!port || (address && !ip_zero(address) && !loopback &&
                  !bytes_equal(address, g_ip, 4))) return false;
    for (uint8_t i = 0; i < NET_SOCKET_MAX; i++) {
        if (g_sockets[i].state != TCP_CLOSED && i != handle &&
            g_sockets[i].local_port == port) return false;
        if (g_udp_sockets[i].used && UDP_HANDLE_BASE + i != handle &&
            g_udp_sockets[i].local_port == port) return false;
    }
    if (socket_valid(handle) && g_sockets[handle].state == TCP_ALLOCATED) {
        g_sockets[handle].local_port = port;
        return true;
    }
    if (udp_socket_valid(handle)) {
        g_udp_sockets[handle - UDP_HANDLE_BASE].local_port = port;
        return true;
    }
    return false;
}

static bool stack_socket_listen(int handle, uint8_t backlog UNUSED) {
    if (!socket_valid(handle) || g_sockets[handle].state != TCP_ALLOCATED) return false;
    g_sockets[handle].state = TCP_LISTEN;
    g_sockets[handle].pending = -1;
    return true;
}

static int stack_socket_accept(int handle, uint8_t address[4], uint16_t *port,
                               uint32_t timeout_ms) {
    uint32_t start = pit_get_ticks(), limit = timeout_ticks(timeout_ms);
    tcp_socket_t *listener;
    if (!socket_valid(handle) || g_sockets[handle].state != TCP_LISTEN) return -1;
    listener = &g_sockets[handle];
    while (listener->pending < 0 ||
           g_sockets[(uint8_t)listener->pending].state != TCP_ESTABLISHED) {
        if ((uint32_t)(pit_get_ticks() - start) >= limit) return -1;
        wait_one_tick();
    }
    {
        int child = listener->pending;
        tcp_socket_t *accepted = &g_sockets[child];
        listener->pending = -1;
        accepted->listener = -1;
        if (address) kmemcpy(address, accepted->remote_ip, 4);
        if (port) *port = accepted->remote_port;
        return child;
    }
}

static bool stack_socket_connect(int handle, const uint8_t address[4],
                                 uint16_t port, uint32_t timeout_ms) {
    tcp_socket_t *socket;
    uint32_t start, limit, retry_ticks, last_send;
    if (udp_socket_valid(handle)) {
        udp_socket_t *udp = &g_udp_sockets[handle - UDP_HANDLE_BASE];
        if (!address || !port) return false;
        kmemcpy(udp->remote_ip, address, 4); udp->remote_port = port;
        udp->connected = true; return true;
    }
    if (!socket_valid(handle) || !address || !port) return false;
    socket = &g_sockets[handle];
    if (address[0] == 127U && address[1] == 0U &&
        address[2] == 0U && address[3] == 1U) {
        uint32_t start = pit_get_ticks();
        uint32_t limit = timeout_ticks(timeout_ms);
        do {
            int listener = -1;
            int child = -1;
            net_spin_lock(&g_socket_lock);
            for (uint8_t i = 0; i < NET_SOCKET_MAX; i++) {
                if (g_sockets[i].state == TCP_LISTEN &&
                    g_sockets[i].local_port == port &&
                    g_sockets[i].pending < 0) {
                    listener = i;
                    break;
                }
            }
            if (listener >= 0) {
                for (uint8_t i = 0; i < NET_SOCKET_MAX; i++) {
                    if (g_sockets[i].state == TCP_CLOSED) {
                        child = i;
                        break;
                    }
                }
            }
            if (child >= 0) {
                tcp_socket_t *accepted = &g_sockets[child];
                kmemset(accepted, 0, sizeof(*accepted));
                accepted->state = TCP_ESTABLISHED;
                accepted->local_port = port;
                accepted->remote_port = socket->local_port;
                accepted->listener = (int8_t)listener;
                accepted->pending = -1;
                accepted->peer = (int8_t)handle;
                kmemcpy(accepted->remote_ip, address, 4);

                socket->state = TCP_ESTABLISHED;
                socket->remote_port = port;
                socket->listener = -1;
                socket->pending = -1;
                socket->peer = (int8_t)child;
                kmemcpy(socket->remote_ip, address, 4);
                g_sockets[listener].pending = (int8_t)child;
            }
            net_spin_unlock(&g_socket_lock);
            if (child >= 0) return true;
            if ((uint32_t)(pit_get_ticks() - start) >= limit) return false;
            wait_one_tick();
        } while (true);
    }
    if (!g_configured) return false;
    kmemcpy(socket->remote_ip, address, 4);
    socket->remote_port = port;
    socket->receive_length = 0;
    socket->reset = false;
    socket->state = TCP_SYN_SENT;
    socket->send_unacknowledged = (pit_get_ticks() << 12) ^
                                  ((uint32_t)socket->local_port << 16) ^ 0xB1E50U;
    socket->send_next = socket->send_unacknowledged + 1U;
    socket->receive_next = 0;
    start = pit_get_ticks();
    limit = timeout_ticks(timeout_ms);
    retry_ticks = limit / 3U;
    if (!retry_ticks) retry_ticks = 1U;
    last_send = start - retry_ticks;
    while ((uint32_t)(pit_get_ticks() - start) < limit) {
        uint32_t now = pit_get_ticks();
        if ((uint32_t)(now - last_send) >= retry_ticks) {
            if (!tcp_send_segment(socket, TCP_FLAG_SYN, NULL, 0,
                                  socket->send_unacknowledged)) break;
            last_send = now;
        }
        if (socket->state == TCP_ESTABLISHED) return true;
        if (socket->reset || socket->state == TCP_CLOSED) break;
        wait_one_tick();
    }
    socket->state = TCP_CLOSED;
    return false;
}

static int32_t stack_socket_send(int handle, const void *data, uint32_t length,
                                 uint32_t timeout_ms) {
    tcp_socket_t *socket;
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sent = 0;
    if (udp_socket_valid(handle)) {
        udp_socket_t *udp = &g_udp_sockets[handle - UDP_HANDLE_BASE];
        if (!udp->connected || length > UDP_RECEIVE_SIZE || (!data && length)) return -1;
        return udp_send(udp->remote_ip, udp->local_port, udp->remote_port,
                        data, (uint16_t)length, false) ? (int32_t)length : -1;
    }
    if (!socket_valid(handle) || (!data && length)) return -1;
    socket = &g_sockets[handle];
    if (socket->state != TCP_ESTABLISHED) return -1;
    if (socket->peer >= 0 && socket_valid(socket->peer)) {
        tcp_socket_t *peer = &g_sockets[(uint8_t)socket->peer];
        uint32_t available;
        uint32_t flags;
        if (peer->state != TCP_ESTABLISHED && peer->state != TCP_CLOSE_WAIT)
            return -1;
        flags = net_irq_save();
        available = TCP_RECEIVE_SIZE - peer->receive_length;
        if (length > available) length = available;
        if (length) {
            kmemcpy(peer->receive + peer->receive_length, data, length);
            peer->receive_length += length;
        }
        net_irq_restore(flags);
        return (int32_t)length;
    }
    while (sent < length) {
        uint16_t chunk = (uint16_t)((length - sent) > TCP_MSS
                         ? TCP_MSS : (length - sent));
        uint32_t sequence = socket->send_next;
        uint32_t expected = sequence + chunk;
        uint32_t start = pit_get_ticks();
        uint32_t limit = timeout_ticks(timeout_ms);
        uint32_t retry = limit / 3U;
        uint32_t last_send;
        if (!retry) retry = 1U;
        socket->send_next = expected;
        last_send = start - retry;
        while (socket->send_unacknowledged < expected &&
               (uint32_t)(pit_get_ticks() - start) < limit) {
            uint32_t now = pit_get_ticks();
            if ((uint32_t)(now - last_send) >= retry) {
                if (!tcp_send_segment(socket, TCP_FLAG_ACK | TCP_FLAG_PSH,
                                      bytes + sent, chunk, sequence)) {
                    socket->send_next = sequence;
                    return sent ? (int32_t)sent : -1;
                }
                last_send = now;
            }
            if (socket->reset || socket->state == TCP_CLOSED) return sent ? (int32_t)sent : -1;
            wait_one_tick();
        }
        if (socket->send_unacknowledged < expected) {
            socket->send_next = sequence;
            return sent ? (int32_t)sent : -1;
        }
        sent += chunk;
    }
    return (int32_t)sent;
}

static int32_t stack_socket_receive(int handle, void *data, uint32_t capacity,
                                    uint32_t timeout_ms) {
    tcp_socket_t *socket;
    uint32_t start, limit, take, remaining, flags;
    if (udp_socket_valid(handle))
        return stack_socket_receivefrom(handle, NULL, NULL, data, capacity, timeout_ms);
    if (!socket_valid(handle) || !data || !capacity) return -1;
    socket = &g_sockets[handle];
    start = pit_get_ticks(); limit = timeout_ticks(timeout_ms);
    while (!socket->receive_length && socket->state != TCP_CLOSE_WAIT &&
           socket->state != TCP_CLOSED && !socket->reset) {
        if ((uint32_t)(pit_get_ticks() - start) >= limit) return 0;
        wait_one_tick();
    }
    if (!socket->receive_length) return 0;
    flags = net_irq_save();
    if (!socket->receive_length) {
        net_irq_restore(flags);
        return 0;
    }
    take = socket->receive_length < capacity ? socket->receive_length : capacity;
    kmemcpy(data, socket->receive, take);
    remaining = socket->receive_length - take;
    if (remaining) kmemcpy(socket->receive, socket->receive + take, remaining);
    socket->receive_length = remaining;
    net_irq_restore(flags);
    return (int32_t)take;
}

static int32_t stack_socket_sendto(int handle, const uint8_t address[4], uint16_t port,
                                   const void *data, uint32_t length,
                                   uint32_t timeout_ms UNUSED) {
    udp_socket_t *socket;
    if (!udp_socket_valid(handle) || !address || !port || (!data && length) ||
        length > UDP_RECEIVE_SIZE) return -1;
    socket = &g_udp_sockets[handle - UDP_HANDLE_BASE];
    return udp_send(address, socket->local_port, port, data, (uint16_t)length,
                    ip_broadcast(address)) ? (int32_t)length : -1;
}

static int32_t stack_socket_receivefrom(int handle, uint8_t address[4], uint16_t *port,
                                        void *data, uint32_t capacity,
                                        uint32_t timeout_ms) {
    udp_socket_t *socket;
    uint32_t start, limit, flags, take;
    if (!udp_socket_valid(handle) || !data || !capacity) return -1;
    socket = &g_udp_sockets[handle - UDP_HANDLE_BASE];
    start = pit_get_ticks(); limit = timeout_ticks(timeout_ms);
    while (!socket->receive_length) {
        if ((uint32_t)(pit_get_ticks() - start) >= limit) return 0;
        wait_one_tick();
    }
    flags = net_irq_save();
    take = socket->receive_length < capacity ? socket->receive_length : capacity;
    kmemcpy(data, socket->receive, take);
    if (address) kmemcpy(address, socket->receive_ip, 4);
    if (port) *port = socket->receive_port;
    socket->receive_length = 0;
    net_irq_restore(flags);
    return (int32_t)take;
}

static bool stack_socket_readable(int handle) {
    if (udp_socket_valid(handle)) return g_udp_sockets[handle - UDP_HANDLE_BASE].receive_length != 0;
    if (!socket_valid(handle)) return false;
    if (g_sockets[handle].state == TCP_LISTEN) {
        int child = g_sockets[handle].pending;
        return child >= 0 && g_sockets[child].state == TCP_ESTABLISHED;
    }
    return g_sockets[handle].receive_length != 0 ||
           g_sockets[handle].state == TCP_CLOSE_WAIT;
}

static void stack_socket_close(int handle) {
    tcp_socket_t *socket;
    uint32_t start, limit;
    if (udp_socket_valid(handle)) {
        kmemset(&g_udp_sockets[handle - UDP_HANDLE_BASE], 0,
                sizeof(g_udp_sockets[0]));
        return;
    }
    if (!socket_valid(handle)) return;
    socket = &g_sockets[handle];
    if (socket->peer >= 0 && socket_valid(socket->peer)) {
        tcp_socket_t *peer = &g_sockets[(uint8_t)socket->peer];
        peer->peer = -1;
        if (peer->state == TCP_ESTABLISHED) peer->state = TCP_CLOSE_WAIT;
        kmemset(socket, 0, sizeof(*socket));
        return;
    }
    if (socket->state == TCP_ESTABLISHED) {
        uint32_t sequence = socket->send_next;
        socket->send_next++;
        socket->state = TCP_FIN_WAIT;
        tcp_send_segment(socket, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0, sequence);
        start = pit_get_ticks(); limit = timeout_ticks(1000U);
        while (socket->send_unacknowledged < socket->send_next &&
               (uint32_t)(pit_get_ticks() - start) < limit) wait_one_tick();
    }
    if (socket->state == TCP_LISTEN && socket->pending >= 0)
        kmemset(&g_sockets[(uint8_t)socket->pending], 0, sizeof(*socket));
    kmemset(socket, 0, sizeof(*socket));
}

static bool append_string(char *destination, uint32_t capacity,
                          uint32_t *used, const char *source) {
    while (source && *source) {
        if (*used + 1U >= capacity) return false;
        destination[(*used)++] = *source++;
    }
    destination[*used] = '\0';
    return true;
}

static bool stack_parse_http_endpoint(const char *url, char host[128],
                                      uint16_t *port) {
    const char *cursor;
    uint32_t host_length = 0U;
    uint32_t parsed = 0U;
    if (!url || !host || !port || kstrncmp(url, "http://", 7U) != 0)
        return false;
    cursor = url + 7U;
    while (*cursor && *cursor != '/' && *cursor != ':' && *cursor != '?' &&
           *cursor != '#') {
        if (host_length + 1U >= 128U) return false;
        host[host_length++] = *cursor++;
    }
    host[host_length] = '\0';
    if (!host_length) return false;
    *port = 80U;
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

static int32_t stack_http_exchange(const char *url, const void *request,
                                   uint32_t request_length, void *data,
                                   uint32_t capacity, uint32_t timeout_ms) {
    char host[128];
    uint16_t port;
    uint8_t address[4];
    uint32_t total = 0U;
    int socket;
    if (!request || !request_length || !data || !capacity ||
        !stack_parse_http_endpoint(url, host, &port))
        return -1;
    if (!stack_resolve(host, address, timeout_ms)) return -1;
    socket = stack_socket_open(NET_SOCKET_TCP);
    if (socket < 0 || !stack_socket_connect(socket, address, port, timeout_ms)) {
        if (socket >= 0) stack_socket_close(socket);
        return -1;
    }
    if (stack_socket_send(socket, request, request_length, timeout_ms) !=
        (int32_t)request_length) {
        stack_socket_close(socket);
        return -1;
    }
    while (total < capacity) {
        int32_t received = stack_socket_receive(socket, (uint8_t *)data + total,
                                                capacity - total, timeout_ms);
        if (received <= 0) break;
        total += (uint32_t)received;
    }
    stack_socket_close(socket);
    return total ? (int32_t)total : -1;
}

static int32_t stack_http_get(const char *url, void *data, uint32_t capacity,
                              uint32_t timeout_ms) {
    char host[128], path[256], request[640];
    const char *cursor;
    uint32_t host_length = 0U, path_length = 0U, used = 0U;
    uint16_t port = 80U;
    if (!url || kstrncmp(url, "http://", 7U) != 0) return -1;
    cursor = url + 7U;
    while (*cursor && *cursor != '/' && *cursor != ':' && *cursor != '?' &&
           *cursor != '#') {
        if (host_length + 1U >= sizeof(host)) return -1;
        host[host_length++] = *cursor++;
    }
    host[host_length] = '\0';
    if (!host_length) return -1;
    if (*cursor == ':') {
        uint32_t parsed = 0U;
        cursor++;
        if (*cursor < '0' || *cursor > '9') return -1;
        while (*cursor >= '0' && *cursor <= '9') {
            parsed = parsed * 10U + (uint32_t)(*cursor++ - '0');
            if (parsed > 65535U) return -1;
        }
        port = (uint16_t)parsed;
    }
    (void)port;
    path[path_length++] = '/';
    if (*cursor == '/') path_length = 0U;
    if (*cursor == '/' || *cursor == '?') {
        if (*cursor == '?') path[path_length++] = '/';
        while (*cursor && *cursor != '#' && path_length + 1U < sizeof(path))
            path[path_length++] = *cursor++;
        if (*cursor && *cursor != '#') return -1;
    }
    path[path_length] = '\0';
    request[0] = '\0';
    if (!append_string(request, sizeof(request), &used, "GET ") ||
        !append_string(request, sizeof(request), &used, path) ||
        !append_string(request, sizeof(request), &used, " HTTP/1.1\r\nHost: ") ||
        !append_string(request, sizeof(request), &used, host) ||
        !append_string(request, sizeof(request), &used,
            "\r\nUser-Agent: NetSurf/3.11 (BlesKernOS 0.8)\r\n"
            "Accept: text/html,text/css,image/png,image/jpeg,image/gif,image/bmp,*/*;q=0.2\r\n"
            "Accept-Encoding: identity\r\nConnection: close\r\n\r\n"))
        return -1;
    return stack_http_exchange(url, request, used, data, capacity, timeout_ms);
}

static void stack_info(net_info_t *info) {
    if (!info) return;
    info->configured = g_configured;
    kmemcpy(info->address, g_ip, 4); kmemcpy(info->netmask, g_mask, 4);
    kmemcpy(info->gateway, g_gateway, 4); kmemcpy(info->dns, g_dns, 4);
}

static const net_stack_ops_t g_ops = {
    stack_dhcp, stack_configure, stack_ping, stack_resolve,
    stack_socket_open, stack_socket_connect, stack_socket_bind,
    stack_socket_listen, stack_socket_accept, stack_socket_send,
    stack_socket_receive, stack_socket_sendto, stack_socket_receivefrom,
    stack_socket_readable, stack_socket_close, stack_http_get,
    stack_http_exchange, stack_info
};
static bool netstack_init(void) {
    if (!network_register_stack(&g_ops)) return false;
    netdev_set_rx_handler(netstack_receive);
    kprintf("  [NET] Ethernet/ARP/IPv4/ICMP/UDP/DHCP/DNS/TCP/HTTP instalado\n"); return true;
}
static void netstack_shutdown(void) { netdev_set_rx_handler(NULL); network_unregister_stack(&g_ops); }
const bk_driver_module_t *bleskernos_driver_query(void) {
    static const bk_driver_module_t module = { BK_DRIVER_ABI_VERSION, sizeof(bk_driver_module_t),
        "netstack-ipv4", "Ethernet, ARP, IPv4, ICMP, UDP y DHCP", netstack_init, netstack_shutdown };
    return &module;
}
