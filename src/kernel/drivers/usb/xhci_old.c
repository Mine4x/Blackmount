#include "xhci_old.h"
#include <arch/x86_64/irq.h>
#include <string.h>


#include <heap.h>
#include <debug.h>

#define XHCI_MOD "xhci"


static xhci_controller_t *g_hc = NULL;


#define OP_USBCMD   0x00u
#define OP_USBSTS   0x04u
#define OP_PAGESIZE 0x08u
#define OP_DNCTRL   0x14u
#define OP_CRCR     0x18u   // 64-bit
#define OP_DCBAAP   0x30u   // 64-bit
#define OP_CONFIG   0x38u

#define USBCMD_RUN  (1u << 0)
#define USBCMD_RST  (1u << 1)
#define USBCMD_INTE (1u << 2)   // Interrupter Enable
#define USBCMD_HSEE (1u << 3)   // Host System Error Enable

#define USBSTS_HCH  (1u << 0)   // HC Halted
#define USBSTS_HSE  (1u << 2)   // Host System Error
#define USBSTS_EINT (1u << 3)   // Event Interrupt (W1C)
#define USBSTS_PCD  (1u << 4)   // Port Change Detect (W1C)
#define USBSTS_CNR  (1u << 11)  // Controller Not Ready

#define CRCR_RCS    (1u << 0)   // Ring Cycle State


#define PORT_BASE   0x400u
#define PORT_SIZE   0x10u


#define PORTSC_CCS          (1u << 0)   // Current Connect Status (RO)
#define PORTSC_PED          (1u << 1)   // Port Enabled/Disabled (RW1C to clear)
#define PORTSC_PR           (1u << 4)   // Port Reset (RW)
#define PORTSC_PP           (1u << 9)   // Port Power (RW)
#define PORTSC_SPEED_SHIFT  10
#define PORTSC_SPEED_MASK   (0xFu << 10)

#define PORTSC_W1C_MASK     (0x7Fu << 17)
#define PORTSC_CSC          (1u << 17)  // Connect Status Change
#define PORTSC_PEC          (1u << 18)  // Port Enable/Disable Change
#define PORTSC_WRC          (1u << 19)  // Warm Port Reset Change
#define PORTSC_OCC          (1u << 20)  // Over-current Change
#define PORTSC_PRC          (1u << 21)  // Port Reset Change
#define PORTSC_PLC          (1u << 22)  // Port Link State Change
#define PORTSC_CEC          (1u << 23)  // Config Error Change


#define IR0_BASE    0x20u
#define IR_IMAN     0x00u
#define IR_IMOD     0x04u
#define IR_ERSTSZ   0x08u
#define IR_ERSTBA   0x10u   // 64-bit
#define IR_ERDP     0x18u   // 64-bit

#define IMAN_IP     (1u << 0)   // Interrupt Pending (W1C)
#define IMAN_IE     (1u << 1)   // Interrupt Enable

#define ERDP_EHB    (1u << 3)   // Event Handler Busy (W1C)



static inline uint8_t  cap_r8 (xhci_controller_t *hc, uint32_t off) { return *(volatile uint8_t  *)(hc->cap_base + off); }
static inline uint16_t cap_r16(xhci_controller_t *hc, uint32_t off) { return *(volatile uint16_t *)(hc->cap_base + off); }
static inline uint32_t cap_r32(xhci_controller_t *hc, uint32_t off) { return *(volatile uint32_t *)(hc->cap_base + off); }

static inline uint32_t op_r32(xhci_controller_t *hc, uint32_t off)               { return *(volatile uint32_t *)(hc->op_base + off); }
static inline void     op_w32(xhci_controller_t *hc, uint32_t off, uint32_t val) { *(volatile uint32_t *)(hc->op_base + off) = val; }
static inline void     op_w64(xhci_controller_t *hc, uint32_t off, uint64_t val) {
    op_w32(hc, off,     (uint32_t)(val & 0xFFFFFFFFu));
    op_w32(hc, off + 4, (uint32_t)(val >> 32));
}


static inline uint32_t port_r32(xhci_controller_t *hc, uint8_t port, uint32_t off) {
    return *(volatile uint32_t *)(hc->op_base + PORT_BASE + (uint32_t)(port - 1) * PORT_SIZE + off);
}
static inline void port_w32(xhci_controller_t *hc, uint8_t port, uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(hc->op_base + PORT_BASE + (uint32_t)(port - 1) * PORT_SIZE + off) = val;
}


