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
#include <memory.h>
#include <util/vector.h>
#include "xhci_ext_cap.h"

static xhci_controller_t  m_controllers[XHCI_MAX_CONTROLLERS];
static int                 m_controller_count = 0;

static void _parse_capability_registers(xhci_controller_t* hc);
static void _log_capability_registers(xhci_controller_t* hc);
static void _log_operational_registers(xhci_controller_t* hc);
static int  _start_host_controller(xhci_controller_t* hc);
static void _log_usbsts(xhci_controller_t* hc);
static int  _reset_host_controller(xhci_controller_t* hc);
static void _setup_dcbaa(xhci_controller_t* hc);
static void _configure_operational_registers(xhci_controller_t* hc);
static void _acknowledge_irq(xhci_controller_t* hc, uint8_t interrupter);
static void _configure_runtime_registers(xhci_controller_t* hc);
static void _process_events(xhci_controller_t* hc);
static void _parse_extended_capabilites(xhci_controller_t* hc);
static bool _is_usb3_port(xhci_controller_t* hc, uint8_t port_num);
static xhci_command_completion_trb_t* _send_command_trb(xhci_controller_t* hc, xhci_trb_t* cmd_trb, uint32_t timeout_ms);
static xhci_portsc_register_t _read_portsc_reg(xhci_controller_t* hc, uint8_t port);
static void _write_portsc_reg(xhci_controller_t* hc, xhci_portsc_register_t reg, uint8_t port);
static int  _reset_port(xhci_controller_t* hc, uint8_t port);
static void _take_bios_ownership(xhci_controller_t* hc);
static int  _init_controller(xhci_controller_t* hc, pci_device_t* pci_dev);
static int  _start_controller(xhci_controller_t* hc);
static void _stop_controller(xhci_controller_t* hc);
static const char* _usb_speed_to_string(uint8_t speed);

static void _xhci_irq_handler_0() { _process_events(&m_controllers[0]); _acknowledge_irq(&m_controllers[0], 0); }
static void _xhci_irq_handler_1() { _process_events(&m_controllers[1]); _acknowledge_irq(&m_controllers[1], 0); }
static void _xhci_irq_handler_2() { _process_events(&m_controllers[2]); _acknowledge_irq(&m_controllers[2], 0); }
static void _xhci_irq_handler_3() { _process_events(&m_controllers[3]); _acknowledge_irq(&m_controllers[3], 0); }
static void _xhci_irq_handler_4() { _process_events(&m_controllers[4]); _acknowledge_irq(&m_controllers[4], 0); }
static void _xhci_irq_handler_5() { _process_events(&m_controllers[5]); _acknowledge_irq(&m_controllers[5], 0); }
static void _xhci_irq_handler_6() { _process_events(&m_controllers[6]); _acknowledge_irq(&m_controllers[6], 0); }
static void _xhci_irq_handler_7() { _process_events(&m_controllers[7]); _acknowledge_irq(&m_controllers[7], 0); }

static void (*_irq_handlers[XHCI_MAX_CONTROLLERS])() = {
    _xhci_irq_handler_0,
    _xhci_irq_handler_1,
    _xhci_irq_handler_2,
    _xhci_irq_handler_3,
    _xhci_irq_handler_4,
    _xhci_irq_handler_5,
    _xhci_irq_handler_6,
    _xhci_irq_handler_7,
};

int xhci_init_device() {
    log_info(XHCI_MOD, "xHCI init!");

    pci_device_t* pci = pci_get_devices();
    while (pci) {
        if (pci->class_code == 0x0C &&
            pci->subclass   == 0x03 &&
            pci->prog_if    == 0x30)
        {
            if (m_controller_count >= XHCI_MAX_CONTROLLERS) {
                log_warn(XHCI_MOD, "Exceeded max controller count (%d), skipping %04x:%04x",
                         XHCI_MAX_CONTROLLERS, pci->vendor_id, pci->device_id);
                pci = pci->next;
                continue;
            }

            log_info(XHCI_MOD, "Found xHCI controller %d: %04x:%04x (bus %u slot %u fn %u)",
                     m_controller_count, pci->vendor_id, pci->device_id,
                     pci->bus, pci->slot, pci->function);
            printf("Found xHCI controller %d: %04x:%04x (bus %u slot %u fn %u)\n",
                     m_controller_count, pci->vendor_id, pci->device_id,
                     pci->bus, pci->slot, pci->function);

            xhci_controller_t* hc = &m_controllers[m_controller_count];
            memset(hc, 0, sizeof(xhci_controller_t));

            if (_init_controller(hc, pci) == 0) {
                m_controller_count++;
            } else {
                log_err(XHCI_MOD, "Failed to init controller %d", m_controller_count);
            }
        }
        pci = pci->next;
    }

    if (m_controller_count == 0) {
        log_err(XHCI_MOD, "No xHCI controllers successfully initialized");
        return -1;
    }

    log_ok(XHCI_MOD, "Initialized %d xHCI controller(s)", m_controller_count);
    return 0;
}

