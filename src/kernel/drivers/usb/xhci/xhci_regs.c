#include "xhci_regs.h"
#include "xhci_common.h"
#include <heap.h>

xhci_doorbell_register_t* m_doorbell_registers;

void xhci_doorbell_manager_init(uintptr_t base) {
    m_doorbell_registers = (xhci_doorbell_register_t*)base;
}

void xhci_doorbell_manager_ring_doorbell(uint8_t doorbell, uint8_t target) {
    m_doorbell_registers[doorbell].raw = (uint32_t)target;
}

void xhci_doorbell_manager_ring_command_doorbell() {
    xhci_doorbell_manager_ring_doorbell(0, XHCI_DOORBELL_TARGET_COMMAND_RING);
}

void xhci_doorbell_manager_ring_control_endpoint_doorbell(uint8_t doorbell) {
    xhci_doorbell_manager_ring_doorbell(doorbell, XHCI_DOORBELL_TARGET_CONTROL_EP_RING);
}

void xhci_extended_capability_init(
    xhci_extended_capability_t* cap,
    volatile uint32_t* xhc_base,
    volatile uint32_t* cap_ptr
)
{
    cap->m_base = cap_ptr;
    cap->m_entry.raw = *cap_ptr;
    cap->m_next = NULL;

    xhci_extended_capability_read_next_ext_caps(cap);
}

void xhci_extended_capability_read_next_ext_caps(
    xhci_extended_capability_t* cap
)
{
    if (cap->m_entry.next) {

        volatile uint32_t* next_cap_ptr =
            cap->m_base + cap->m_entry.next;

        cap->m_next = kmalloc(sizeof(xhci_extended_capability_t));
        if (!cap->m_next) return;

        xhci_extended_capability_init(cap->m_next, NULL, next_cap_ptr);
    }
}