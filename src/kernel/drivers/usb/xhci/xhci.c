#include "xhci.h"
#include "xhci_common.h"
#include <mem/dma.h>
#include <debug.h>
#include <drivers/pci/pci.h>
#include "xhci_mem.h"
#include "xhci_regs.h"
#include <timer/timer.h>
#include <heap.h>
#include "xhci_rings.h"

void* xhc_block;
pci_device_t* xhc_dev;
uintptr_t xhc_base;

volatile xhci_capability_registers_t* m_cap_regs;
volatile xhci_operational_registers_t* m_op_regs;
volatile xhci_runtime_registers_t* m_runtime_regs;

uint8_t m_capability_regs_length;
    
// HCSPARAMS1
uint8_t m_max_device_slots;
uint8_t m_max_interrupters;
uint8_t m_max_ports;

// HCSPARAMS2
uint8_t m_isochronous_scheduling_threshold;
uint8_t m_erst_max;
uint8_t m_max_scratchpad_buffers;

// hccparams1
bool m_64bit_addressing_capability;
bool m_bandwidth_negotiation_capability;
bool m_64byte_context_size;
bool m_port_power_control;
bool m_port_indicators;
bool m_light_reset_capability;
uint32_t m_extended_capabilities_offset;

uint64_t* m_dcbaa;
uint64_t* m_dcbaa_virt;

static void _parse_capability_registers() {
    m_cap_regs = (volatile xhci_capability_registers_t*)xhc_base;

    m_capability_regs_length = m_cap_regs->caplength;

    m_max_device_slots = XHCI_MAX_DEVICE_SLOTS(m_cap_regs);
    m_max_interrupters = XHCI_MAX_INTERRUPTERS(m_cap_regs);
    m_max_ports = XHCI_MAX_PORTS(m_cap_regs);

    m_isochronous_scheduling_threshold = XHCI_IST(m_cap_regs);
    m_erst_max = XHCI_ERST_MAX(m_cap_regs);
    m_max_scratchpad_buffers = XHCI_MAX_SCRATCHPAD_BUFFERS(m_cap_regs);

    m_64bit_addressing_capability = XHCI_AC64(m_cap_regs);
    m_bandwidth_negotiation_capability = XHCI_BNC(m_cap_regs);
    m_64byte_context_size = XHCI_CSZ(m_cap_regs);
    m_port_power_control = XHCI_PPC(m_cap_regs);
    m_port_indicators = XHCI_PIND(m_cap_regs);
    m_light_reset_capability = XHCI_LHRC(m_cap_regs);
    m_extended_capabilities_offset = XHCI_XECP(m_cap_regs) * sizeof(uint32_t);

    m_op_regs = (volatile xhci_operational_registers_t*)((uintptr_t)xhc_base + m_capability_regs_length);

    m_runtime_regs = (volatile xhci_runtime_registers_t*)(xhc_base + m_cap_regs->rtsoff);
}

static void _log_capability_registers() {
    log_debug(XHCI_MOD, "===== Xhci Capability Registers (0x%llx) =====", (uint64_t)m_cap_regs);
    log_debug(XHCI_MOD, "    Length                : %i", m_capability_regs_length);
    log_debug(XHCI_MOD, "    Max Device Slots      : %i", m_max_device_slots);
    log_debug(XHCI_MOD, "    Max Interrupters      : %i", m_max_interrupters);
    log_debug(XHCI_MOD, "    Max Ports             : %i", m_max_ports);
    log_debug(XHCI_MOD, "    IST                   : %i", m_isochronous_scheduling_threshold);
    log_debug(XHCI_MOD, "    ERST Max Size         : %i", m_erst_max);
    log_debug(XHCI_MOD, "    Scratchpad Buffers    : %i", m_max_scratchpad_buffers);
    log_debug(XHCI_MOD, "    64-bit Addressing     : %s", m_64bit_addressing_capability ? "yes" : "no");
    log_debug(XHCI_MOD, "    Bandwidth Negotiation : %i", m_bandwidth_negotiation_capability);
    log_debug(XHCI_MOD, "    64-byte Context Size  : %s", m_64byte_context_size ? "yes" : "no");
    log_debug(XHCI_MOD, "    Port Power Control    : %i", m_port_power_control);
    log_debug(XHCI_MOD, "    Port Indicators       : %i", m_port_indicators);
    log_debug(XHCI_MOD, "    Light Reset Available : %i", m_light_reset_capability);
    log_debug(XHCI_MOD, "");
}