int xhci_start_device() {
    for (int i = 0; i < m_controller_count; i++) {
        log_debug(XHCI_MOD, "Starting controller %d", i);
        if (_start_controller(&m_controllers[i]) < 0) {
            log_err(XHCI_MOD, "Failed to start controller %d", i);
        }
    }
    return 0;
}

int xhci_stop_device() {
    for (int i = 0; i < m_controller_count; i++) {
        _stop_controller(&m_controllers[i]);
    }
    return 0;
}

static int _init_controller(xhci_controller_t* hc, pci_device_t* pci_dev) {
    hc->pci_dev = pci_dev;

    pci_map_bar(pci_dev, 0);
    pci_bar_t bar = pci_dev->bars[0];
    hc->base = bar.virt_base;

    log_debug(XHCI_MOD, "xHCI vaddr: 0x%llx", hc->base);
    log_debug(XHCI_MOD, "xHCI paddr: 0x%llx", xhci_get_physical_addr((void*)hc->base));

    _parse_capability_registers(hc);
    _log_capability_registers(hc);

    vector_init(&hc->usb3_ports, sizeof(uint8_t));
    _parse_extended_capabilites(hc);

    _take_bios_ownership(hc);

    if (_reset_host_controller(hc) < 0) {
        log_err(XHCI_MOD, "Unable to reset host controller");
        return -1;
    }

    _configure_operational_registers(hc);
    _log_operational_registers(hc);
    _configure_runtime_registers(hc);

    vector_init(&hc->cmd_completion_events, sizeof(xhci_command_completion_trb_t*));

    int idx = (int)(hc - m_controllers);
    pci_enable_intx(pci_dev, _irq_handlers[idx]);

    return 0;
}

static int _start_controller(xhci_controller_t* hc) {
    _log_usbsts(hc);

    if (_start_host_controller(hc) < 0) {
        return -1;
    }

    for (uint8_t port = 0; port < hc->max_ports; port++) {
        xhci_portsc_register_t portsc = _read_portsc_reg(hc, port);
        if (portsc.pp == 0) {
            portsc.pp = 1;
            _write_portsc_reg(hc, portsc, port);
        }
    }

    timer_sleep_ms(500);

    log_ok(XHCI_MOD, "Controller started! Scanning %d ports", hc->max_ports);

    for (uint8_t port = 0; port < hc->max_ports; port++) {
        xhci_portsc_register_t portsc = _read_portsc_reg(hc, port);

        log_debug(XHCI_MOD, "Port %d: PP=%d CCS=%d PED=%d PLS=%d speed=%d",
                  port, portsc.pp, portsc.ccs, portsc.ped,
                  portsc.pls, portsc.port_speed);

        if (!portsc.pp) {
            log_warn(XHCI_MOD, "Port %d failed to power up", port);
            continue;
        }

        if (portsc.ccs) {
            if (_reset_port(hc, port) == 0) {
                portsc = _read_portsc_reg(hc, port);
                log_debug(XHCI_MOD, "Device connected on port %d - %s",
                          port, _usb_speed_to_string(portsc.port_speed));
                printf("Device connected on port %d - %s\n",
                       port, _usb_speed_to_string(portsc.port_speed));
            } else {
                log_err(XHCI_MOD, "Failed to reset port %d", port);
            }
        }
    }

    _log_usbsts(hc);
    return 0;
}

static void _stop_controller(xhci_controller_t* hc) {
    vector_free(&hc->cmd_completion_events);
    vector_free(&hc->usb3_ports);
}

