#include "xhci_device.h"
#include "xhci_common.h"
#include "xhci_mem.h"
#include <memory.h>

void xhci_transfer_ring_init(xhci_transfer_ring_t* ring, size_t max_trbs)
{
    ring->max_trb_count = max_trbs;
    ring->cycle_bit     = 1;
    ring->enqueue_ptr   = 0;

    const size_t ring_size = max_trbs * sizeof(xhci_trb_t);

    ring->trbs = (xhci_trb_t*)alloc_xhci_memory(
        ring_size,
        XHCI_TRANSFER_RING_SEGMENTS_ALIGNMENT,
        XHCI_TRANSFER_RING_SEGMENTS_BOUNDARY
    );
    memset(ring->trbs, 0, ring_size);

    ring->physical_base = xhci_get_physical_addr(ring->trbs);

    
    xhci_trb_t* link = &ring->trbs[max_trbs - 1];
    link->parameter   = ring->physical_base;
    link->status      = 0;
    link->control     = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT)
                      | XHCI_LINK_TRB_TC_BIT
                      | ring->cycle_bit;
}

void xhci_transfer_ring_enqueue(xhci_transfer_ring_t* ring, xhci_trb_t* trb)
{
    trb->cycle_bit            = ring->cycle_bit;
    ring->trbs[ring->enqueue_ptr] = *trb;

    if (++ring->enqueue_ptr == ring->max_trb_count - 1) {
        
        ring->trbs[ring->max_trb_count - 1].control =
            (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT)
            | XHCI_LINK_TRB_TC_BIT
            | ring->cycle_bit;

        ring->enqueue_ptr = 0;
        ring->cycle_bit   = !ring->cycle_bit;
    }
}