#include "xhci.h"
#include "xhci_common.h"
#include <mem/dma.h>
#include <debug.h>
#include <drivers/pci/pci.h>
#include "xhci_mem.h"

dma_buf_t* xhc_block;
pci_device_t* xhc_dev;

static pci_device_t* get_hc(void) {
    pci_device_t *pci = pci_get_devices();
    while (pci) {
        if (pci->class_code == 0x0C &&
            pci->subclass   == 0x03 &&
            pci->prog_if    == 0x30) break;
        pci = pci->next;
    }
    if (!pci) {
        log_err(XHCI_MOD, "No xHCI controller found in PCI device list");
        return NULL;
    }
    log_info(XHCI_MOD, "Found xHCI: %04x:%04x (bus %u slot %u fn %u)",
             pci->vendor_id, pci->device_id, pci->bus, pci->slot, pci->function);
    return pci;
}

int xhci_init_device() {
    log_info(XHCI_MOD, "xHCI init!");

    pci_device_t* hc = get_hc();
    if (!hc) {
        return;
    }
    xhc_dev = hc;

    pci_map_bar(xhc_dev, 0);
    pci_bar_t bar = xhc_dev->bars[0];
    uintptr_t tmp = xhci_map_mmio(bar.virt_base, bar.size);

    log_debug(XHCI_MOD, "xHCI vadrr : %0x%llx", tmp);
    log_debug(XHCI_MOD, "xHCI padrr : %0x%llx", xhci_get_physical_addr((void*)tmp));

    return 0;
}

int xhci_start_device() {
    return 0;
}

int xhci_stop_device() {
    return 0;
}