#ifndef XHCI_H
#define XHCI_H

#define XHCI_MOD "xHCI"

int xhci_init_device();
int xhci_start_device();
int xhci_stop_device();

#endif // XHCI_H