#include "xhci_rings.h"
#include "xhci_common.h"
#include <debug.h>

size_t      m_max_trb_count;     // Number of valid TRBs in the ring including the LINK_TRB
size_t      m_enqueue_ptr;       // Index in the ring where to enqueue next TRB
xhci_trb_t* m_trbs;              // Base address of the ring buffer
uintptr_t   m_physical_base;     // Physical base of the ring
uint8_t     m_rcs_bit;           // Ring cycle state


volatile xhci_interrupter_registers_t* e_interrupter_regs;
size_t             e_segment_trb_count;
xhci_trb_t*        e_trbs;
uintptr_t          e_physical_base;
xhci_erst_entry_t* e_seg_table;
uint64_t           e_dequeue_ptr;
uint8_t            e_rcs_bit;


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



void _update_erdp() {
    uint64_t dequeue_address = e_physical_base + (e_dequeue_ptr * sizeof(xhci_trb_t));
    e_interrupter_regs->erdp = dequeue_address;
}

int xhci_event_ring_has_unprocessed_events() {
    return (e_trbs[e_dequeue_ptr].cycle_bit == e_rcs_bit);
}

xhci_trb_t* _dequeue_trb() {
    if (e_trbs[e_dequeue_ptr].cycle_bit != e_rcs_bit) {
        log_err("xHCI-rings", "Event Ring attempted to dequeue an invalid TRB, returning nullptr!");
        return NULL;
    }

    xhci_trb_t* ret = &e_trbs[e_dequeue_ptr];

    if (++e_dequeue_ptr == e_segment_trb_count) {
        e_dequeue_ptr = 0;
        e_rcs_bit = !e_rcs_bit;
    }

    return ret;
}

void xhci_event_ring_dequeue(xhci_trb_t** buffer, size_t* count, size_t max_count) {
    *count = 0;

    while (xhci_event_ring_has_unprocessed_events()) {
        if (*count >= max_count) {
            break;
        }

        xhci_trb_t* trb = _dequeue_trb();
        if (!trb) {
            break;
        }

        buffer[*count] = trb;
        (*count)++;
    }

    _update_erdp();

    uint64_t erdp = e_interrupter_regs->erdp;
    erdp |= XHCI_ERDP_EHB;
    e_interrupter_regs->erdp = erdp;
}

void xhci_event_ring_flush() {
    while (xhci_event_ring_has_unprocessed_events()) {
        if (!_dequeue_trb()) {
            break;
        }
    }

    _update_erdp();

    uint64_t erdp = e_interrupter_regs->erdp;
    erdp |= XHCI_ERDP_EHB;
    e_interrupter_regs->erdp = erdp;
}

void xhci_event_ring_init(size_t max_trbs, volatile xhci_interrupter_registers_t* interrupter) {
    e_interrupter_regs = interrupter;
    e_segment_trb_count = max_trbs;
    e_rcs_bit = XHCI_CRCR_RING_CYCLE_STATE;
    e_dequeue_ptr = 0;

    const uint64_t segment_count = 1;
    const uint64_t segment_size = max_trbs * sizeof(xhci_trb_t);
    const uint64_t segment_table_size = segment_count * sizeof(xhci_erst_entry_t);

    e_trbs = (xhci_trb_t*)alloc_xhci_memory(
        segment_size,
        XHCI_EVENT_RING_SEGMENTS_ALIGNMENT,
        XHCI_EVENT_RING_SEGMENTS_BOUNDARY
    );

    e_physical_base = xhci_get_physical_addr(e_trbs);

    e_seg_table = (xhci_erst_entry_t*)alloc_xhci_memory(
        segment_table_size,
        XHCI_EVENT_RING_SEGMENT_TABLE_ALIGNMENT,
        XHCI_EVENT_RING_SEGMENT_TABLE_BOUNDARY
    );

    xhci_erst_entry_t entry;
    entry.ring_segment_base_address = e_physical_base;
    entry.ring_segment_size = e_segment_trb_count;
    entry.rsvd = 0;

    e_seg_table[0] = entry;

    e_interrupter_regs->erstsz = 1;

    _update_erdp();

    e_interrupter_regs->erstba = xhci_get_physical_addr(e_seg_table);
}

xhci_trb_t* xhci_event_ring_get_virtual_base() {
    return e_trbs;
}

uintptr_t xhci_event_ring_get_physical_base() {
    return e_physical_base;
}

uint8_t xhci_event_ring_get_cycle_bit() {
    return e_rcs_bit;
}