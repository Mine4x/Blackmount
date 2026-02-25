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

#endif // XHCI_REGS_H