static void _parse_extended_capabilites(xhci_controller_t* hc) {
    volatile uint32_t* head_cap_ptr = (volatile uint32_t*)(hc->base + hc->ext_caps_offset);
    xhci_extended_capability_init(&hc->ext_cap_head, (volatile uint32_t*)hc->base, head_cap_ptr);

    xhci_extended_capability_t* node = &hc->ext_cap_head;

    while (node) {
        if (xhci_extended_capability_id(node) == XHCI_EXT_CAP_SUPPORTED_PROTOCOL) {
            xhci_usb_supported_protocol_capability_t cap;
            xhci_usb_supported_protocol_capability_init(&cap, node->m_base);

            log_debug(XHCI_MOD, "USB Protocol Cap: major=%d minor=%d ports=%d-%d",
                      cap.major_revision_version,
                      cap.minor_revision_version,
                      cap.compatible_port_offset,
                      cap.compatible_port_offset + cap.compatible_port_count - 1);

            uint8_t first_port = cap.compatible_port_offset - 1;
            uint8_t last_port  = first_port + cap.compatible_port_count - 1;

            if (cap.major_revision_version == 3) {
                for (uint8_t port = first_port; port <= last_port; port++) {
                    uint8_t p = port;
                    vector_push(&hc->usb3_ports, &p);
                }
            }
        }
        node = xhci_extended_capability_next(node);
    }
}

static void _process_events(xhci_controller_t* hc) {
    xhci_trb_t* events[32];
    size_t event_count = 0;

    if (xhci_event_ring_has_unprocessed_events()) {
        xhci_event_ring_dequeue_events(events, &event_count, 32);
    }

    uint8_t command_completion_status = 0;

    for (size_t i = 0; i < event_count; i++) {
        xhci_trb_t* trb = events[i];
        switch (trb->trb_type) {
        case XHCI_TRB_TYPE_CMD_COMPLETION_EVENT: {
            command_completion_status = 1;
            xhci_command_completion_trb_t* c = (xhci_command_completion_trb_t*)trb;
            vector_push(&hc->cmd_completion_events, &c);
            break;
        }
        default:
            break;
        }
    }

    hc->cmd_irq_completed = command_completion_status;
}

static xhci_command_completion_trb_t* _send_command_trb(xhci_controller_t* hc, xhci_trb_t* cmd_trb, uint32_t timeout_ms) {
    xhci_command_ring_enqueue(cmd_trb);
    xhci_doorbell_manager_ring_command_doorbell();

    uint64_t sleep_passed = 0;
    while (!hc->cmd_irq_completed) {
        timer_sleep_us(10);
        sleep_passed += 10;
        if (sleep_passed > timeout_ms * 1000) {
            log_warn(XHCI_MOD, "Timeout waiting on command completion");
            break;
        }
    }

    xhci_command_completion_trb_t* completion_trb =
        hc->cmd_completion_events.size
            ? *(xhci_command_completion_trb_t**)vector_get(&hc->cmd_completion_events, 0)
            : NULL;

    vector_clear(&hc->cmd_completion_events);
    hc->cmd_irq_completed = 0;

    if (!completion_trb) {
        log_err(XHCI_MOD, "Failed to find completion TRB for command %d", cmd_trb->trb_type);
        return NULL;
    }

    if (completion_trb->completion_code != XHCI_TRB_COMPLETION_CODE_SUCCESS) {
        log_err(XHCI_MOD, "Command TRB failed with error: %s",
                trb_completion_code_to_string(completion_trb->completion_code));
        return NULL;
    }

    return completion_trb;
}

static void _parse_capability_registers(xhci_controller_t* hc) {
    hc->cap_regs = (volatile xhci_capability_registers_t*)hc->base;

    hc->cap_length           = hc->cap_regs->caplength;
    hc->max_device_slots     = XHCI_MAX_DEVICE_SLOTS(hc->cap_regs);
    hc->max_interrupters     = XHCI_MAX_INTERRUPTERS(hc->cap_regs);
    hc->max_ports            = XHCI_MAX_PORTS(hc->cap_regs);
    hc->ist                  = XHCI_IST(hc->cap_regs);
    hc->erst_max             = XHCI_ERST_MAX(hc->cap_regs);
    hc->max_scratchpad_buffers = XHCI_MAX_SCRATCHPAD_BUFFERS(hc->cap_regs);
    hc->ac64                 = XHCI_AC64(hc->cap_regs);
    hc->bnc                  = XHCI_BNC(hc->cap_regs);
    hc->csz                  = XHCI_CSZ(hc->cap_regs);
    hc->ppc                  = XHCI_PPC(hc->cap_regs);
    hc->pind                 = XHCI_PIND(hc->cap_regs);
    hc->lhrc                 = XHCI_LHRC(hc->cap_regs);
    hc->ext_caps_offset      = XHCI_XECP(hc->cap_regs) * sizeof(uint32_t);

    hc->op_regs      = (volatile xhci_operational_registers_t*)(hc->base + hc->cap_length);
    hc->runtime_regs = (volatile xhci_runtime_registers_t*)(hc->base + hc->cap_regs->rtsoff);

    xhci_doorbell_manager_init(hc->base + hc->cap_regs->dboff);
}

