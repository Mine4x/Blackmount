#ifndef XHCI_RINGS_H
#define XHCI_RINGS_H

#include <stddef.h>
#include <stdint.h>

#include "xhci_mem.h"
#include "xhci_trb.h"

void xhci_command_ring_init(size_t max_trbs);

 xhci_trb_t* xhci_command_ring_get_virtual_base();

 uintptr_t xhci_command_ring_get_physical_base();

 uint8_t xhci_command_ring_get_cycle_bit();

void xhci_command_ring_enqueue(xhci_trb_t* trb);

#endif // XHCI_RINGS_H