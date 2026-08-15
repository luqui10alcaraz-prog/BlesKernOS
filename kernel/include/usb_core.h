#ifndef BK_USB_CORE_H
#define BK_USB_CORE_H

#include "types.h"

#define BK_USB_CORE_ABI_VERSION 1U
#define BK_USB_MAX_INTERFACES   8U
#define BK_USB_MAX_ENDPOINTS    8U
#define BK_USB_MAX_EXTRA        96U
#define BK_USB_MAX_DEVICES      32U

#define USB_DIR_IN              0x80U
#define USB_ENDPOINT_NUMBER(x)  ((x) & 0x0FU)
#define USB_ENDPOINT_IN(x)      (((x) & USB_DIR_IN) != 0U)

#define USB_EP_CONTROL          0U
#define USB_EP_ISOCHRONOUS      1U
#define USB_EP_BULK             2U
#define USB_EP_INTERRUPT        3U

#define USB_CLASS_HID           0x03U
#define USB_CLASS_PRINTER       0x07U
#define USB_CLASS_MASS_STORAGE  0x08U
#define USB_CLASS_HUB           0x09U

typedef enum {
    USB_SPEED_LOW = 1,
    USB_SPEED_FULL = 2
} usb_speed_t;

typedef struct usb_endpoint {
    uint8_t address;
    uint8_t attributes;
    uint16_t max_packet;
    uint8_t interval;
    uint8_t toggle;
} usb_endpoint_t;

typedef struct usb_interface {
    uint8_t number;
    uint8_t alternate;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t protocol;
    uint8_t endpoint_count;
    usb_endpoint_t endpoints[BK_USB_MAX_ENDPOINTS];
    uint8_t extra_length;
    uint8_t extra[BK_USB_MAX_EXTRA];
    bool claimed;
    const void *owner;
} usb_interface_t;

typedef struct usb_device {
    uint32_t core_abi;
    uint32_t id;
    uint8_t address;
    uint8_t max_packet0;
    usb_speed_t speed;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_version;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint8_t configuration;
    uint8_t interface_count;
    usb_interface_t interfaces[BK_USB_MAX_INTERFACES];
    uint32_t parent_id;
    uint8_t parent_port;
    uint8_t root_port;
    uint8_t depth;
    bool connected;
    void *hcd_private;
} usb_device_t;

typedef struct usb_class_driver {
    uint32_t abi_version;
    uint32_t descriptor_size;
    const char *name;
    bool (*probe)(usb_device_t *device, usb_interface_t *interface);
    void (*disconnect)(usb_device_t *device, usb_interface_t *interface);
} usb_class_driver_t;

bool usb_class_register(const usb_class_driver_t *driver);
bool usb_class_unregister(const usb_class_driver_t *driver);
uint32_t usb_device_count(void);
usb_device_t *usb_device_at(uint32_t index);
usb_endpoint_t *usb_interface_endpoint(usb_interface_t *interface,
                                       uint8_t type, bool in,
                                       uint32_t ordinal);

bool usb_control_transfer(usb_device_t *device, uint8_t request_type,
                          uint8_t request, uint16_t value, uint16_t index,
                          void *data, uint16_t length);
bool usb_endpoint_transfer(usb_device_t *device, usb_endpoint_t *endpoint,
                           void *data, uint32_t length,
                           uint32_t *actual_length,
                           uint32_t timeout_ms);
bool usb_clear_endpoint_halt(usb_device_t *device,
                             usb_endpoint_t *endpoint);
bool usb_set_interface(usb_device_t *device, uint8_t interface_number,
                       uint8_t alternate_setting);

/* Used by an external hub-class driver after it has powered and reset a
 * downstream port.  USB 1.1 UHCI can route full/low-speed children directly.
 */
bool usb_enumerate_hub_port(usb_device_t *hub, uint8_t port,
                            usb_speed_t speed);
void usb_disconnect_hub_port(usb_device_t *hub, uint8_t port);

#endif
