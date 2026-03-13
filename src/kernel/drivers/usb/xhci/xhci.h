#ifndef XHCI_H
#define XHCI_H
#include <drivers/pci/pci.h>
#include "xhci_trb.h"
#include "xhci_regs.h"
#include "xhci_device.h"
#include <util/vector.h>
#include <stdbool.h>

#define XHCI_MOD "xHCI"
#define XHCI_MAX_CONTROLLERS 8

typedef struct {
    pci_device_t*                          pci_dev;
    uintptr_t                              base;

    volatile xhci_capability_registers_t*  cap_regs;
    volatile xhci_operational_registers_t* op_regs;
    volatile xhci_runtime_registers_t*     runtime_regs;

    uint8_t  cap_length;
    uint8_t  max_device_slots;
    uint8_t  max_interrupters;
    uint8_t  max_ports;
    uint8_t  ist;
    uint8_t  erst_max;
    uint8_t  max_scratchpad_buffers;

    bool     ac64;
    bool     bnc;
    bool     csz;
    bool     ppc;
    bool     pind;
    bool     lhrc;

    uint32_t ext_caps_offset;

    uint64_t* dcbaa;
    uint64_t* dcbaa_virt;

    volatile uint8_t       cmd_irq_completed;
    vector                 cmd_completion_events;
    vector                 usb3_ports;

    xhci_extended_capability_t ext_cap_head;

    xhci_device_t devices[XHCI_MAX_DEVICES];
} xhci_controller_t;

int xhci_init_device();
int xhci_start_device();
int xhci_stop_device();

int xhci_control_transfer(xhci_controller_t* hc, xhci_device_t* dev,
                           usb_setup_packet_t* setup,
                           void* data_buf, uint16_t data_len,
                           bool dir_in, uint32_t timeout_ms);

int xhci_configure_endpoint(xhci_controller_t* hc, xhci_device_t* dev,
                             uint8_t ep_addr, uint8_t ep_attrs,
                             uint16_t max_pkt_size, uint8_t interval);

int xhci_queue_transfer(xhci_controller_t* hc, xhci_device_t* dev,
                        uint8_t ep_addr, void* buf, uint16_t len);

int xhci_wait_transfer(xhci_device_t* dev, uint8_t ep_addr,
                       uint32_t timeout_ms);

#define XHCI_MAX_PROBE_CBS 8

typedef void (*xhci_device_probe_cb_t)(xhci_controller_t* hc,
                                        xhci_device_t*     dev);

void xhci_register_probe_callback(xhci_device_probe_cb_t cb);

#endif // XHCI_H