static inline uint32_t ir_r32(xhci_controller_t *hc, uint32_t off) {
    return *(volatile uint32_t *)(hc->rt_base + IR0_BASE + off);
}
static inline void ir_w32(xhci_controller_t *hc, uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(hc->rt_base + IR0_BASE + off) = val;
}
static inline void ir_w64(xhci_controller_t *hc, uint32_t off, uint64_t val) {
    ir_w32(hc, off,     (uint32_t)(val & 0xFFFFFFFFu));
    ir_w32(hc, off + 4, (uint32_t)(val >> 32));
}



static inline void doorbell(xhci_controller_t *hc, uint8_t slot, uint8_t ep_target) {
    hc->db_base[slot] = ep_target;
}



static void udelay(uint32_t us) {
    
    for (volatile uint64_t i = 0; i < (uint64_t)us * 300; i++)
        __asm__ volatile("pause");
}


static int op_wait(xhci_controller_t *hc, uint32_t off,
                   uint32_t mask, uint32_t expected, uint32_t timeout_ms) {
    for (uint32_t i = 0; i < timeout_ms * 10; i++) {
        if ((op_r32(hc, off) & mask) == expected) return 0;
        udelay(100);
    }
    return -1;
}













static inline xhci_slot_ctx_t *dev_slot_ctx(xhci_controller_t *hc, void *base) {
    return (xhci_slot_ctx_t *)((uint8_t *)base);
}
static inline xhci_ep_ctx_t *dev_ep_ctx(xhci_controller_t *hc, void *base, int dci) {
    return (xhci_ep_ctx_t *)((uint8_t *)base + hc->ctx_stride * (uint32_t)dci);
}
static inline xhci_input_ctrl_ctx_t *in_ctrl_ctx(xhci_controller_t *hc, void *base) {
    return (xhci_input_ctrl_ctx_t *)((uint8_t *)base);
}
static inline xhci_slot_ctx_t *in_slot_ctx(xhci_controller_t *hc, void *base) {
    return (xhci_slot_ctx_t *)((uint8_t *)base + hc->ctx_stride);
}
static inline xhci_ep_ctx_t *in_ep_ctx(xhci_controller_t *hc, void *base, int dci) {
    
    return (xhci_ep_ctx_t *)((uint8_t *)base + hc->ctx_stride * (uint32_t)(dci + 1));
}



static int ring_alloc(xhci_ring_t *ring) {
    size_t size = XHCI_RING_SIZE * sizeof(xhci_trb_t);
    ring->dma = dma_alloc(size, DMA_ZONE_NORMAL);
    if (!ring->dma) return -1;

    ring->trbs    = (xhci_trb_t *)ring->dma->virt;
    ring->phys    = ring->dma->phys;
    ring->enqueue = 0;
    ring->dequeue = 0;
    ring->cycle   = 1;
    memset(ring->trbs, 0, size);

    
    
    uint32_t last = XHCI_RING_SIZE - 1;
    ring->trbs[last].parameter = ring->phys;
    ring->trbs[last].status    = 0;
    ring->trbs[last].control   = TRB_CTRL_TYPE(TRB_TYPE_LINK) | TRB_LINK_TC | ring->cycle;
    return 0;
}

static void ring_free(xhci_ring_t *ring) {
    if (ring->dma) { dma_free(ring->dma); ring->dma = NULL; }
    ring->trbs = NULL;
}




static void ring_push(xhci_ring_t *ring, const xhci_trb_t *trb) {
    uint32_t idx = ring->enqueue;

    ring->trbs[idx].parameter = trb->parameter;
    ring->trbs[idx].status    = trb->status;
    
    ring->trbs[idx].control   = (trb->control & ~TRB_CTRL_CYCLE) | ring->cycle;

    ring->enqueue++;

    
    if (ring->enqueue == XHCI_RING_SIZE - 1) {
        
        ring->trbs[ring->enqueue].control =
            TRB_CTRL_TYPE(TRB_TYPE_LINK) | TRB_LINK_TC | ring->cycle;
        ring->cycle ^= 1;
        ring->enqueue = 0;
    }
}





static bool event_pending(const xhci_ring_t *er) {
    uint8_t cycle_bit = er->trbs[er->dequeue].control & 1u;
    return (cycle_bit == er->cycle);
}



