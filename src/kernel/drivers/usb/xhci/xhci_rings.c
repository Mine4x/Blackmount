#include "xhci_rings.h"
#include "xhci_common.h"

size_t      m_max_trb_count;     // Number of valid TRBs in the ring including the LINK_TRB
size_t      m_enqueue_ptr;       // Index in the ring where to enqueue next TRB
xhci_trb_t* m_trbs;              // Base address of the ring buffer
uintptr_t   m_physical_base;     // Physical base of the ring
uint8_t     m_rcs_bit;           // Ring cycle state

void xhci_command_ring_init(size_t max_trbs) {
    m_max_trb_count = max_trbs;
    m_rcs_bit = 1;
    m_enqueue_ptr = 0;

    const uint64_t ring_size = max_trbs * sizeof(xhci_trb_t);

    m_trbs = alloc_xhci_memory(
        ring_size,
        XHCI_COMMAND_RING_SEGMENTS_ALIGNMENT,
        XHCI_COMMAND_RING_SEGMENTS_BOUNDARY
    );

    m_physical_base = xhci_get_physical_addr(m_trbs);

    m_trbs[m_max_trb_count - 1].parameter = m_physical_base;
    m_trbs[m_max_trb_count - 1].control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_LINK_TRB_TC_BIT | m_rcs_bit;
}

void xhci_command_ring_enqueue(xhci_trb_t* trb) {
    trb->cycle_bit = m_rcs_bit;

    m_trbs[m_enqueue_ptr] = *trb;

    if (++m_enqueue_ptr == m_max_trb_count - 1) {
        m_trbs[m_max_trb_count - 1].control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_LINK_TRB_TC_BIT | m_rcs_bit;

        m_enqueue_ptr = 0;
        m_rcs_bit = !m_rcs_bit;
    }
}

  xhci_trb_t* xhci_command_ring_get_virtual_base() {
    return m_trbs;
}

  uintptr_t xhci_command_ring_get_physical_base() {
    return m_physical_base;
}

  uint8_t xhci_command_ring_get_cycle_bit() {
    return m_rcs_bit;
}