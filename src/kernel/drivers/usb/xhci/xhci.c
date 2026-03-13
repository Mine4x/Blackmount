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
#include "xhci_device.h"

static xhci_controller_t  m_controllers[XHCI_MAX_CONTROLLERS];
static int                 m_controller_count = 0;


static xhci_device_probe_cb_t m_probe_cbs[XHCI_MAX_PROBE_CBS];
static int                     m_probe_cb_count = 0;





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
static int  _enable_slot(xhci_controller_t* hc, uint8_t* slot_id_out);
static int  _address_device(xhci_controller_t* hc, xhci_device_t* dev, bool bsr);
static int  _get_device_descriptor(xhci_controller_t* hc, xhci_device_t* dev);
static int  _enumerate_device(xhci_controller_t* hc, uint8_t port, uint8_t speed);
static uint16_t _ep0_max_packet_size_for_speed(uint8_t speed);
static uint8_t  _usb_ep_attrs_to_xhci_type(uint8_t ep_attrs, bool dir_in);
static uint8_t  _binterval_to_xhci_interval(uint8_t binterval, uint8_t speed);





static void _xhci_irq_handler_0() { _process_events(&m_controllers[0]); _acknowledge_irq(&m_controllers[0], 0); }
static void _xhci_irq_handler_1() { _process_events(&m_controllers[1]); _acknowledge_irq(&m_controllers[1], 0); }
static void _xhci_irq_handler_2() { _process_events(&m_controllers[2]); _acknowledge_irq(&m_controllers[2], 0); }
static void _xhci_irq_handler_3() { _process_events(&m_controllers[3]); _acknowledge_irq(&m_controllers[3], 0); }
static void _xhci_irq_handler_4() { _process_events(&m_controllers[4]); _acknowledge_irq(&m_controllers[4], 0); }
static void _xhci_irq_handler_5() { _process_events(&m_controllers[5]); _acknowledge_irq(&m_controllers[5], 0); }
static void _xhci_irq_handler_6() { _process_events(&m_controllers[6]); _acknowledge_irq(&m_controllers[6], 0); }
static void _xhci_irq_handler_7() { _process_events(&m_controllers[7]); _acknowledge_irq(&m_controllers[7], 0); }

static void (*_irq_handlers[XHCI_MAX_CONTROLLERS])() = {
    _xhci_irq_handler_0, _xhci_irq_handler_1,
    _xhci_irq_handler_2, _xhci_irq_handler_3,
    _xhci_irq_handler_4, _xhci_irq_handler_5,
    _xhci_irq_handler_6, _xhci_irq_handler_7,
};