static void event_consume(xhci_controller_t *hc) {
    hc->event_ring.dequeue++;
    if (hc->event_ring.dequeue == XHCI_RING_SIZE) {
        hc->event_ring.dequeue = 0;
        hc->event_ring.cycle ^= 1;
    }
    
    
    uint64_t erdp = hc->event_ring.phys
                  + (uint64_t)hc->event_ring.dequeue * sizeof(xhci_trb_t);
    ir_w64(hc, IR_ERDP, erdp | ERDP_EHB);
}




static int event_poll(xhci_controller_t *hc, uint8_t want_type,
                      xhci_trb_t *out, uint32_t timeout_ms) {
    for (uint32_t t = 0; t < timeout_ms * 10; t++) {
        if (event_pending(&hc->event_ring)) {
            xhci_trb_t *ev   = &hc->event_ring.trbs[hc->event_ring.dequeue];
            uint8_t     type = (uint8_t)TRB_GET_TYPE(ev->control);

            if (type == want_type) {
                if (out) *out = *ev;
                event_consume(hc);
                return 0;
            }
            
            event_consume(hc);
        }
        udelay(100);
    }
    return -1;
}






static void xhci_irq_handler(int irq) {
    (void)irq;
    xhci_controller_t *hc = g_hc;
    if (!hc) return;

    
    uint32_t sts = op_r32(hc, OP_USBSTS);
    if (sts & USBSTS_EINT)
        op_w32(hc, OP_USBSTS, USBSTS_EINT);

    
    uint32_t iman = ir_r32(hc, IR_IMAN);
    if (iman & IMAN_IP)
        ir_w32(hc, IR_IMAN, iman | IMAN_IP);

    
    while (event_pending(&hc->event_ring)) {
        xhci_trb_t *ev   = &hc->event_ring.trbs[hc->event_ring.dequeue];
        uint8_t     type = (uint8_t)TRB_GET_TYPE(ev->control);

        if (type == TRB_TYPE_PORT_STATUS_CHANGE) {
            
            uint8_t port = (uint8_t)((ev->parameter >> 24) & 0xFF);
            uint32_t sc  = port_r32(hc, port, 0);
            
            port_w32(hc, port, 0, (sc & ~PORTSC_W1C_MASK) | (sc & PORTSC_W1C_MASK));
            log_info(XHCI_MOD, "Hot-plug event on port %u (PORTSC=%08x)", port, sc);
        }
        event_consume(hc);
    }
}





static int cmd_send(xhci_controller_t *hc, const xhci_trb_t *cmd, xhci_trb_t *result_out) {
    ring_push(&hc->cmd_ring, cmd);
    doorbell(hc, 0, 0);   

    xhci_trb_t ev;
    if (event_poll(hc, TRB_TYPE_CMD_COMPLETION, &ev, 5000) != 0) {
        log_err(XHCI_MOD, "Command timeout");
        return -1;
    }
    if (result_out) *result_out = ev;

    uint32_t cc = (ev.status >> 24) & 0xFFu;
    if (cc != CC_SUCCESS) {
        log_err(XHCI_MOD, "Command failed: completion code %u", cc);
        return (int)cc;
    }
    return 0;
}

static int cmd_enable_slot(xhci_controller_t *hc, uint8_t *slot_out) {
    xhci_trb_t cmd = { .control = TRB_CTRL_TYPE(TRB_TYPE_ENABLE_SLOT_CMD) };
    xhci_trb_t ev;
    int rc = cmd_send(hc, &cmd, &ev);
    if (rc == 0 && slot_out)
        *slot_out = (uint8_t)TRB_GET_SLOT(ev.control);
    return rc;
}

static int cmd_disable_slot(xhci_controller_t *hc, uint8_t slot) {
    xhci_trb_t cmd = {
        .control = TRB_CTRL_TYPE(TRB_TYPE_DISABLE_SLOT_CMD) | TRB_CTRL_SLOT(slot),
    };
    return cmd_send(hc, &cmd, NULL);
}



static int cmd_address_device(xhci_controller_t *hc, uint8_t slot,
                               uint64_t in_ctx_phys, bool bsr) {
    xhci_trb_t cmd = {
        .parameter = in_ctx_phys,
        .control   = TRB_CTRL_TYPE(TRB_TYPE_ADDRESS_DEVICE_CMD) |
                     TRB_CTRL_SLOT(slot) |
                     (bsr ? TRB_CTRL_BSR : 0u),
    };
    return cmd_send(hc, &cmd, NULL);
}

