#ifndef XHCI_REGS_H
#define XHCI_REGS_H

#include <stdint.h>
#include <stddef.h>

// Capability Registers
typedef struct {
    volatile const uint8_t  caplength;
    volatile const uint8_t  reserved0;
    volatile const uint16_t hciversion;
    volatile const uint32_t hcsparams1;
    volatile const uint32_t hcsparams2;
    volatile const uint32_t hcsparams3;
    volatile const uint32_t hccparams1;
    volatile const uint32_t dboff;
    volatile const uint32_t rtsoff;
    volatile const uint32_t hccparams2;
} xhci_capability_registers_t;

_Static_assert(sizeof(xhci_capability_registers_t) == 32, 
               "xhci_capability_registers_t must be 32 bytes");

// Operational Registers
typedef struct {
    volatile uint32_t usbcmd;
    volatile uint32_t usbsts;
    volatile uint32_t pagesize;
    volatile uint32_t reserved0[2];
    volatile uint32_t dnctrl;
    volatile uint64_t crcr;
    volatile uint32_t reserved1[4];
    volatile uint64_t dcbaap;
    volatile uint32_t config;
    volatile uint32_t reserved2[49];
} xhci_operational_registers_t;

_Static_assert(sizeof(xhci_operational_registers_t) == 256, 
               "xhci_operational_registers_t must be 256 bytes");

typedef struct {
    uint32_t iman;         // Interrupter Management
    uint32_t imod;         // Interrupter Moderation
    uint32_t erstsz;       // Event Ring Segment Table Size
    uint32_t rsvd;         // Reserved
    uint64_t erstba;       // Event Ring Segment Table Base Address
    union {
        struct {
            // This index is used to accelerate the checking of
            // an Event Ring Full condition. This field can be 0.
            uint64_t dequeue_erst_segment_index : 3;

            // This bit is set by the controller when it sets the
            // Interrupt Pending bit. Then once your handler is finished
            // handling the event ring, you clear it by writing a '1' to it.
            uint64_t event_handler_busy         : 1;

            // Physical address of the _next_ item in the event ring
            uint64_t event_ring_dequeue_pointer : 60;
        };
        uint64_t erdp;     // Event Ring Dequeue Pointer (offset 18h)
    };
} xhci_interrupter_registers_t;

typedef struct {
    uint32_t mf_index;                          // Microframe Index (offset 0000h)
    uint32_t rsvdz[7];                          // Reserved (offset 001Fh:0004h)
    xhci_interrupter_registers_t ir[1024];      // Interrupter Register Sets (offset 0020h to 8000h)
} xhci_runtime_registers_t;

typedef struct {
    union {
        struct {
            uint8_t     db_target;
            uint8_t     rsvd;
            uint16_t    db_stream_id;
        };

        uint32_t raw;
    };
} __attribute__((packed)) xhci_doorbell_register_t;

void xhci_doorbell_manager_init(uintptr_t base);

void xhci_doorbell_manager_ring_doorbell(uint8_t doorbell, uint8_t target);

void xhci_doorbell_manager_ring_command_doorbell();
void xhci_doorbell_manager_ring_control_endpoint_doorbell(uint8_t doorbell);

#include <stdint.h>

typedef struct xhci_extended_capability_entry {
    union {
        struct {
            uint8_t id;
            uint8_t next;

            uint16_t cap_specific;
        };

        uint32_t raw;
    };
} xhci_extended_capability_entry_t;
_Static_assert(sizeof(xhci_extended_capability_entry_t) == 4,
               "xhci_extended_capability_entry_t must be 4 bytes");

