#ifndef XHCI_H
#define XHCI_H
#include <drivers/pci/pci.h>
#include "xhci_trb.h"
#include "xhci_regs.h"

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
static void _parse_extended_capabilites();
static bool _is_usb3_port(uint8_t port_num);
static xhci_command_completion_trb_t* _send_command_trb(xhci_trb_t* cmd_trb, uint32_t timeout_ms);
static xhci_portsc_register_t _read_portsc_reg(uint8_t port);
static void _write_portsc_reg(xhci_portsc_register_t reg, uint8_t port);
static int _reset_port(uint8_t port);
static const char* _usb_speed_to_string(uint8_t speed);

int xhci_init_device();
int xhci_start_device();
int xhci_stop_device();

#endif // XHCI_H