static int cmd_evaluate_context(xhci_controller_t *hc, uint8_t slot, uint64_t in_ctx_phys) {
    xhci_trb_t cmd = {
        .parameter = in_ctx_phys,
        .control   = TRB_CTRL_TYPE(TRB_TYPE_EVAL_CTX_CMD) | TRB_CTRL_SLOT(slot),
    };
    return cmd_send(hc, &cmd, NULL);
}










static uint64_t build_setup_packet(uint8_t bmRequestType, uint8_t bRequest,
                                    uint16_t wValue, uint16_t wIndex, uint16_t wLength) {
    return (uint64_t)bmRequestType
         | ((uint64_t)bRequest  <<  8)
         | ((uint64_t)wValue    << 16)
         | ((uint64_t)wIndex    << 32)
         | ((uint64_t)wLength   << 48);
}



static int control_transfer(xhci_controller_t *hc, xhci_device_t *dev,
                             uint8_t bmRequestType, uint8_t bRequest,
                             uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                             uint64_t data_phys) {
    xhci_ring_t *ring  = &dev->ep0_ring;
    bool         dir_in = (bmRequestType & USB_DIR_IN) != 0;

    
    {
        uint32_t trt = (wLength == 0)  ? TRB_TRT_NO_DATA
                     : (dir_in)        ? TRB_TRT_IN_DATA
                                       : TRB_TRT_OUT_DATA;
        xhci_trb_t t = {
            .parameter = build_setup_packet(bmRequestType, bRequest,
                                             wValue, wIndex, wLength),
            .status    = 8,   
            .control   = TRB_CTRL_TYPE(TRB_TYPE_SETUP_STAGE) | TRB_CTRL_IDT | trt,
        };
        ring_push(ring, &t);
    }

    
    if (wLength > 0) {
        xhci_trb_t t = {
            .parameter = data_phys,
            .status    = wLength,
            .control   = TRB_CTRL_TYPE(TRB_TYPE_DATA_STAGE)
                       | TRB_CTRL_ISP
                       | (dir_in ? TRB_CTRL_DIR_IN : 0u),
        };
        ring_push(ring, &t);
    }

    
    
    {
        bool      status_in = (wLength == 0) || !dir_in;
        xhci_trb_t t = {
            .parameter = 0,
            .status    = 0,
            .control   = TRB_CTRL_TYPE(TRB_TYPE_STATUS_STAGE)
                       | TRB_CTRL_IOC
                       | (status_in ? TRB_CTRL_DIR_IN : 0u),
        };
        ring_push(ring, &t);
    }

    
    doorbell(hc, dev->slot_id, 1);

    
    xhci_trb_t ev;
    if (event_poll(hc, TRB_TYPE_TRANSFER_EVENT, &ev, 5000) != 0) {
        log_err(XHCI_MOD, "Control transfer timeout (slot %u)", dev->slot_id);
        return -1;
    }
    uint32_t cc = (ev.status >> 24) & 0xFFu;
    if (cc != CC_SUCCESS && cc != CC_SHORT_PACKET) {
        log_err(XHCI_MOD, "Control transfer error: CC=%u (slot %u)", cc, dev->slot_id);
        return (int)cc;
    }
    return 0;
}



static uint8_t port_speed(xhci_controller_t *hc, uint8_t port) {
    return (uint8_t)((port_r32(hc, port, 0) & PORTSC_SPEED_MASK) >> PORTSC_SPEED_SHIFT);
}





static int port_reset(xhci_controller_t *hc, uint8_t port) {
    uint32_t sc = port_r32(hc, port, 0);
    
    sc = (sc & ~PORTSC_W1C_MASK) | PORTSC_PR;
    port_w32(hc, port, 0, sc);

    
    for (uint32_t t = 0; t < 5000; t++) {
        uint32_t v = port_r32(hc, port, 0);
        if (v & PORTSC_PRC) {
            
            port_w32(hc, port, 0, (v & ~PORTSC_W1C_MASK) | PORTSC_PRC);
            return 0;
        }
        udelay(100);
    }
    log_err(XHCI_MOD, "Port %u reset timed out", port);
    return -1;
}



