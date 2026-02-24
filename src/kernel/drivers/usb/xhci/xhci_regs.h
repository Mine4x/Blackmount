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

#endif // XHCI_REGS_H