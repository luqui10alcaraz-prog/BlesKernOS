#ifndef NETWORK_H
#define NETWORK_H

#include "types.h"

#define NET_MAC_LENGTH       6U
#define NET_DEVICE_NAME_MAX 16U
#define NET_FRAME_MAX      1536U
#define NET_SOCKET_MAX        8U
#define NET_SOCKET_TCP        1U
#define NET_SOCKET_UDP        2U

typedef struct {
    const char *name;
    const uint8_t *mac;
    uint16_t mtu;
    bool (*send)(const void *frame, uint16_t length);
    bool (*link_up)(void);
    void (*poll)(void);
} net_device_ops_t;

typedef void (*net_rx_handler_t)(const uint8_t *frame, uint16_t length);

typedef struct {
    bool configured;
    bool link_up;
    bool stack_ready;
    bool tls_ready;
    char device[NET_DEVICE_NAME_MAX];
    uint16_t mtu;
    uint8_t mac[NET_MAC_LENGTH];
    uint8_t address[4];
    uint8_t netmask[4];
    uint8_t gateway[4];
    uint8_t dns[4];
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_dropped;
} net_info_t;

typedef struct {
    bool (*dhcp)(uint32_t timeout_ms);
    bool (*configure)(const uint8_t address[4], const uint8_t netmask[4],
                      const uint8_t gateway[4], const uint8_t dns[4]);
    bool (*ping)(const uint8_t address[4], uint32_t timeout_ms,
                 uint32_t *round_trip_ms);
    bool (*resolve)(const char *hostname, uint8_t address[4],
                    uint32_t timeout_ms);
    int (*socket_open)(uint8_t type);
    bool (*socket_connect)(int socket, const uint8_t address[4],
                           uint16_t port, uint32_t timeout_ms);
    bool (*socket_bind)(int socket, const uint8_t address[4], uint16_t port);
    bool (*socket_listen)(int socket, uint8_t backlog);
    int (*socket_accept)(int socket, uint8_t address[4], uint16_t *port,
                         uint32_t timeout_ms);
    int32_t (*socket_send)(int socket, const void *data, uint32_t length,
                           uint32_t timeout_ms);
    int32_t (*socket_receive)(int socket, void *data, uint32_t capacity,
                              uint32_t timeout_ms);
    int32_t (*socket_sendto)(int socket, const uint8_t address[4], uint16_t port,
                             const void *data, uint32_t length, uint32_t timeout_ms);
    int32_t (*socket_receivefrom)(int socket, uint8_t address[4], uint16_t *port,
                                  void *data, uint32_t capacity, uint32_t timeout_ms);
    bool (*socket_readable)(int socket);
    void (*socket_close)(int socket);
    int32_t (*http_get)(const char *url, void *data, uint32_t capacity,
                        uint32_t timeout_ms);
    int32_t (*http_exchange)(const char *url, const void *request,
                             uint32_t request_length, void *data,
                             uint32_t capacity, uint32_t timeout_ms);
    void (*get_info)(net_info_t *info);
} net_stack_ops_t;

typedef struct {
    int32_t (*https_get)(const char *url, void *data, uint32_t capacity,
                         uint32_t timeout_ms);
    int32_t (*https_exchange)(const char *url, const void *request,
                              uint32_t request_length, void *data,
                              uint32_t capacity, uint32_t timeout_ms);
    int (*last_error)(void);
} net_tls_ops_t;

/* Frontera estable entre drivers Ethernet y una pila de protocolos .DVR. */
bool netdev_register(const net_device_ops_t *ops);
void netdev_unregister(const net_device_ops_t *ops);
bool netdev_present(void);
bool netdev_send(const void *frame, uint16_t length);
void netdev_poll(void);
void netdev_receive(const void *frame, uint16_t length);
void netdev_set_rx_handler(net_rx_handler_t handler);
void netdev_get_info(net_info_t *info);

/* Proxy pequeño usado por la shell; los protocolos permanecen externos. */
bool network_register_stack(const net_stack_ops_t *ops);
void network_unregister_stack(const net_stack_ops_t *ops);
bool network_dhcp(uint32_t timeout_ms);
bool network_configure(const uint8_t address[4], const uint8_t netmask[4],
                       const uint8_t gateway[4], const uint8_t dns[4]);
bool network_ping(const uint8_t address[4], uint32_t timeout_ms,
                  uint32_t *round_trip_ms);
bool network_resolve(const char *hostname, uint8_t address[4],
                     uint32_t timeout_ms);
int network_socket_open(uint8_t type);
bool network_socket_connect(int socket, const uint8_t address[4],
                            uint16_t port, uint32_t timeout_ms);
bool network_socket_bind(int socket, const uint8_t address[4], uint16_t port);
bool network_socket_listen(int socket, uint8_t backlog);
int network_socket_accept(int socket, uint8_t address[4], uint16_t *port,
                          uint32_t timeout_ms);
int32_t network_socket_send(int socket, const void *data, uint32_t length,
                            uint32_t timeout_ms);
int32_t network_socket_receive(int socket, void *data, uint32_t capacity,
                               uint32_t timeout_ms);
int32_t network_socket_sendto(int socket, const uint8_t address[4], uint16_t port,
                              const void *data, uint32_t length, uint32_t timeout_ms);
int32_t network_socket_receivefrom(int socket, uint8_t address[4], uint16_t *port,
                                   void *data, uint32_t capacity, uint32_t timeout_ms);
bool network_socket_readable(int socket);
void network_socket_close(int socket);
int32_t network_http_get(const char *url, void *data, uint32_t capacity,
                         uint32_t timeout_ms);
int32_t network_http_exchange(const char *url, const void *request,
                              uint32_t request_length, void *data,
                              uint32_t capacity, uint32_t timeout_ms);
bool network_register_tls(const net_tls_ops_t *ops);
void network_unregister_tls(const net_tls_ops_t *ops);
int32_t network_https_get(const char *url, void *data, uint32_t capacity,
                          uint32_t timeout_ms);
int32_t network_https_exchange(const char *url, const void *request,
                               uint32_t request_length, void *data,
                               uint32_t capacity, uint32_t timeout_ms);
int network_tls_last_error(void);
void network_get_info(net_info_t *info);
void network_start_autoconfigure(void);

#endif