static int alloc_device_contexts(xhci_controller_t *hc, xhci_device_t *dev) {
    
    size_t dev_sz = hc->ctx_stride * 32u;
    
    size_t in_sz  = hc->ctx_stride * 33u;

    dev->dev_ctx_dma = dma_alloc(dev_sz, DMA_ZONE_NORMAL);
    if (!dev->dev_ctx_dma) return -1;

    dev->in_ctx_dma = dma_alloc(in_sz, DMA_ZONE_NORMAL);
    if (!dev->in_ctx_dma) {
        dma_free(dev->dev_ctx_dma);
        dev->dev_ctx_dma = NULL;
        return -1;
    }

    dev->dev_ctx = dev->dev_ctx_dma->virt;
    dev->in_ctx  = dev->in_ctx_dma->virt;
    memset(dev->dev_ctx, 0, dev_sz);
    memset(dev->in_ctx,  0, in_sz);
    return 0;
}



static int enumerate_device(xhci_controller_t *hc, uint8_t port) {
    log_info(XHCI_MOD, "Enumerating device on port %u", port);

    
    if (port_reset(hc, port) != 0) return -1;
    udelay(10000);  

    if (!(port_r32(hc, port, 0) & PORTSC_CCS)) {
        log_warn(XHCI_MOD, "Port %u: device vanished after reset", port);
        return -1;
    }

    uint8_t speed = port_speed(hc, port);
    log_debug(XHCI_MOD, "Port %u speed: %u", port, speed);

    
    uint16_t init_mps;
    switch (speed) {
        case USB_SPEED_LOW:        init_mps = 8;   break;
        case USB_SPEED_FULL:       init_mps = 64;  break;
        case USB_SPEED_HIGH:       init_mps = 64;  break;
        default :    init_mps = 512; break;
    }

    
    uint8_t slot_id = 0;
    if (cmd_enable_slot(hc, &slot_id) != 0) {
        log_err(XHCI_MOD, "EnableSlot failed");
        return -1;
    }
    log_info(XHCI_MOD, "Slot %u assigned for port %u", slot_id, port);

    xhci_device_t *dev = &hc->devices[slot_id];
    memset(dev, 0, sizeof(*dev));
    dev->present        = true;
    dev->slot_id        = slot_id;
    dev->port           = port;
    dev->speed          = speed;
    dev->max_packet_ep0 = init_mps;

    
    if (alloc_device_contexts(hc, dev) != 0) goto fail_pre_slot;

    
    if (ring_alloc(&dev->ep0_ring) != 0) goto fail_post_ctx;

    
    hc->dcbaa[slot_id] = dev->dev_ctx_dma->phys;

    
    {
        xhci_input_ctrl_ctx_t *ic = in_ctrl_ctx(hc, dev->in_ctx);
        ic->add_flags  = (1u << 0) | (1u << 1);  
        ic->drop_flags = 0;

        xhci_slot_ctx_t *sc = in_slot_ctx(hc, dev->in_ctx);
        sc->route_string  = 0;          
        sc->speed         = speed;
        sc->ctx_entries   = 1;          
        sc->root_hub_port = port;
        sc->intr_target   = 0;

        xhci_ep_ctx_t *ep0 = in_ep_ctx(hc, dev->in_ctx, 1); 
        ep0->ep_type       = 4;         
        ep0->max_packet    = init_mps;
        ep0->max_burst     = 0;
        ep0->cerr          = 3;         
        ep0->avg_trb_len   = 8;
        ep0->interval      = 0;
        
        ep0->tr_dequeue_ptr = dev->ep0_ring.phys | 1u;
    }

    
    if (cmd_address_device(hc, slot_id, dev->in_ctx_dma->phys, false) != 0) {
        log_err(XHCI_MOD, "AddressDevice failed (slot %u)", slot_id);
        goto fail_full;
    }
    log_ok(XHCI_MOD, "Slot %u: device now addressed", slot_id);

    
    dma_buf_t *desc_dma = dma_alloc(256, DMA_ZONE_NORMAL);
    if (!desc_dma) goto fail_full;
    memset(desc_dma->virt, 0, 256);

    if (control_transfer(hc, dev,
                         USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                         USB_REQ_GET_DESCRIPTOR,
                         (uint16_t)(USB_DESC_DEVICE << 8), 0, 8,
                         desc_dma->phys) != 0) {
        log_err(XHCI_MOD, "GET_DESCRIPTOR (8 B) failed");
        dma_free(desc_dma);
        goto fail_full;
    }

    
    uint8_t raw_mps = ((uint8_t *)desc_dma->virt)[7];
    uint16_t real_mps;
    
    if (speed == USB_SPEED_SUPER || speed == USB_SPEED_SUPER_PLUS)
        real_mps = (uint16_t)(1u << raw_mps);
    else
        real_mps = raw_mps;

    log_debug(XHCI_MOD, "bMaxPacketSize0 = %u", real_mps);

    
    if (real_mps != init_mps) {
        dev->max_packet_ep0 = real_mps;

        
        memset(dev->in_ctx, 0, hc->ctx_stride * 33u);
        xhci_input_ctrl_ctx_t *ic = in_ctrl_ctx(hc, dev->in_ctx);
        ic->add_flags = (1u << 1);  

        xhci_ep_ctx_t *ep0 = in_ep_ctx(hc, dev->in_ctx, 1);
        ep0->ep_type       = 4;
        ep0->max_packet    = real_mps;
        ep0->max_burst     = 0;
        ep0->cerr          = 3;
        ep0->avg_trb_len   = 8;
        ep0->tr_dequeue_ptr = dev->ep0_ring.phys | 1u;

        if (cmd_evaluate_context(hc, slot_id, dev->in_ctx_dma->phys) != 0)
            log_warn(XHCI_MOD, "EvaluateContext failed — continuing with old MPS");
    }

    
    memset(desc_dma->virt, 0, 256);
    if (control_transfer(hc, dev,
                         USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                         USB_REQ_GET_DESCRIPTOR,
                         (uint16_t)(USB_DESC_DEVICE << 8), 0, 18,
                         desc_dma->phys) != 0) {
        log_err(XHCI_MOD, "GET_DESCRIPTOR (18 B) failed");
        dma_free(desc_dma);
        goto fail_full;
    }

    memcpy(&dev->dev_desc, desc_dma->virt, sizeof(usb_device_descriptor_t));
    dma_free(desc_dma);

    log_ok(XHCI_MOD, "Device on slot %u: VID=%04x PID=%04x Class=%02x/%02x/%02x Cfgs=%u USB=%x.%x",
           slot_id,
           dev->dev_desc.idVendor, dev->dev_desc.idProduct,
           dev->dev_desc.bDeviceClass, dev->dev_desc.bDeviceSubClass,
           dev->dev_desc.bDeviceProtocol,
           dev->dev_desc.bNumConfigurations,
           dev->dev_desc.bcdUSB >> 8, dev->dev_desc.bcdUSB & 0xFF);

    return 0;

    
fail_full:
    ring_free(&dev->ep0_ring);
fail_post_ctx:
    dma_free(dev->dev_ctx_dma);
    dma_free(dev->in_ctx_dma);
    hc->dcbaa[slot_id] = 0;
fail_pre_slot:
    cmd_disable_slot(hc, slot_id);
    dev->present = false;
    return -1;
}







