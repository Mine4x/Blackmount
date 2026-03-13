#ifndef XHCI_DEVICE_H
#define XHCI_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "xhci_common.h"
#include "xhci_mem.h"
#include "xhci_trb.h"


// Slot Context — xHCI Spec Section 6.2.2
typedef struct {
    union {
        struct {
            uint32_t route_string    : 20;
            uint32_t speed           : 4;  // XHCI_USB_SPEED_*
            uint32_t rsvd0           : 1;
            uint32_t mtt             : 1;
            uint32_t hub             : 1;
            uint32_t context_entries : 5;  // DCI of the last valid EP context
        };
        uint32_t dword0;
    };
    union {
        struct {
            uint32_t max_exit_latency : 16;
            uint32_t root_hub_port    : 8;  // 1-based
            uint32_t num_ports        : 8;
        };
        uint32_t dword1;
    };
    union {
        struct {
            uint32_t tt_slot_id         : 8;
            uint32_t tt_port_num        : 8;
            uint32_t ttt                : 2;
            uint32_t rsvd1              : 4;
            uint32_t interrupter_target : 10;
        };
        uint32_t dword2;
    };
    union {
        struct {
            uint32_t usb_dev_addr : 8;
            uint32_t rsvd2        : 19;
            uint32_t slot_state   : 5;  // XHCI_SLOT_STATE_*
        };
        uint32_t dword3;
    };
    uint32_t rsvd3[4];
} __attribute__((packed)) xhci_slot_context_t;
_Static_assert(sizeof(xhci_slot_context_t) == 32,
               "xhci_slot_context_t must be 32 bytes");

// Endpoint Context — xHCI Spec Section 6.2.3
typedef struct {
    union {
        struct {
            uint32_t ep_state     : 3;  // XHCI_ENDPOINT_STATE_*
            uint32_t rsvd0        : 5;
            uint32_t mult         : 2;
            uint32_t max_pstreams : 5;
            uint32_t lsa          : 1;
            uint32_t interval     : 8;  // Scheduling period: 2^interval * 125µs
            uint32_t max_esit_hi  : 8;
        };
        uint32_t dword0;
    };
    union {
        struct {
            uint32_t rsvd1           : 1;
            uint32_t cerr            : 2;  // Error count (3 = max retries)
            uint32_t ep_type         : 3;  // XHCI_ENDPOINT_TYPE_*
            uint32_t rsvd2           : 1;
            uint32_t hid             : 1;
            uint32_t max_burst_size  : 8;
            uint32_t max_packet_size : 16;
        };
        uint32_t dword1;
    };
    // Transfer Ring Dequeue Pointer: bits [63:4]=phys addr, bit[0]=DCS
    uint32_t tr_dequeue_lo;
    uint32_t tr_dequeue_hi;
    union {
        struct {
            uint32_t avg_trb_len : 16;
            uint32_t max_esit_lo : 16;
        };
        uint32_t dword4;
    };
    uint32_t rsvd3[3];
} __attribute__((packed)) xhci_endpoint_context_t;
_Static_assert(sizeof(xhci_endpoint_context_t) == 32,
               "xhci_endpoint_context_t must be 32 bytes");

// Input Control Context — xHCI Spec Section 6.2.5.1
typedef struct {
    uint32_t drop_flags;  // Contexts to remove; bit 0=Slot, bit N=DCI N
    uint32_t add_flags;   // Contexts to initialize; bit 0=Slot, bit N=DCI N
    uint32_t rsvd0[5];
    union {
        struct {
            uint8_t config_value;
            uint8_t interface_num;
            uint8_t alternate_setting;
            uint8_t rsvd1;
        };
        uint32_t dword7;
    };
} __attribute__((packed)) xhci_input_control_context_t;
_Static_assert(sizeof(xhci_input_control_context_t) == 32,
               "xhci_input_control_context_t must be 32 bytes");

typedef struct {
    uint64_t trb_pointer;
    union {
        struct {
            uint32_t trb_transfer_length : 24;
            uint32_t completion_code     : 8;
        };
        uint32_t status;
    };
    union {
        struct {
            uint32_t cycle_bit   : 1;
            uint32_t rsvd0       : 1;
            uint32_t event_data  : 1;
            uint32_t rsvd1       : 7;
            uint32_t trb_type    : 6;   // = XHCI_TRB_TYPE_TRANSFER_EVENT
            uint32_t endpoint_id : 5;   // DCI of endpoint that completed
            uint32_t rsvd2       : 3;
            uint32_t slot_id     : 8;
        };
        uint32_t control;
    };
} xhci_transfer_event_trb_t;
_Static_assert(sizeof(xhci_transfer_event_trb_t) == 16,
               "xhci_transfer_event_trb_t must be 16 bytes");