static void _log_operational_registers() {
    log_debug(XHCI_MOD, "===== Xhci Operational Registers (0x%llx) =====", (uint64_t)m_op_regs);
    log_debug(XHCI_MOD, "    usbcmd     : 0x%x", m_op_regs->usbcmd);
    log_debug(XHCI_MOD, "    usbsts     : 0x%x", m_op_regs->usbsts);
    log_debug(XHCI_MOD, "    pagesize   : 0x%x", m_op_regs->pagesize);
    log_debug(XHCI_MOD, "    dnctrl     : 0x%x", m_op_regs->dnctrl);
    log_debug(XHCI_MOD, "    crcr       : 0x%llx", m_op_regs->crcr);
    log_debug(XHCI_MOD, "    dcbaap     : 0x%llx", m_op_regs->dcbaap);
    log_debug(XHCI_MOD, "    config     : 0x%x", m_op_regs->config);
    log_debug(XHCI_MOD, "");
}

static int _start_host_controller() {
    uint32_t usbcmd = m_op_regs->usbcmd;
    usbcmd |= XHCI_USBCMD_RUN_STOP;
    usbcmd |= XHCI_USBCMD_INTERRUPTER_ENABLE;
    m_op_regs->usbcmd = usbcmd;

    const int max_retries = 1000;
    int retries = 0;

    while (m_op_regs->usbsts & XHCI_USBSTS_HCH) {
        if (retries++ >= max_retries) {
            log_err(XHCI_MOD, "Controller failed to start: timeout after %d tries", retries);
            return -1;
        }

        timer_sleep_ms(1);
    }

    if (m_op_regs->usbsts & XHCI_USBSTS_CNR) {
        log_err(XHCI_MOD, "Controller failed to start: controller not ready");
        return -2;
    }

    return 0;
}

void _log_usbsts() {
    uint32_t status = m_op_regs->usbsts;
    log_debug(XHCI_MOD, "===== USBSTS =====");
    if (status & XHCI_USBSTS_HCH)  log_debug(XHCI_MOD, "    Host Controlled Halted");
    if (status & XHCI_USBSTS_HSE)  log_debug(XHCI_MOD, "    Host System Error");
    if (status & XHCI_USBSTS_EINT) log_debug(XHCI_MOD, "    Event Interrupt");
    if (status & XHCI_USBSTS_PCD)  log_debug(XHCI_MOD, "    Port Change Detect");
    if (status & XHCI_USBSTS_SSS)  log_debug(XHCI_MOD, "    Save State Status");
    if (status & XHCI_USBSTS_RSS)  log_debug(XHCI_MOD, "    Restore State Status");
    if (status & XHCI_USBSTS_SRE)  log_debug(XHCI_MOD, "    Save/Restore Error");
    if (status & XHCI_USBSTS_CNR)  log_debug(XHCI_MOD, "    Controller Not Ready");
    if (status & XHCI_USBSTS_HCE)  log_debug(XHCI_MOD, "    Host Controller Error");
    log_debug(XHCI_MOD, "");
}

static int _reset_host_controller() {
    uint32_t usbcmd = m_op_regs->usbcmd;
    usbcmd &= ~XHCI_USBCMD_RUN_STOP;
    m_op_regs->usbcmd = usbcmd;

    uint32_t timeout = 200;
    while (!(m_op_regs->usbsts & XHCI_USBSTS_HCH)) {
        if (--timeout <= 0) {
            log_err(XHCI_MOD, "HC did not halt withing %ums", timeout);
            return -1;
        }

        timer_sleep_ms(1);
    }

    usbcmd = m_op_regs->usbcmd;
    usbcmd |= XHCI_USBCMD_HCRESET;
    usbcmd = m_op_regs->usbcmd = usbcmd;

    timeout = 1000;
    while (
        m_op_regs->usbcmd & XHCI_USBCMD_HCRESET ||
        m_op_regs->usbsts & XHCI_USBSTS_CNR
    ) {
        if (--timeout <= 0) {
            log_err(XHCI_MOD, "HC did not reset withing %ums", timeout);
            return -1;
        }

        timer_sleep_ms(1);
    }
    
    timer_sleep_ms(50);

    if (m_op_regs->usbcmd != 0)
        return -1;
    if (m_op_regs->crcr != 0)
        return -1;
    if (m_op_regs->dcbaap != 0)
        return -1;
    if (m_op_regs->config != 0)
        return -1;
    if (m_op_regs->dnctrl != 0)
        return -1;

    return 0;
}

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

