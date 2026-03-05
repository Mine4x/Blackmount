#include "xhci_regs.h"
#include "xhci_common.h"

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