static int setup_scratchpad(xhci_controller_t *hc) {
    uint32_t hi = (hc->hcs_params2 >> 21) & 0x1Fu;
    uint32_t lo = (hc->hcs_params2 >> 27) & 0x1Fu;
    hc->num_scratch = (hi << 5) | lo;

    if (hc->num_scratch == 0) return 0;
    log_debug(XHCI_MOD, "Allocating %u scratchpad pages", hc->num_scratch);

    
    uint32_t pgsz_reg = op_r32(hc, OP_PAGESIZE) & 0xFFFFu;
    uint32_t page_sz  = 4096u;
    for (int i = 0; i < 16; i++) {
        if (pgsz_reg & (1u << i)) { page_sz = 1u << (i + 12); break; }
    }

    
    hc->scratch_arr_dma = dma_alloc(hc->num_scratch * 8u, DMA_ZONE_NORMAL);
    if (!hc->scratch_arr_dma) return -1;
    memset(hc->scratch_arr_dma->virt, 0, hc->num_scratch * 8u);

    hc->scratch_bufs = (dma_buf_t **)kmalloc(hc->num_scratch * sizeof(dma_buf_t *));
    if (!hc->scratch_bufs) return -1;

    uint64_t *arr = (uint64_t *)hc->scratch_arr_dma->virt;
    for (uint32_t i = 0; i < hc->num_scratch; i++) {
        hc->scratch_bufs[i] = dma_alloc(page_sz, DMA_ZONE_NORMAL);
        if (!hc->scratch_bufs[i]) return -1;
        memset(hc->scratch_bufs[i]->virt, 0, page_sz);
        arr[i] = hc->scratch_bufs[i]->phys;
    }

    
    hc->dcbaa[0] = hc->scratch_arr_dma->phys;
    return 0;
}