int xhci_init_device() {
    log_info(XHCI_MOD, "xHCI init!");

    pci_device_t* pci = pci_get_devices();
    while (pci) {
        if (pci->class_code == 0x0C && pci->subclass == 0x03 && pci->prog_if == 0x30) {
            if (m_controller_count >= XHCI_MAX_CONTROLLERS) {
                log_warn(XHCI_MOD, "Exceeded max controller count, skipping %04x:%04x",
                         pci->vendor_id, pci->device_id);
                pci = pci->next;
                continue;
            }

            log_info(XHCI_MOD, "Found xHCI controller %d: %04x:%04x (bus %u slot %u fn %u)",
                     m_controller_count, pci->vendor_id, pci->device_id,
                     pci->bus, pci->slot, pci->function);

            xhci_controller_t* hc = &m_controllers[m_controller_count];
            memset(hc, 0, sizeof(xhci_controller_t));

            if (_init_controller(hc, pci) == 0)
                m_controller_count++;
            else
                log_err(XHCI_MOD, "Failed to init controller %d", m_controller_count);
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
        if (_start_controller(&m_controllers[i]) < 0)
            log_err(XHCI_MOD, "Failed to start controller %d", i);
    }
    return 0;
}

int xhci_stop_device() {
    for (int i = 0; i < m_controller_count; i++)
        _stop_controller(&m_controllers[i]);
    return 0;
}





void xhci_register_probe_callback(xhci_device_probe_cb_t cb) {
    if (m_probe_cb_count < XHCI_MAX_PROBE_CBS)
        m_probe_cbs[m_probe_cb_count++] = cb;
    else
        log_warn(XHCI_MOD, "xhci_register_probe_callback: table full");
}





int xhci_control_transfer(xhci_controller_t* hc, xhci_device_t* dev,
                           usb_setup_packet_t* setup,
                           void* data_buf, uint16_t data_len,
                           bool dir_in, uint32_t timeout_ms)
{
    (void)hc; 
    dev->transfer_completed = 0;

    
    xhci_trb_t setup_trb;
    memset(&setup_trb, 0, sizeof(setup_trb));
    memcpy(&setup_trb.parameter, setup, 8);
    setup_trb.status = 8;
    uint32_t trt = (data_len == 0) ? 0u : (dir_in ? 3u : 2u);
    setup_trb.control = (XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT)
                      | (1u << 6)     
                      | (trt << 16);  
    xhci_transfer_ring_enqueue(&dev->ep0_ring, &setup_trb);

    
    if (data_len > 0) {
        xhci_trb_t data_trb;
        memset(&data_trb, 0, sizeof(data_trb));
        data_trb.parameter = (uint64_t)xhci_get_physical_addr(data_buf);
        data_trb.status    = data_len;
        data_trb.control   = (XHCI_TRB_TYPE_DATA_STAGE << XHCI_TRB_TYPE_SHIFT)
                           | (dir_in ? (1u << 16) : 0u); 
        xhci_transfer_ring_enqueue(&dev->ep0_ring, &data_trb);
    }

    
    
    
    xhci_trb_t status_trb;
    memset(&status_trb, 0, sizeof(status_trb));
    uint32_t status_dir = (data_len == 0 || dir_in) ? 0u : 1u;
    status_trb.control  = (XHCI_TRB_TYPE_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT)
                        | (1u << 5)            
                        | (status_dir << 16);  
    xhci_transfer_ring_enqueue(&dev->ep0_ring, &status_trb);

    
    xhci_doorbell_manager_ring_doorbell(dev->slot_id,
                                        XHCI_DOORBELL_TARGET_CONTROL_EP_RING);

    
    
    uint64_t elapsed_us = 0;
    while (!dev->transfer_completed) {
        _process_events((xhci_controller_t*)dev->hc);
        if (dev->transfer_completed) break;
        timer_sleep_us(100);
        elapsed_us += 100;
        if (elapsed_us > (uint64_t)timeout_ms * 1000) {
            log_err(XHCI_MOD, "Control transfer timeout (slot=%d)", dev->slot_id);
            return -1;
        }
    }

    xhci_transfer_event_trb_t* evt = (xhci_transfer_event_trb_t*)&dev->last_transfer_event;
    if (evt->completion_code != XHCI_TRB_COMPLETION_CODE_SUCCESS &&
        evt->completion_code != XHCI_TRB_COMPLETION_CODE_SHORT_PACKET) {
        log_err(XHCI_MOD, "Control transfer error (slot=%d): %s",
                dev->slot_id, trb_completion_code_to_string(evt->completion_code));
        return -1;
    }
    return 0;
}

int xhci_configure_endpoint(xhci_controller_t* hc, xhci_device_t* dev,
                             uint8_t ep_addr, uint8_t ep_attrs,
                             uint16_t max_pkt_size, uint8_t interval)
{
    uint8_t dci    = xhci_dci_from_ep_addr(ep_addr);
    bool    dir_in = (ep_addr & 0x80) != 0;

    if (dci < 2 || dci >= XHCI_MAX_DCI) {
        log_err(XHCI_MOD, "configure_endpoint: invalid DCI %d (ep_addr=0x%02x)", dci, ep_addr);
        return -1;
    }

    
    xhci_transfer_ring_init(&dev->ep_rings[dci], XHCI_TRANSFER_RING_TRB_COUNT);
    dev->ep_transfer_completed[dci] = 0;

    uint32_t ctx_size = hc->csz ? 64u : 32u;

    
    
    
    
    
    
    const size_t in_ctx_bytes = ctx_size * 34;
    memset(dev->input_ctx, 0, in_ctx_bytes);

    
    xhci_input_control_context_t* ctrl_ctx = (xhci_input_control_context_t*)dev->input_ctx;
    ctrl_ctx->add_flags = (1u << 0) | (1u << dci);

    
    
    xhci_slot_context_t* out_slot = (xhci_slot_context_t*)dev->output_ctx;
    xhci_slot_context_t* in_slot  = (xhci_slot_context_t*)((uint8_t*)dev->input_ctx + ctx_size * 1);
    *in_slot = *out_slot;
    if (dci > in_slot->context_entries)
        in_slot->context_entries = dci;

    
    
    xhci_endpoint_context_t* ep_ctx =
        (xhci_endpoint_context_t*)((uint8_t*)dev->input_ctx + ctx_size * (dci + 1));

    ep_ctx->ep_type         = _usb_ep_attrs_to_xhci_type(ep_attrs, dir_in);
    ep_ctx->cerr            = 3;
    ep_ctx->max_packet_size = max_pkt_size;
    ep_ctx->interval        = _binterval_to_xhci_interval(interval, dev->speed);
    ep_ctx->avg_trb_len     = max_pkt_size; 

    uintptr_t phys            = dev->ep_rings[dci].physical_base;
    ep_ctx->tr_dequeue_lo     = (uint32_t)( phys        & 0xFFFFFFF0u) | 1u; 
    ep_ctx->tr_dequeue_hi     = (uint32_t)((phys >> 32) & 0xFFFFFFFFu);

    
    
    
    xhci_trb_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.parameter = (uint64_t)xhci_get_physical_addr(dev->input_ctx);
    cmd.control   = (XHCI_TRB_TYPE_CONFIGURE_ENDPOINT_CMD << XHCI_TRB_TYPE_SHIFT)
                  | ((uint32_t)dev->slot_id << 24);

    xhci_command_completion_trb_t* comp = _send_command_trb(hc, &cmd, 1000);
    if (!comp) {
        log_err(XHCI_MOD, "Configure Endpoint failed (slot=%d dci=%d)", dev->slot_id, dci);
        return -1;
    }

    log_debug(XHCI_MOD, "Configured endpoint dci=%d ep_addr=0x%02x max_pkt=%d",
              dci, ep_addr, max_pkt_size);
    return 0;
}

int xhci_queue_transfer(xhci_controller_t* hc, xhci_device_t* dev,
                        uint8_t ep_addr, void* buf, uint16_t len)
{
    (void)hc;
    uint8_t dci = xhci_dci_from_ep_addr(ep_addr);

    if (dci < 2 || dci >= XHCI_MAX_DCI) {
        log_err(XHCI_MOD, "queue_transfer: invalid DCI %d", dci);
        return -1;
    }

    dev->ep_transfer_completed[dci] = 0;

    
    
    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.parameter = (uint64_t)xhci_get_physical_addr(buf);
    trb.status    = len;
    trb.control   = (XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT)
                  | (1u << 5); 

    xhci_transfer_ring_enqueue(&dev->ep_rings[dci], &trb);

    
    xhci_doorbell_manager_ring_doorbell(dev->slot_id, (uint8_t)dci);

    return 0;
}

int xhci_wait_transfer(xhci_device_t* dev, uint8_t ep_addr, uint32_t timeout_ms)
{
    uint8_t dci = xhci_dci_from_ep_addr(ep_addr);

    uint64_t elapsed_us = 0;
    while (!dev->ep_transfer_completed[dci]) {
        _process_events((xhci_controller_t*)dev->hc);
        if (dev->ep_transfer_completed[dci]) break;
        timer_sleep_us(100);
        elapsed_us += 100;
        if (elapsed_us > (uint64_t)timeout_ms * 1000) {
            return -1;
        }
    }

    xhci_transfer_event_trb_t* evt =
        (xhci_transfer_event_trb_t*)&dev->ep_last_transfer_event[dci];

    if (evt->completion_code != XHCI_TRB_COMPLETION_CODE_SUCCESS &&
        evt->completion_code != XHCI_TRB_COMPLETION_CODE_SHORT_PACKET) {
        log_err(XHCI_MOD, "Transfer error on ep 0x%02x dci=%d: %s",
                ep_addr, dci, trb_completion_code_to_string(evt->completion_code));
        return -1;
    }
    return 0;
}





static void _process_events(xhci_controller_t* hc) {
    xhci_trb_t* events[32];
    size_t event_count = 0;

    if (xhci_event_ring_has_unprocessed_events())
        xhci_event_ring_dequeue_events(events, &event_count, 32);

    uint8_t any_cmd_completion = 0;

    for (size_t i = 0; i < event_count; i++) {
        xhci_trb_t* trb = events[i];

        switch (trb->trb_type) {

        
        case XHCI_TRB_TYPE_CMD_COMPLETION_EVENT: {
            any_cmd_completion = 1;
            xhci_command_completion_trb_t* c = (xhci_command_completion_trb_t*)trb;
            vector_push(&hc->cmd_completion_events, &c);
            break;
        }

        
        case XHCI_TRB_TYPE_TRANSFER_EVENT: {
            xhci_transfer_event_trb_t* evt = (xhci_transfer_event_trb_t*)trb;
            uint8_t slot_id = evt->slot_id;
            uint8_t dci     = (uint8_t)evt->endpoint_id;

            if (slot_id == 0 || slot_id >= XHCI_MAX_DEVICES) break;

            xhci_device_t* dev = &hc->devices[slot_id];
            if (!dev->active) break;

            if (dci == 1) {
                
                dev->last_transfer_event = *trb;
                dev->transfer_completed  = 1;
            } else if (dci < XHCI_MAX_DCI) {
                
                dev->ep_last_transfer_event[dci] = *trb;
                dev->ep_transfer_completed[dci]  = 1;
            }
            break;
        }

        
        case XHCI_TRB_TYPE_PORT_STATUS_CHANGE_EVENT: {
            xhci_port_status_change_event_trb_t* psc =
                (xhci_port_status_change_event_trb_t*)trb;
            log_debug(XHCI_MOD, "Port Status Change: port %d", psc->port_id);
            break;
        }

        default:
            log_debug(XHCI_MOD, "Unhandled event TRB type %d (%s)",
                      trb->trb_type, trb_type_to_string(trb->trb_type));
            break;
        }
    }

    hc->cmd_irq_completed = any_cmd_completion;
}





static xhci_command_completion_trb_t* _send_command_trb(xhci_controller_t* hc,
                                                          xhci_trb_t* cmd_trb,
                                                          uint32_t timeout_ms)
{
    xhci_command_ring_enqueue(cmd_trb);
    xhci_doorbell_manager_ring_command_doorbell();

    
    
    
    
    
    uint64_t elapsed = 0;
    while (!hc->cmd_irq_completed) {
        _process_events(hc);          
        if (hc->cmd_irq_completed) break;
        timer_sleep_us(100);
        elapsed += 100;
        if (elapsed > (uint64_t)timeout_ms * 1000) {
            log_warn(XHCI_MOD, "Timeout waiting on command completion");
            break;
        }
    }

    xhci_command_completion_trb_t* comp =
        hc->cmd_completion_events.size
            ? *(xhci_command_completion_trb_t**)vector_get(&hc->cmd_completion_events, 0)
            : NULL;

    vector_clear(&hc->cmd_completion_events);
    hc->cmd_irq_completed = 0;

    if (!comp) {
        log_err(XHCI_MOD, "No completion TRB for command type %d", cmd_trb->trb_type);
        return NULL;
    }
    if (comp->completion_code != XHCI_TRB_COMPLETION_CODE_SUCCESS) {
        log_err(XHCI_MOD, "Command failed: %s",
                trb_completion_code_to_string(comp->completion_code));
        return NULL;
    }
    return comp;
}





static uint16_t _ep0_max_packet_size_for_speed(uint8_t speed)
{
    switch (speed) {
    case XHCI_USB_SPEED_LOW_SPEED:        return 8;
    case XHCI_USB_SPEED_FULL_SPEED:       return 8;
    case XHCI_USB_SPEED_HIGH_SPEED:       return 64;
    case XHCI_USB_SPEED_SUPER_SPEED:
    case XHCI_USB_SPEED_SUPER_SPEED_PLUS: return 512;
    default:                              return 8;
    }
}


static uint8_t _usb_ep_attrs_to_xhci_type(uint8_t ep_attrs, bool dir_in)
{
    switch (ep_attrs & 0x03) {
    case 0: return XHCI_ENDPOINT_TYPE_CONTROL;
    case 1: return dir_in ? XHCI_ENDPOINT_TYPE_ISOCHRONOUS_IN
                          : XHCI_ENDPOINT_TYPE_ISOCHRONOUS_OUT;
    case 2: return dir_in ? XHCI_ENDPOINT_TYPE_BULK_IN
                          : XHCI_ENDPOINT_TYPE_BULK_OUT;
    case 3: return dir_in ? XHCI_ENDPOINT_TYPE_INTERRUPT_IN
                          : XHCI_ENDPOINT_TYPE_INTERRUPT_OUT;
    default: return XHCI_ENDPOINT_TYPE_INVALID;
    }
}




static uint8_t _binterval_to_xhci_interval(uint8_t binterval, uint8_t speed)
{
    switch (speed) {
    case XHCI_USB_SPEED_LOW_SPEED:
    case XHCI_USB_SPEED_FULL_SPEED:
        
        
        
        {
            uint32_t mf = (uint32_t)binterval * 8;
            uint8_t  v  = 0;
            while ((1u << (v + 1)) <= mf) v++;
            return v < 3 ? 3 : v; 
        }
    case XHCI_USB_SPEED_HIGH_SPEED:
        
        return (binterval > 0) ? (binterval - 1) : 0;
    case XHCI_USB_SPEED_SUPER_SPEED:
    case XHCI_USB_SPEED_SUPER_SPEED_PLUS:
        return (binterval > 0) ? (binterval - 1) : 0;
    default:
        return 3; 
    }
}

static int _enable_slot(xhci_controller_t* hc, uint8_t* slot_id_out)
{
    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.control = (XHCI_TRB_TYPE_ENABLE_SLOT_CMD << XHCI_TRB_TYPE_SHIFT);

    xhci_command_completion_trb_t* comp = _send_command_trb(hc, &trb, 1000);
    if (!comp) return -1;

    *slot_id_out = comp->slot_id;
    log_debug(XHCI_MOD, "Enable Slot → slot_id=%d", *slot_id_out);
    return 0;
}

static int _address_device(xhci_controller_t* hc, xhci_device_t* dev, bool bsr)
{
    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.parameter = (uint64_t)xhci_get_physical_addr(dev->input_ctx);
    trb.control   = (XHCI_TRB_TYPE_ADDRESS_DEVICE_CMD << XHCI_TRB_TYPE_SHIFT)
                  | ((uint32_t)dev->slot_id << 24)
                  | (bsr ? (1u << 9) : 0u);

    xhci_command_completion_trb_t* comp = _send_command_trb(hc, &trb, 1000);
    if (!comp) {
        log_err(XHCI_MOD, "Address Device failed (slot=%d bsr=%d)", dev->slot_id, bsr);
        return -1;
    }
    return 0;
}

static int _get_device_descriptor(xhci_controller_t* hc, xhci_device_t* dev)
{
    usb_setup_packet_t setup = {
        .bmRequestType = USB_REQTYPE_DIR_IN | USB_REQTYPE_TYPE_STANDARD
                       | USB_REQTYPE_RECIP_DEVICE,
        .bRequest      = USB_REQ_GET_DESCRIPTOR,
        .wValue        = (uint16_t)((USB_DESC_TYPE_DEVICE << 8) | 0x00),
        .wIndex        = 0,
        .wLength       = sizeof(usb_device_descriptor_t)
    };

    void* buf = alloc_xhci_memory(64, 64, PAGE_SIZE);
    if (!buf) return -1;
    memset(buf, 0, 64);

    int ret = xhci_control_transfer(hc, dev, &setup, buf,
                                    sizeof(usb_device_descriptor_t),
                                    true, 2000);
    if (ret == 0)
        memcpy(&dev->descriptor, buf, sizeof(usb_device_descriptor_t));

    free_xhci_memory(buf);
    return ret;
}

static int _enumerate_device(xhci_controller_t* hc, uint8_t port, uint8_t speed)
{
    uint8_t slot_id = 0;
    if (_enable_slot(hc, &slot_id) < 0 || slot_id == 0 || slot_id >= XHCI_MAX_DEVICES)
        return -1;

    xhci_device_t* dev = &hc->devices[slot_id];
    memset(dev, 0, sizeof(xhci_device_t));
    dev->active  = true;
    dev->slot_id = slot_id;
    dev->port    = port;
    dev->speed   = speed;
    dev->hc      = hc;  

    uint32_t ctx_size = hc->csz ? 64u : 32u;

    
    const size_t out_bytes = ctx_size * 33;
    dev->output_ctx = alloc_xhci_memory(out_bytes,
                                         XHCI_DEVICE_CONTEXT_ALIGNMENT,
                                         XHCI_DEVICE_CONTEXT_BOUNDARY);
    if (!dev->output_ctx) { dev->active = false; return -1; }
    memset(dev->output_ctx, 0, out_bytes);

    hc->dcbaa[slot_id]      = (uint64_t)xhci_get_physical_addr(dev->output_ctx);
    hc->dcbaa_virt[slot_id] = (uint64_t)dev->output_ctx;

    
    const size_t in_bytes = ctx_size * 34;
    dev->input_ctx = alloc_xhci_memory(in_bytes,
                                        XHCI_DEVICE_CONTEXT_ALIGNMENT,
                                        XHCI_DEVICE_CONTEXT_BOUNDARY);
    if (!dev->input_ctx) { dev->active = false; return -1; }
    memset(dev->input_ctx, 0, in_bytes);

    
    xhci_transfer_ring_init(&dev->ep0_ring, XHCI_TRANSFER_RING_TRB_COUNT);

    
    xhci_input_control_context_t* ctrl_ctx = (xhci_input_control_context_t*)dev->input_ctx;
    ctrl_ctx->add_flags = (1u << 0) | (1u << 1);

    
    xhci_slot_context_t* slot_ctx =
        (xhci_slot_context_t*)((uint8_t*)dev->input_ctx + ctx_size * 1);
    slot_ctx->route_string    = 0;
    slot_ctx->speed           = speed;
    slot_ctx->context_entries = 1;
    slot_ctx->root_hub_port   = port + 1; 

    
    xhci_endpoint_context_t* ep0_ctx =
        (xhci_endpoint_context_t*)((uint8_t*)dev->input_ctx + ctx_size * 2);
    ep0_ctx->ep_type         = XHCI_ENDPOINT_TYPE_CONTROL;
    ep0_ctx->cerr            = 3;
    ep0_ctx->max_packet_size = _ep0_max_packet_size_for_speed(speed);
    ep0_ctx->avg_trb_len     = 8;
    uintptr_t ep0_phys       = dev->ep0_ring.physical_base;
    ep0_ctx->tr_dequeue_lo   = (uint32_t)( ep0_phys        & 0xFFFFFFF0u) | 1u;
    ep0_ctx->tr_dequeue_hi   = (uint32_t)((ep0_phys >> 32) & 0xFFFFFFFFu);

    
    if (_address_device(hc, dev, false) < 0) { dev->active = false; return -1; }

    
    if (_get_device_descriptor(hc, dev) < 0) { dev->active = false; return -1; }

    usb_device_descriptor_t* d = &dev->descriptor;
    log_ok(XHCI_MOD,
           "USB Device [slot=%d port=%d] VID=%04x PID=%04x "
           "Class=%02x/%02x/%02x USB=%.2x.%02x MaxPkt=%d",
           slot_id, port, d->idVendor, d->idProduct,
           d->bDeviceClass, d->bDeviceSubClass, d->bDeviceProtocol,
           d->bcdUSB >> 8, d->bcdUSB & 0xFF, d->bMaxPacketSize0);
    printf("USB Device port %d: VID=%04x PID=%04x Class=%02x/%02x/%02x\n",
           port, d->idVendor, d->idProduct,
           d->bDeviceClass, d->bDeviceSubClass, d->bDeviceProtocol);

    
    for (int i = 0; i < m_probe_cb_count; i++)
        m_probe_cbs[i](hc, dev);

    return 0;
}





static int _init_controller(xhci_controller_t* hc, pci_device_t* pci_dev) {
    hc->pci_dev = pci_dev;

    
    
    
    pci_enable_bus_mastering(pci_dev);

    pci_map_bar(pci_dev, 0);
    pci_bar_t bar = pci_dev->bars[0];
    hc->base = bar.virt_base;

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

    if (_start_host_controller(hc) < 0) return -1;

    for (uint8_t port = 0; port < hc->max_ports; port++) {
        xhci_portsc_register_t portsc = _read_portsc_reg(hc, port);
        if (!portsc.pp) {
            portsc.pp = 1;
            _write_portsc_reg(hc, portsc, port);
        }
    }

    timer_sleep_ms(500);

    log_ok(XHCI_MOD, "Controller started! Scanning %d ports", hc->max_ports);

    for (uint8_t port = 0; port < hc->max_ports; port++) {
        xhci_portsc_register_t portsc = _read_portsc_reg(hc, port);

        if (!portsc.pp) {
            log_warn(XHCI_MOD, "Port %d failed to power up", port);
            continue;
        }
        if (portsc.ccs) {
            if (_reset_port(hc, port) == 0) {
                portsc = _read_portsc_reg(hc, port);
                printf("Device on port %d: %s\n", port,
                       _usb_speed_to_string(portsc.port_speed));
                if (_enumerate_device(hc, port, portsc.port_speed) < 0)
                    log_err(XHCI_MOD, "Port %d: enumeration failed", port);
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





static void _parse_capability_registers(xhci_controller_t* hc) {
    hc->cap_regs             = (volatile xhci_capability_registers_t*)hc->base;
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
    log_debug(XHCI_MOD, "===== xHCI Capability Registers (0x%llx) =====", (uint64_t)hc->cap_regs);
    log_debug(XHCI_MOD, "    Max Device Slots : %i  Max Ports : %i  CSZ : %s  AC64 : %s",
              hc->max_device_slots, hc->max_ports,
              hc->csz ? "64B" : "32B", hc->ac64 ? "yes" : "no");
}

static void _log_operational_registers(xhci_controller_t* hc) {
    log_debug(XHCI_MOD, "===== xHCI Operational Registers =====");
    log_debug(XHCI_MOD, "    usbcmd=0x%x usbsts=0x%x dcbaap=0x%llx crcr=0x%llx",
              hc->op_regs->usbcmd, hc->op_regs->usbsts,
              hc->op_regs->dcbaap, hc->op_regs->crcr);
}





static int _start_host_controller(xhci_controller_t* hc) {
    hc->op_regs->usbcmd |= XHCI_USBCMD_RUN_STOP | XHCI_USBCMD_INTERRUPTER_ENABLE;

    for (int i = 0; i < 1000; i++) {
        if (!(hc->op_regs->usbsts & XHCI_USBSTS_HCH)) goto started;
        timer_sleep_ms(1);
    }
    log_err(XHCI_MOD, "Controller failed to start"); return -1;
started:
    if (hc->op_regs->usbsts & XHCI_USBSTS_CNR) {
        log_err(XHCI_MOD, "CNR still set after start"); return -2;
    }
    return 0;
}

static void _log_usbsts(xhci_controller_t* hc) {
    uint32_t s = hc->op_regs->usbsts;
    log_debug(XHCI_MOD, "USBSTS=0x%x%s%s%s%s%s", s,
              s & XHCI_USBSTS_HCH ? " HCH" : "",
              s & XHCI_USBSTS_HSE ? " HSE" : "",
              s & XHCI_USBSTS_CNR ? " CNR" : "",
              s & XHCI_USBSTS_HCE ? " HCE" : "",
              s & XHCI_USBSTS_PCD ? " PCD" : "");
}

static int _reset_host_controller(xhci_controller_t* hc) {
    hc->op_regs->usbcmd &= ~XHCI_USBCMD_RUN_STOP;
    for (int i = 0; i < 200; i++) {
        if (hc->op_regs->usbsts & XHCI_USBSTS_HCH) goto halted;
        timer_sleep_ms(1);
    }
    log_err(XHCI_MOD, "HC did not halt"); return -1;
halted:
    hc->op_regs->usbcmd |= XHCI_USBCMD_HCRESET;
    for (int i = 0; i < 1000; i++) {
        if (!(hc->op_regs->usbcmd & XHCI_USBCMD_HCRESET) &&
            !(hc->op_regs->usbsts & XHCI_USBSTS_CNR)) goto reset_done;
        timer_sleep_ms(1);
    }
    log_err(XHCI_MOD, "HC did not complete reset"); return -1;
reset_done:
    timer_sleep_ms(50);
    
    
    
    
    return 0;
}





static void _setup_dcbaa(xhci_controller_t* hc) {
    size_t sz = sizeof(uint64_t) * (hc->max_device_slots + 1);
    hc->dcbaa      = (uint64_t*)alloc_xhci_memory(sz,
                         XHCI_DEVICE_CONTEXT_INDEX_ALIGNMENT,
                         XHCI_DEVICE_CONTEXT_INDEX_BOUNDARY);
    hc->dcbaa_virt = (uint64_t*)kmalloc(sz);
    memset(hc->dcbaa,      0, sz);
    memset(hc->dcbaa_virt, 0, sz);

    if (hc->max_scratchpad_buffers > 0) {
        uint64_t* spa = (uint64_t*)alloc_xhci_memory(
            hc->max_scratchpad_buffers * sizeof(uint64_t),
            XHCI_DEVICE_CONTEXT_ALIGNMENT, XHCI_DEVICE_CONTEXT_BOUNDARY);
        for (uint8_t i = 0; i < hc->max_scratchpad_buffers; i++) {
            void* sp = alloc_xhci_memory(PAGE_SIZE,
                           XHCI_SCRATCHPAD_BUFFERS_ALIGNMENT,
                           XHCI_SCRATCHPAD_BUFFER_ARRAY_BOUNDARY);
            spa[i] = (uint64_t)xhci_get_physical_addr(sp);
        }
        hc->dcbaa[0]      = (uint64_t)xhci_get_physical_addr(spa);
        hc->dcbaa_virt[0] = (uint64_t)spa;
    }
    hc->op_regs->dcbaap = (uint64_t)xhci_get_physical_addr(hc->dcbaa);
}

static void _configure_operational_registers(xhci_controller_t* hc) {
    hc->op_regs->dnctrl = 0xffff;
    hc->op_regs->config = (uint32_t)hc->max_device_slots;
    _setup_dcbaa(hc);
    xhci_command_ring_init(XHCI_COMMAND_RING_TRB_COUNT);
    hc->op_regs->crcr = (uint64_t)xhci_command_ring_get_physical_base()
                      | xhci_command_ring_get_cycle_bit();
}

static void _acknowledge_irq(xhci_controller_t* hc, uint8_t interrupter) {
    volatile xhci_interrupter_registers_t* ir = &hc->runtime_regs->ir[interrupter];
    ir->iman |= XHCI_IMAN_INTERRUPT_PENDING;
}

static void _configure_runtime_registers(xhci_controller_t* hc) {
    hc->op_regs->usbsts = XHCI_USBSTS_EINT;
    volatile xhci_interrupter_registers_t* ir = &hc->runtime_regs->ir[0];
    ir->iman |= XHCI_IMAN_INTERRUPT_ENABLE;
    xhci_event_ring_init(XHCI_EVENT_RING_TRB_COUNT, ir);
    _acknowledge_irq(hc, 0);
}





static void _parse_extended_capabilites(xhci_controller_t* hc) {
    volatile uint32_t* head = (volatile uint32_t*)(hc->base + hc->ext_caps_offset);
    xhci_extended_capability_init(&hc->ext_cap_head, (volatile uint32_t*)hc->base, head);

    xhci_extended_capability_t* node = &hc->ext_cap_head;
    while (node) {
        if (xhci_extended_capability_id(node) == XHCI_EXT_CAP_SUPPORTED_PROTOCOL) {
            xhci_usb_supported_protocol_capability_t cap;
            xhci_usb_supported_protocol_capability_init(&cap, node->m_base);
            if (cap.major_revision_version == 3) {
                uint8_t first = cap.compatible_port_offset - 1;
                uint8_t last  = first + cap.compatible_port_count - 1;
                for (uint8_t p = first; p <= last; p++)
                    vector_push(&hc->usb3_ports, &p);
            }
        }
        node = xhci_extended_capability_next(node);
    }
}

static bool _is_usb3_port(xhci_controller_t* hc, uint8_t port_num) {
    for (size_t i = 0; i < hc->usb3_ports.size; i++)
        if (*(uint8_t*)vector_get(&hc->usb3_ports, i) == port_num) return true;
    return false;
}

static void _take_bios_ownership(xhci_controller_t* hc) {
    xhci_extended_capability_t* node = &hc->ext_cap_head;
    while (node) {
        if (xhci_extended_capability_id(node) == XHCI_EXT_CAP_USB_LEGACY_SUPPORT) {
            volatile uint32_t* usblegsup = node->m_base;
            *usblegsup |= XHCI_LEGACY_OS_OWNED_SEMAPHORE;
            for (int i = 200; i > 0 && (*usblegsup & XHCI_LEGACY_BIOS_OWNED_SEMAPHORE); i--)
                timer_sleep_ms(1);
            if (*usblegsup & XHCI_LEGACY_BIOS_OWNED_SEMAPHORE)
                log_warn(XHCI_MOD, "BIOS did not release xHCI ownership");
            *(usblegsup + 1) &= ~XHCI_LEGACY_SMI_ENABLE_BITS;
            return;
        }
        node = xhci_extended_capability_next(node);
    }
}





static xhci_portsc_register_t _read_portsc_reg(xhci_controller_t* hc, uint8_t port) {
    uint64_t addr = (uint64_t)hc->op_regs + 0x400 + 0x10 * port;
    xhci_portsc_register_t r; r.raw = *(volatile uint32_t*)addr; return r;
}

static void _write_portsc_reg(xhci_controller_t* hc, xhci_portsc_register_t reg, uint8_t port) {
    uint64_t addr = (uint64_t)hc->op_regs + 0x400 + 0x10 * port;
    *(volatile uint32_t*)addr = reg.raw;
}

static int _reset_port(xhci_controller_t* hc, uint8_t port) {
    xhci_portsc_register_t portsc = _read_portsc_reg(hc, port);
    if (!portsc.pp) {
        portsc.pp = 1; _write_portsc_reg(hc, portsc, port);
        timer_sleep_ms(20); portsc = _read_portsc_reg(hc, port);
        if (!portsc.pp) return -1;
    }
    portsc.ped = 0; portsc.csc = 1; portsc.pec = 1;
    portsc.prc = 1; portsc.wrc = 1;
    _write_portsc_reg(hc, portsc, port);
    portsc = _read_portsc_reg(hc, port);
    if (!_is_usb3_port(hc, port)) { portsc.pr = 1; _write_portsc_reg(hc, portsc, port); }

    for (int t = 200; t > 0; t--) {
        portsc = _read_portsc_reg(hc, port);
        if (portsc.ped) goto port_ready;
        timer_sleep_ms(1);
    }
    log_err(XHCI_MOD, "Port %d reset timed out", port); return -1;
port_ready:
    portsc.ped = 0; portsc.csc = 1; portsc.pec = 1;
    portsc.prc = 1; portsc.wrc = 1;
    _write_portsc_reg(hc, portsc, port);
    return 0;
}





static const char* _usb_speed_to_string(uint8_t speed) {
    static const char* s[] = {
        "Invalid", "Full Speed (12 Mb/s)", "Low Speed (1.5 Mb/s)",
        "High Speed (480 Mb/s)", "SuperSpeed (5 Gb/s)", "SuperSpeed+ (10 Gb/s)", "Undefined"
    };
    return s[speed < 6 ? speed : 6];
}