typedef struct xhci_portsc_register {
    union {
        struct {
            // Current connect status (RO), if PP is 0, this bit is also 0
            uint32_t    ccs         : 1;

            // Port Enable/Disable (R/WC), if PP is 0, this bit is also 0
            uint32_t    ped         : 1;

            // Reserved and zeroed
            uint32_t    rsvd0       : 1;
            
            // Over-current active (RO)
            uint32_t    oca         : 1;

            // Port reset (R/W), if PP is 0, this bit is also 0
            uint32_t    pr          : 1;

            // Port link state (R/W), if PP is 0, this bit is also 0
            uint32_t    pls         : 4;

            // Port power (R/W)
            uint32_t    pp          : 1;

            // Port speed (RO)
            uint32_t    port_speed  : 4;

            // Port indicator control (R/W), if PP is 0, this bit is also 0
            uint32_t    pic         : 2;

            // Port link state write strobe (R/W), if PP is 0, this bit is also 0
            uint32_t    lws         : 1;

            // Connect status change (R/WC), if PP is 0, this bit is also 0.
            // ** When transitioning from 0 to a 1, will trigger a Port Status Change Event.
            // ** Clear this bit by writing a '1' to it.
            uint32_t    csc         : 1;

            /*
            Port enable/disable change (R/WC), if PP is 0, this bit is also 0.
            ** When transitioning from 0 to a 1, will trigger a Port Status Change Event.
            ** For a USB2 protocol port, this bit shall be set to ‘1’ only when the port is disabled (EOF2)
            ** For a USB3 protocol port, this bit shall never be set to ‘1’
            ** Software shall clear this bit by writing a ‘1’ to it. Refer to section 4.19.2
            */
            uint32_t    pec         : 1;

            // Warm port reset change (R/WC), if PP is 0, this bit is also 0.
            // ** When transitioning from 0 to a 1, will trigger a Port Status Change Event.
            // ** Reserved and zeroed on USB2 ports.
            // ** Software shall clear this bit by writing a '1' to it.
            uint32_t    wrc         : 1;

            // Over-current change (R/WC), if PP is 0, this bit is also 0.
            // ** When transitioning from 0 to a 1, will trigger a Port Status Change Event.
            // ** Software shall clear this bit by writing a '1' to it.
            uint32_t    occ         : 1;

            // Port reset change (R/WC), if PP is 0, this bit is also 0.
            // ** When transitioning from 0 to a 1, will trigger a Port Status Change Event.
            // ** Software shall clear this bit by writing a '1' to it.
            uint32_t    prc         : 1;

            // Port link state change (R/WC), if PP is 0, this bit is also 0.
            // ** When transitioning from 0 to a 1, will trigger a Port Status Change Event.
            uint32_t    plc         : 1;

            // Port config error change (R/WC), if PP is 0, this bit is also 0.
            // ** When transitioning from 0 to a 1, will trigger a Port Status Change Event.
            // ** Reserved and zeroed on USB2 ports.
            // ** Software shall clear this bit by writing a '1' to it.
            uint32_t    cec         : 1;

            // Cold attach status (R/O), if PP is 0, this bit is also 0.
            uint32_t    cas         : 1;

            // Wake on connect enable (R/W)
            uint32_t    wce         : 1;

            // Wake on disconnect enable (R/W)
            uint32_t    wde         : 1;

            // Wake on over-current enable (R/W)
            uint32_t    woe         : 1;

            // Reserved and zeroed
            uint32_t    rsvd1        : 2;

            // Device removable (RO)
            uint32_t    dr          : 1;

            // Warm port reset (R/WC).
            // ** Reserved and zeroed on USB2 ports.
            uint32_t    wpr         : 1;
        } __attribute__((packed));

        // Must be accessed using 32-bit dwords
        uint32_t raw;
    };
} __attribute__((packed)) xhci_portsc_register_t;
_Static_assert(sizeof(xhci_portsc_register_t) == sizeof(uint32_t),
               "xhci_portsc_register must be 4 bytes");;

#define XHCI_NEXT_EXT_CAP_PTR(ptr, next) (volatile uint32_t*)((char*)ptr + (next * sizeof(uint32_t)))

typedef enum {
    XHCI_EXT_CAP_RESERVED                           = 0,
    XHCI_EXT_CAP_USB_LEGACY_SUPPORT                 = 1,
    XHCI_EXT_CAP_SUPPORTED_PROTOCOL                 = 2,
    XHCI_EXT_CAP_EXTENDED_POWER_MANAGEMENT          = 3,
    XHCI_EXT_CAP_IOVIRTUALIZATION_SUPPORT           = 4,
    XHCI_EXT_CAP_MESSAGE_INTERRUPT_SUPPORT          = 5,
    XHCI_EXT_CAP_LOCAL_MEMORY_SUPPORT               = 6,
    XHCI_EXT_CAP_USB_DEBUG_CAPABILITY_SUPPORT       = 10,
    XHCI_EXT_CAP_EXTENDED_MESSAGE_INTERRUPT_SUPPORT = 17
} xhci_extended_capability_code_t;


typedef struct xhci_extended_capability xhci_extended_capability_t;

struct xhci_extended_capability {
    volatile uint32_t* m_base;
    xhci_extended_capability_entry_t m_entry;

    xhci_extended_capability_t* m_next;
};

void xhci_extended_capability_init(
    xhci_extended_capability_t* cap,
    volatile uint32_t* xhc_base,
    volatile uint32_t* cap_ptr
);

static inline volatile uint32_t*
xhci_extended_capability_base(const xhci_extended_capability_t* cap)
{
    return cap->m_base;
}

static inline xhci_extended_capability_code_t
xhci_extended_capability_id(const xhci_extended_capability_t* cap)
{
    return (xhci_extended_capability_code_t)cap->m_entry.id;
}

static inline xhci_extended_capability_t*
xhci_extended_capability_next(const xhci_extended_capability_t* cap)
{
    return cap->m_next;
}

void xhci_extended_capability_read_next_ext_caps(
    xhci_extended_capability_t* cap
);


#endif // XHCI_REGS_H