static void scan_ports(xhci_controller_t *hc) {
    for (uint8_t p = 1; p <= hc->max_ports; p++) {
        uint32_t sc = port_r32(hc, p, 0);
        if (sc & PORTSC_CCS) {
            log_debug(XHCI_MOD, "Port %u: device present (PORTSC=%08x)", p, sc);
            enumerate_device(hc, p);
        }
    }
}



static int hc_init(xhci_controller_t *hc) {
    
    hc->cap_length  = cap_r8 (hc, 0x00);
    hc->hci_version = cap_r16(hc, 0x02);
    hc->hcs_params1 = cap_r32(hc, 0x04);
    hc->hcs_params2 = cap_r32(hc, 0x08);
    hc->hcc_params1 = cap_r32(hc, 0x10);

    hc->max_slots  = (uint8_t) ((hc->hcs_params1 >>  0) & 0xFFu);
    hc->max_ports  = (uint8_t) ((hc->hcs_params1 >> 24) & 0xFFu);
    hc->csz        = (hc->hcc_params1 >> 2) & 1u;
    hc->ctx_stride = hc->csz ? 64u : 32u;

    uint32_t dboff  = cap_r32(hc, 0x14);
    uint32_t rtsoff = cap_r32(hc, 0x18);

    hc->op_base = hc->cap_base + hc->cap_length;
    hc->db_base = (volatile uint32_t *)(hc->cap_base + dboff);
    hc->rt_base = hc->cap_base + rtsoff;

    if (hc->max_slots > XHCI_MAX_SLOTS) hc->max_slots = XHCI_MAX_SLOTS;
    if (hc->max_ports > XHCI_MAX_PORTS) hc->max_ports = XHCI_MAX_PORTS;

    log_info(XHCI_MOD, "xHCI v%u.%u | ports=%u slots=%u ctx_stride=%u",
             hc->hci_version >> 8, hc->hci_version & 0xFFu,
             hc->max_ports, hc->max_slots, hc->ctx_stride);

    
    if (op_r32(hc, OP_USBCMD) & USBCMD_RUN) {
        op_w32(hc, OP_USBCMD, op_r32(hc, OP_USBCMD) & ~USBCMD_RUN);
        if (op_wait(hc, OP_USBSTS, USBSTS_HCH, USBSTS_HCH, 1000) != 0) {
            log_crit(XHCI_MOD, "HC did not halt");
            return -1;
        }
    }

    
    op_w32(hc, OP_USBCMD, USBCMD_RST);
    
    if (op_wait(hc, OP_USBCMD, USBCMD_RST, 0, 1000) != 0) {
        log_crit(XHCI_MOD, "Reset timed out (HCRST)");
        return -1;
    }
    
    if (op_wait(hc, OP_USBSTS, USBSTS_CNR, 0, 1000) != 0) {
        log_crit(XHCI_MOD, "Reset timed out (CNR)");
        return -1;
    }
    log_ok(XHCI_MOD, "HC reset complete");

    
    op_w32(hc, OP_CONFIG, (op_r32(hc, OP_CONFIG) & ~0xFFu) | hc->max_slots);

    
    
    size_t dcbaa_sz = ((size_t)hc->max_slots + 1u) * 8u;
    hc->dcbaa_dma = dma_alloc(dcbaa_sz, DMA_ZONE_NORMAL);
    if (!hc->dcbaa_dma) { log_crit(XHCI_MOD, "DCBAA alloc failed"); return -1; }
    hc->dcbaa = (uint64_t *)hc->dcbaa_dma->virt;
    memset(hc->dcbaa, 0, dcbaa_sz);
    op_w64(hc, OP_DCBAAP, hc->dcbaa_dma->phys);

    
    if (setup_scratchpad(hc) != 0) {
        log_crit(XHCI_MOD, "Scratchpad setup failed");
        return -1;
    }

    
    if (ring_alloc(&hc->cmd_ring) != 0) {
        log_crit(XHCI_MOD, "Command ring alloc failed");
        return -1;
    }
    
    op_w64(hc, OP_CRCR, hc->cmd_ring.phys | CRCR_RCS);

    
    if (ring_alloc(&hc->event_ring) != 0) {
        log_crit(XHCI_MOD, "Event ring alloc failed");
        return -1;
    }

    hc->erst_dma = dma_alloc(sizeof(xhci_erst_entry_t), DMA_ZONE_NORMAL);
    if (!hc->erst_dma) { log_crit(XHCI_MOD, "ERST alloc failed"); return -1; }

    xhci_erst_entry_t *erst = (xhci_erst_entry_t *)hc->erst_dma->virt;
    memset(erst, 0, sizeof(*erst));
    erst->ring_segment_base = hc->event_ring.phys;
    erst->ring_segment_size = XHCI_RING_SIZE;

    
    ir_w32(hc, IR_ERSTSZ, 1);                      
    ir_w64(hc, IR_ERDP,   hc->event_ring.phys);    
    ir_w64(hc, IR_ERSTBA, hc->erst_dma->phys);     

    
    ir_w32(hc, IR_IMOD, 0x000003E8u);
    ir_w32(hc, IR_IMAN, IMAN_IE);                  

    
    
    
    
    int irq = pci_enable_msi(hc->pci, 0x40, xhci_irq_handler);
    if (irq < 0) {
        log_warn(XHCI_MOD, "MSI unavailable, falling back to INTx");
        irq = pci_enable_intx(hc->pci, xhci_irq_handler);
    }
    if (irq < 0) {
        log_crit(XHCI_MOD, "Could not configure IRQ");
        return -1;
    }
    hc->irq_vector = irq;
    log_ok(XHCI_MOD, "IRQ %d configured", irq);

    
    op_w32(hc, OP_USBCMD, USBCMD_RUN | USBCMD_INTE | USBCMD_HSEE);
    if (op_wait(hc, OP_USBSTS, USBSTS_HCH, 0, 1000) != 0) {
        log_crit(XHCI_MOD, "HC failed to start (HCH still set)");
        return -1;
    }
    log_ok(XHCI_MOD, "HC running");

    
    {
        xhci_trb_t noop = { .control = TRB_CTRL_TYPE(TRB_TYPE_NO_OP_CMD) };
        if (cmd_send(hc, &noop, NULL) != 0)
            log_warn(XHCI_MOD, "No-Op command did not complete cleanly");
        else
            log_ok(XHCI_MOD, "Command ring verified");
    }

    return 0;
}



