#ifndef XHCI_H
#define XHCI_H
#include <drivers/pci/pci.h>
#include "xhci_trb.h"

#define XHCI_MOD "xHCI"

static void _parse_capability_registers(void);
static void _log_capability_registers(void);
static void _log_operational_registers(void);
static int  _start_host_controller(void);
static void _log_usbsts(void);
static int  _reset_host_controller(void);
static pci_device_t* get_hc(void);
static void _setup_dcbaa(void);
static void _configure_operational_registers(void);
static void _acknowledge_irq(uint8_t interrupter);
static void _xhci_irq_handler(void);
static void _configure_runtime_registers(void);
static void _process_events(void);
static xhci_command_completion_trb_t* _send_command_trb(xhci_trb_t* cmd_trb, uint32_t timeout_ms);

int xhci_init_device();
int xhci_start_device();
int xhci_stop_device();

#endif // XHCI_H