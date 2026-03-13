#ifndef XHCI_EXT_CAP_H
#define XHCI_EXT_CAP_H
#include <stdint.h>

typedef struct xhci_usb_supported_protocol_capability {
    union {
        struct {
            uint8_t id;
            uint8_t next;
            uint8_t minor_revision_version;
            uint8_t major_revision_version;
        };
        uint32_t dword0;
    };

    union {
        uint32_t dword1;
        uint32_t name; // "USB "
    };

    union {
        struct {
            uint8_t compatible_port_offset;
            uint8_t compatible_port_count;
            uint8_t protocol_defined;
            uint8_t protocol_speed_id_count; // PSIC
        };
        uint32_t dword2;
    };

    union {
        struct {
            uint32_t slot_type : 4;
            uint32_t reserved  : 28;
        };
        uint32_t dword3;
    };
} xhci_usb_supported_protocol_capability_t;

_Static_assert(sizeof(xhci_usb_supported_protocol_capability_t) == 4 * sizeof(uint32_t),
               "xhci_usb_supported_protocol_capability_t must be 16 bytes");

static inline void xhci_usb_supported_protocol_capability_init(
    xhci_usb_supported_protocol_capability_t* cap,
    volatile uint32_t* ptr
) {
    cap->dword0 = ptr[0];
    cap->dword1 = ptr[1];
    cap->dword2 = ptr[2];
    cap->dword3 = ptr[3];
}

#endif