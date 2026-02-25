#ifndef XHCI_RINGS_H
#define XHCI_RINGS_H

#include <stddef.h>
#include <stdint.h>

#include "xhci_mem.h"
#include "xhci_trb.h"
#include "xhci_regs.h"

typedef struct {
    uint64_t ring_segment_base_address; // Base address of the Event Ring segment
    uint32_t ring_segment_size;         // Size of the Event Ring segment (only low 16 bits are used)
    uint32_t rsvd;
} xhci_erst_entry_t __attribute__((packed));

void xhci_command_ring_init(size_t max_trbs);

xhci_trb_t* xhci_command_ring_get_virtual_base();

uintptr_t xhci_command_ring_get_physical_base();

uint8_t xhci_command_ring_get_cycle_bit();

void xhci_command_ring_enqueue(xhci_trb_t* trb);


void xhci_event_ring_init(size_t max_trbs, volatile xhci_interrupter_registers_t* interrupter);

xhci_trb_t* xhci_event_ring_get_virtual_base();

uintptr_t xhci_event_ring_get_physical_base();

uint8_t xhci_event_ring_get_cycle_bit();

int xhci_event_ring_has_unprocessed_events();
void xhci_event_ring_dequeue(xhci_trb_t** buffer, size_t* count, size_t max_count);
void xhci_event_ring_flush();

#endif // XHCI_RINGS_H