static void _log_capability_registers(xhci_controller_t* hc) {
    log_debug(XHCI_MOD, "===== Xhci Capability Registers (0x%llx) =====", (uint64_t)hc->cap_regs);
    log_debug(XHCI_MOD, "    Length                : %i", hc->cap_length);
    log_debug(XHCI_MOD, "    Max Device Slots      : %i", hc->max_device_slots);
    log_debug(XHCI_MOD, "    Max Interrupters      : %i", hc->max_interrupters);
    log_debug(XHCI_MOD, "    Max Ports             : %i", hc->max_ports);
    log_debug(XHCI_MOD, "    IST                   : %i", hc->ist);
    log_debug(XHCI_MOD, "    ERST Max Size         : %i", hc->erst_max);
    log_debug(XHCI_MOD, "    Scratchpad Buffers    : %i", hc->max_scratchpad_buffers);
    log_debug(XHCI_MOD, "    64-bit Addressing     : %s", hc->ac64 ? "yes" : "no");
    log_debug(XHCI_MOD, "    Bandwidth Negotiation : %i", hc->bnc);
    log_debug(XHCI_MOD, "    64-byte Context Size  : %s", hc->csz ? "yes" : "no");
    log_debug(XHCI_MOD, "    Port Power Control    : %i", hc->ppc);
    log_debug(XHCI_MOD, "    Port Indicators       : %i", hc->pind);
    log_debug(XHCI_MOD, "    Light Reset Available : %i", hc->lhrc);
    log_debug(XHCI_MOD, "");
}

static void _log_operational_registers(xhci_controller_t* hc) {
    log_debug(XHCI_MOD, "===== Xhci Operational Registers (0x%llx) =====", (uint64_t)hc->op_regs);
    log_debug(XHCI_MOD, "    usbcmd   : 0x%x",   hc->op_regs->usbcmd);
    log_debug(XHCI_MOD, "    usbsts   : 0x%x",   hc->op_regs->usbsts);
    log_debug(XHCI_MOD, "    pagesize : 0x%x",   hc->op_regs->pagesize);
    log_debug(XHCI_MOD, "    dnctrl   : 0x%x",   hc->op_regs->dnctrl);
    log_debug(XHCI_MOD, "    crcr     : 0x%llx", hc->op_regs->crcr);
    log_debug(XHCI_MOD, "    dcbaap   : 0x%llx", hc->op_regs->dcbaap);
    log_debug(XHCI_MOD, "    config   : 0x%x",   hc->op_regs->config);
    log_debug(XHCI_MOD, "");
}

static int _start_host_controller(xhci_controller_t* hc) {
    uint32_t usbcmd = hc->op_regs->usbcmd;
    usbcmd |= XHCI_USBCMD_RUN_STOP;
    usbcmd |= XHCI_USBCMD_INTERRUPTER_ENABLE;
    hc->op_regs->usbcmd = usbcmd;

    const int max_retries = 1000;
    int retries = 0;
    while (hc->op_regs->usbsts & XHCI_USBSTS_HCH) {
        if (retries++ >= max_retries) {
            log_err(XHCI_MOD, "Controller failed to start: timeout after %d tries", retries);
            return -1;
        }
        timer_sleep_ms(1);
    }

    if (hc->op_regs->usbsts & XHCI_USBSTS_CNR) {
        log_err(XHCI_MOD, "Controller failed to start: controller not ready");
        return -2;
    }

    return 0;
}