static void _setup_dcbaa() {
    size_t dcbaa_size = sizeof(uintptr_t) * (m_max_device_slots + 1);

    void* dcbaa_memblock = alloc_xhci_memory(dcbaa_size, XHCI_DEVICE_CONTEXT_ALIGNMENT, XHCI_DEVICE_CONTEXT_BOUNDARY);

    m_dcbaa = (uint64_t*)dcbaa_memblock;

    m_dcbaa_virt = kmalloc((m_max_device_slots +1) * sizeof(uint64_t));

    if (m_max_scratchpad_buffers > 0) {
        void* sp_memblock = alloc_xhci_memory(m_max_scratchpad_buffers * sizeof(uint64_t), XHCI_DEVICE_CONTEXT_ALIGNMENT, XHCI_DEVICE_CONTEXT_BOUNDARY);

        uint64_t* scratchpad_array = (uint64_t*)sp_memblock;

        for (uint8_t i = 0; i < m_max_scratchpad_buffers; i ++) {
            void* scratchpad = alloc_xhci_memory(PAGE_SIZE, XHCI_SCRATCHPAD_BUFFERS_ALIGNMENT, XHCI_SCRATCHPAD_BUFFER_ARRAY_BOUNDARY);

            uint64_t scratchpad_paddr = xhci_get_physical_addr(scratchpad);
            scratchpad_array[i] = scratchpad_paddr;
        }

        uint64_t sp_array_pbase = xhci_get_physical_addr(scratchpad_array);

        m_dcbaa[0] = sp_array_pbase;

        m_dcbaa_virt[0] = (uint64_t)scratchpad_array;
    }

    m_op_regs->dcbaap = xhci_get_physical_addr(m_dcbaa);
}

static void _configure_operational_registers() {
    m_op_regs->dnctrl = 0xffff;

    m_op_regs->config = (uint32_t)m_max_device_slots;

    _setup_dcbaa();

    xhci_command_ring_init(XHCI_COMMAND_RING_TRB_COUNT);

    m_op_regs->crcr = xhci_command_ring_get_physical_base() | xhci_command_ring_get_cycle_bit();
}

static void _acknowledge_irq(uint8_t interrupter) {
    volatile xhci_interrupter_registers_t* interrupter_regs = &m_runtime_regs->ir[interrupter];

    uint32_t iman = interrupter_regs->iman;
    iman |= XHCI_IMAN_INTERRUPT_PENDING;
    interrupter_regs->iman = iman;
}

static void _configure_runtime_registers() {
    m_op_regs->usbsts = XHCI_USBSTS_EINT;
    
    volatile xhci_interrupter_registers_t* interrupt_regs = &m_runtime_regs->ir[0];

    uint32_t iman = interrupt_regs->iman;
    iman |= XHCI_IMAN_INTERRUPT_ENABLE;
    interrupt_regs->iman = iman;

    xhci_event_ring_init(XHCI_EVENT_RING_TRB_COUNT, interrupt_regs);

    log_debug(XHCI_MOD, "ERSTSZ  : 0x%llx", interrupt_regs->erstsz);
    log_debug(XHCI_MOD, "ERSTBA  : 0x%llx", interrupt_regs->erstba);
    log_debug(XHCI_MOD, "ERDP    : 0x%llx", interrupt_regs->erdp);

    _acknowledge_irq(0);
}

int xhci_init_device() {
    log_info(XHCI_MOD, "xHCI init!");

    pci_device_t* hc = get_hc();
    if (!hc) {
        return -1;
    }
    xhc_dev = hc;

    pci_map_bar(xhc_dev, 0);
    pci_bar_t bar = xhc_dev->bars[0];
    xhc_base = bar.virt_base;

    log_debug(XHCI_MOD, "xHCI vadrr : %0x%llx", xhc_base);
    log_debug(XHCI_MOD, "xHCI padrr : %0x%llx", xhci_get_physical_addr((void*)xhc_base));

    _parse_capability_registers();
    _log_capability_registers();

    if (_reset_host_controller() < 0) {
        log_err(XHCI_MOD, "Unable to reset host controller");
        return -2;
    }
    _configure_operational_registers();
    _log_operational_registers();

    _configure_runtime_registers();

    return 0;
}

int xhci_start_device() {
    _log_usbsts();

    if (_start_host_controller() < 0) {
        return -1;
    }

    log_ok(XHCI_MOD, "Controller Started!");

    _log_usbsts();

    return 0;
}

int xhci_stop_device() {
    return 0;
}