xhci_controller_t *xhci_get_controller(void) {
    return g_hc;
}

int xhci_init(void) {
    
    
    
    pci_device_t *pci = pci_get_devices();
    while (pci) {
        if (pci->class_code == 0x0C &&
            pci->subclass   == 0x03 &&
            pci->prog_if    == 0x30) break;
        pci = pci->next;
    }
    if (!pci) {
        log_err(XHCI_MOD, "No xHCI controller found in PCI device list");
        return -1;
    }
    log_info(XHCI_MOD, "Found xHCI: %04x:%04x (bus %u slot %u fn %u)",
             pci->vendor_id, pci->device_id, pci->bus, pci->slot, pci->function);

    
    xhci_controller_t *hc = (xhci_controller_t *)kmalloc(sizeof(xhci_controller_t));
    if (!hc) return -1;
    memset(hc, 0, sizeof(xhci_controller_t));
    hc->pci = pci;
    g_hc    = hc;

    
    pci_enable_bus_mastering(pci);
    pci_set_command(pci, pci_get_command(pci) | PCI_CMD_MEM_SPACE | PCI_CMD_BUS_MASTER);

    
    uintptr_t bar0 = pci_map_bar(pci, 0);
    if (!bar0) {
        log_crit(XHCI_MOD, "Failed to map BAR0");
        kfree(hc);
        g_hc = NULL;
        return -1;
    }
    hc->cap_base = (volatile uint8_t *)bar0;

    
    if (hc_init(hc) != 0) {
        log_crit(XHCI_MOD, "Initialization failed");
        kfree(hc);
        g_hc = NULL;
        return -1;
    }

    
    scan_ports(hc);
    return 0;
}