typedef struct {
    union {
        struct {
            uint32_t rsvd0   : 24;
            uint32_t port_id : 8;  // 1-based root hub port number
        };
        uint32_t dword0;
    };
    uint32_t rsvd1;
    union {
        struct {
            uint32_t rsvd2           : 24;
            uint32_t completion_code : 8;
        };
        uint32_t status;
    };
    union {
        struct {
            uint32_t cycle_bit : 1;
            uint32_t rsvd3     : 9;
            uint32_t trb_type  : 6;  // = XHCI_TRB_TYPE_PORT_STATUS_CHANGE_EVENT
            uint32_t rsvd4     : 16;
        };
        uint32_t control;
    };
} xhci_port_status_change_event_trb_t;
_Static_assert(sizeof(xhci_port_status_change_event_trb_t) == 16,
               "xhci_port_status_change_event_trb_t must be 16 bytes");


typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_packet_t;
_Static_assert(sizeof(usb_setup_packet_t) == 8, "usb_setup_packet_t must be 8 bytes");

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_device_descriptor_t;
_Static_assert(sizeof(usb_device_descriptor_t) == 18,
               "usb_device_descriptor_t must be 18 bytes");

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed)) usb_config_descriptor_t;
_Static_assert(sizeof(usb_config_descriptor_t) == 9,
               "usb_config_descriptor_t must be 9 bytes");

// Standard request codes
#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_DESCRIPTOR    0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE     0x0A
#define USB_REQ_SET_INTERFACE     0x0B

// Descriptor types
#define USB_DESC_TYPE_DEVICE      0x01
#define USB_DESC_TYPE_CONFIG      0x02
#define USB_DESC_TYPE_STRING      0x03
#define USB_DESC_TYPE_INTERFACE   0x04
#define USB_DESC_TYPE_ENDPOINT    0x05
#define USB_DESC_TYPE_BOS         0x0F

// bmRequestType field builders
#define USB_REQTYPE_DIR_OUT         0x00
#define USB_REQTYPE_DIR_IN          0x80
#define USB_REQTYPE_TYPE_STANDARD   0x00
#define USB_REQTYPE_TYPE_CLASS      0x20
#define USB_REQTYPE_TYPE_VENDOR     0x40
#define USB_REQTYPE_RECIP_DEVICE    0x00
#define USB_REQTYPE_RECIP_INTERFACE 0x01
#define USB_REQTYPE_RECIP_ENDPOINT  0x02

typedef struct {
    size_t      max_trb_count;
    size_t      enqueue_ptr;
    xhci_trb_t* trbs;
    uintptr_t   physical_base;
    uint8_t     cycle_bit;
} xhci_transfer_ring_t;

void xhci_transfer_ring_init(xhci_transfer_ring_t* ring, size_t max_trbs);
void xhci_transfer_ring_enqueue(xhci_transfer_ring_t* ring, xhci_trb_t* trb);


#define XHCI_MAX_DEVICES 256
#define XHCI_MAX_DCI     32  // DCI values 1–31; index 0 unused

// Forward-declare so xhci_device_t can hold a back-pointer
struct xhci_controller;

typedef struct {
    bool    active;
    uint8_t slot_id;
    uint8_t port;    // 0-based
    uint8_t speed;   // XHCI_USB_SPEED_*

    // Back-pointer to the owning controller — used by xhci_wait_transfer()
    // to poll the event ring when running without a working IRQ.
    struct xhci_controller* hc;

    // DMA context buffers
    void* output_ctx;  // Output Device Context (phys addr in DCBAA[slot_id])
    void* input_ctx;   // Input Context (phys addr in Address/Configure commands)

    // EP0 transfer ring and its completion signal
    xhci_transfer_ring_t ep0_ring;
    volatile uint8_t     transfer_completed;
    xhci_trb_t           last_transfer_event;

    // Additional endpoint rings indexed by DCI (valid range 2–31).
    //
    // DCI = (ep_number * 2) + direction_bit
    //   direction_bit: 1 = IN (device→host), 0 = OUT (host→device)
    //   EP0 bidirectional control = DCI 1 → uses ep0_ring above
    //   EP1 OUT = DCI 2,  EP1 IN = DCI 3
    //   EP2 OUT = DCI 4,  EP2 IN = DCI 5   … and so on
    xhci_transfer_ring_t ep_rings[XHCI_MAX_DCI];
    volatile uint8_t     ep_transfer_completed[XHCI_MAX_DCI];
    xhci_trb_t           ep_last_transfer_event[XHCI_MAX_DCI];

    // Cached device descriptor (filled by GET_DESCRIPTOR during enumeration)
    usb_device_descriptor_t descriptor;
} xhci_device_t;

// Derive DCI from a USB endpoint address byte.
// ep_addr: bit 7 = direction (1=IN, 0=OUT), bits [3:0] = endpoint number.
static inline uint8_t xhci_dci_from_ep_addr(uint8_t ep_addr)
{
    uint8_t ep_num = ep_addr & 0x0F;
    uint8_t dir    = (ep_addr & 0x80) ? 1u : 0u;
    if (ep_num == 0) return 1;
    return (uint8_t)(ep_num * 2u + dir);
}

#endif // XHCI_DEVICE_H