static void _log_usbsts(xhci_controller_t* hc) {
    uint32_t status = hc->op_regs->usbsts;
    log_debug(XHCI_MOD, "===== USBSTS =====");
    if (status & XHCI_USBSTS_HCH)  log_debug(XHCI_MOD, "    Host Controller Halted");
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

static int _reset_host_controller(xhci_controller_t* hc) {
    uint32_t usbcmd = hc->op_regs->usbcmd;
    usbcmd &= ~XHCI_USBCMD_RUN_STOP;
    hc->op_regs->usbcmd = usbcmd;

    uint32_t timeout = 200;
    while (!(hc->op_regs->usbsts & XHCI_USBSTS_HCH)) {
        if (--timeout <= 0) {
            log_err(XHCI_MOD, "HC did not halt within %ums", timeout);
            return -1;
        }
        timer_sleep_ms(1);
    }

    usbcmd = hc->op_regs->usbcmd;
    usbcmd |= XHCI_USBCMD_HCRESET;
    hc->op_regs->usbcmd = usbcmd;

    timeout = 1000;
    while (hc->op_regs->usbcmd & XHCI_USBCMD_HCRESET ||
           hc->op_regs->usbsts & XHCI_USBSTS_CNR)
    {
        if (--timeout <= 0) {
            log_err(XHCI_MOD, "HC did not reset within %ums", timeout);
            return -1;
        }
        timer_sleep_ms(1);
    }

    timer_sleep_ms(50);

    if (hc->op_regs->usbcmd  != 0) return -1;
    if (hc->op_regs->crcr    != 0) return -1;
    if (hc->op_regs->dcbaap  != 0) return -1;
    if (hc->op_regs->config  != 0) return -1;
    if (hc->op_regs->dnctrl  != 0) return -1;

    return 0;
}

static void _setup_dcbaa(xhci_controller_t* hc) {
    size_t dcbaa_size = sizeof(uintptr_t) * (hc->max_device_slots + 1);

    hc->dcbaa      = (uint64_t*)alloc_xhci_memory(dcbaa_size, XHCI_DEVICE_CONTEXT_ALIGNMENT, XHCI_DEVICE_CONTEXT_BOUNDARY);
    hc->dcbaa_virt = (uint64_t*)kmalloc((hc->max_device_slots + 1) * sizeof(uint64_t));

    if (hc->max_scratchpad_buffers > 0) {
        uint64_t* scratchpad_array = (uint64_t*)alloc_xhci_memory(
            hc->max_scratchpad_buffers * sizeof(uint64_t),
            XHCI_DEVICE_CONTEXT_ALIGNMENT,
            XHCI_DEVICE_CONTEXT_BOUNDARY
        );

        for (uint8_t i = 0; i < hc->max_scratchpad_buffers; i++) {
            void* scratchpad = alloc_xhci_memory(PAGE_SIZE, XHCI_SCRATCHPAD_BUFFERS_ALIGNMENT, XHCI_SCRATCHPAD_BUFFER_ARRAY_BOUNDARY);
            scratchpad_array[i] = xhci_get_physical_addr(scratchpad);
        }

        hc->dcbaa[0]      = xhci_get_physical_addr(scratchpad_array);
        hc->dcbaa_virt[0] = (uint64_t)scratchpad_array;
    }

    hc->op_regs->dcbaap = xhci_get_physical_addr(hc->dcbaa);
}

static void _configure_operational_registers(xhci_controller_t* hc) {
    hc->op_regs->dnctrl = 0xffff;
    hc->op_regs->config = (uint32_t)hc->max_device_slots;

    _setup_dcbaa(hc);

    xhci_command_ring_init(XHCI_COMMAND_RING_TRB_COUNT);

    hc->op_regs->crcr = xhci_command_ring_get_physical_base() | xhci_command_ring_get_cycle_bit();
}

static void _acknowledge_irq(xhci_controller_t* hc, uint8_t interrupter) {
    volatile xhci_interrupter_registers_t* ir = &hc->runtime_regs->ir[interrupter];
    uint32_t iman = ir->iman;
    iman |= XHCI_IMAN_INTERRUPT_PENDING;
    ir->iman = iman;
}

static void _configure_runtime_registers(xhci_controller_t* hc) {
    hc->op_regs->usbsts = XHCI_USBSTS_EINT;

    volatile xhci_interrupter_registers_t* ir = &hc->runtime_regs->ir[0];
    uint32_t iman = ir->iman;
    iman |= XHCI_IMAN_INTERRUPT_ENABLE;
    ir->iman = iman;

    xhci_event_ring_init(XHCI_EVENT_RING_TRB_COUNT, ir);

    log_debug(XHCI_MOD, "ERSTSZ : 0x%llx", ir->erstsz);
    log_debug(XHCI_MOD, "ERSTBA : 0x%llx", ir->erstba);
    log_debug(XHCI_MOD, "ERDP   : 0x%llx", ir->erdp);

    _acknowledge_irq(hc, 0);
}

static bool _is_usb3_port(xhci_controller_t* hc, uint8_t port_num) {
    for (size_t i = 0; i < hc->usb3_ports.size; i++) {
        if (*(uint8_t*)vector_get(&hc->usb3_ports, i) == port_num) {
            return true;
        }
    }
    return false;
}

static xhci_portsc_register_t _read_portsc_reg(xhci_controller_t* hc, uint8_t port) {
    uint64_t reg_base = (uint64_t)hc->op_regs + (0x400 + (0x10 * port));
    xhci_portsc_register_t reg;
    reg.raw = *(volatile uint32_t*)reg_base;
    return reg;
}

static void _write_portsc_reg(xhci_controller_t* hc, xhci_portsc_register_t reg, uint8_t port) {
    uint64_t reg_base = (uint64_t)hc->op_regs + (0x400 + (0x10 * port));
    *(volatile uint32_t*)reg_base = reg.raw;
}

static int _reset_port(xhci_controller_t* hc, uint8_t port) {
    xhci_portsc_register_t portsc = _read_portsc_reg(hc, port);
    bool is_usb3 = _is_usb3_port(hc, port);

    if (portsc.pp == 0) {
        portsc.pp = 1;
        _write_portsc_reg(hc, portsc, port);
        timer_sleep_ms(20);
        portsc = _read_portsc_reg(hc, port);
        if (portsc.pp == 0) {
            log_err(XHCI_MOD, "Failed to power port %d", port);
            return -1;
        }
    }

    portsc.ped = 0;
    portsc.csc = 1;
    portsc.pec = 1;
    portsc.prc = 1;
    portsc.wrc = 1;
    _write_portsc_reg(hc, portsc, port);

    portsc = _read_portsc_reg(hc, port);

    if (!is_usb3) {
        portsc.pr = 1;
        _write_portsc_reg(hc, portsc, port);
    }

    int timeout = 200;
    while (timeout > 0) {
        portsc = _read_portsc_reg(hc, port);
        if (portsc.ped) break;
        timeout--;
        timer_sleep_ms(1);
    }

    if (timeout == 0) {
        log_err(XHCI_MOD, "Port %d reset timed out", port);
        return -1;
    }

    portsc.ped = 0;
    portsc.csc = 1;
    portsc.pec = 1;
    portsc.prc = 1;
    portsc.wrc = 1;
    _write_portsc_reg(hc, portsc, port);

    return 0;
}

static void _take_bios_ownership(xhci_controller_t* hc) {
    xhci_extended_capability_t* node = &hc->ext_cap_head;
    while (node) {
        if (xhci_extended_capability_id(node) == XHCI_EXT_CAP_USB_LEGACY_SUPPORT) {
            volatile uint32_t* usblegsup = node->m_base;

            *usblegsup |= XHCI_LEGACY_OS_OWNED_SEMAPHORE;

            uint32_t timeout = 200;
            while ((*usblegsup & XHCI_LEGACY_BIOS_OWNED_SEMAPHORE) && --timeout) {
                timer_sleep_ms(1);
            }
            if (*usblegsup & XHCI_LEGACY_BIOS_OWNED_SEMAPHORE) {
                log_warn(XHCI_MOD, "BIOS did not release xHCI ownership within timeout");
            }

            volatile uint32_t* usblegctlsts = usblegsup + 1;
            *usblegctlsts &= ~XHCI_LEGACY_SMI_ENABLE_BITS;

            log_debug(XHCI_MOD, "BIOS ownership released");
            return;
        }
        node = xhci_extended_capability_next(node);
    }
}

static const char* _usb_speed_to_string(uint8_t speed) {
    static const char* speed_strings[] = {
        "Invalid",
        "Full Speed (12 MB/s - USB2.0)",
        "Low Speed (1.5 Mb/s - USB 2.0)",
        "High Speed (480 Mb/s - USB 2.0)",
        "Super Speed (5 Gb/s - USB3.0)",
        "Super Speed Plus (10 Gb/s - USB 3.1)",
        "Undefined"
    };
    if (speed >= 7) return speed_strings[6];
    return speed_strings[speed];
}