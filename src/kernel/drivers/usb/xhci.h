#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <drivers/pci/pci.h>
#include <mem/dma.h>


#define XHCI_RING_SIZE      256     // TRBs per ring; last slot is always a Link TRB
#define XHCI_MAX_SLOTS      64      // Max device slots we support
#define XHCI_MAX_PORTS      32      // Max root-hub ports we track


#define TRB_TYPE_NORMAL             1
#define TRB_TYPE_SETUP_STAGE        2
#define TRB_TYPE_DATA_STAGE         3
#define TRB_TYPE_STATUS_STAGE       4
#define TRB_TYPE_ISOCH              5
#define TRB_TYPE_LINK               6
#define TRB_TYPE_ENABLE_SLOT_CMD    9
#define TRB_TYPE_DISABLE_SLOT_CMD   10
#define TRB_TYPE_ADDRESS_DEVICE_CMD 11
#define TRB_TYPE_CONFIG_EP_CMD      12
#define TRB_TYPE_EVAL_CTX_CMD       13
#define TRB_TYPE_RESET_EP_CMD       14
#define TRB_TYPE_STOP_EP_CMD        15
#define TRB_TYPE_NO_OP_CMD          23
#define TRB_TYPE_TRANSFER_EVENT     32
#define TRB_TYPE_CMD_COMPLETION     33
#define TRB_TYPE_PORT_STATUS_CHANGE 34


#define TRB_CTRL_CYCLE          (1u << 0)
#define TRB_CTRL_ENT            (1u << 1)   // Evaluate Next TRB
#define TRB_CTRL_ISP            (1u << 2)   // Interrupt on Short Packet
#define TRB_CTRL_NS             (1u << 3)   // No Snoop
#define TRB_CTRL_CH             (1u << 4)   // Chain
#define TRB_CTRL_IOC            (1u << 5)   // Interrupt on Completion
#define TRB_CTRL_IDT            (1u << 6)   // Immediate Data (Setup Stage)
#define TRB_LINK_TC             (1u << 1)   // Toggle Cycle (Link TRB)
#define TRB_CTRL_BSR            (1u << 9)   // Block Set Address Request
#define TRB_CTRL_DIR_IN         (1u << 16)  // Direction=IN (Data/Status Stage)
#define TRB_CTRL_TYPE(t)        ((uint32_t)(t) << 10)
#define TRB_GET_TYPE(ctrl)      (((ctrl) >> 10) & 0x3Fu)
#define TRB_CTRL_SLOT(s)        ((uint32_t)(s) << 24)
#define TRB_GET_SLOT(ctrl)      (((ctrl) >> 24) & 0xFFu)
#define TRB_CTRL_EP(ep)         ((uint32_t)(ep) << 16)
#define TRB_GET_EP(ctrl)        (((ctrl) >> 16) & 0x1Fu)


#define TRB_TRT_NO_DATA         (0u << 16)
#define TRB_TRT_OUT_DATA        (2u << 16)
#define TRB_TRT_IN_DATA         (3u << 16)


#define CC_SUCCESS              1
#define CC_DATA_BUFFER_ERROR    2
#define CC_BABBLE_ERROR         3
#define CC_USB_XACT_ERROR       4
#define CC_TRB_ERROR            5
#define CC_STALL_ERROR          6
#define CC_RESOURCE_ERROR       7
#define CC_SHORT_PACKET         13


#define USB_SPEED_FULL          1
#define USB_SPEED_LOW           2
#define USB_SPEED_HIGH          3
#define USB_SPEED_SUPER         4
#define USB_SPEED_SUPER_PLUS    5


#define USB_DIR_OUT             0x00
#define USB_DIR_IN              0x80
#define USB_TYPE_STANDARD       0x00
#define USB_TYPE_CLASS          0x20
#define USB_TYPE_VENDOR         0x40
#define USB_RECIP_DEVICE        0x00
#define USB_RECIP_INTERFACE     0x01
#define USB_RECIP_ENDPOINT      0x02


#define USB_REQ_GET_STATUS          0x00
#define USB_REQ_CLEAR_FEATURE       0x01
#define USB_REQ_SET_FEATURE         0x03
#define USB_REQ_SET_ADDRESS         0x05
#define USB_REQ_GET_DESCRIPTOR      0x06
#define USB_REQ_SET_CONFIGURATION   0x09


#define USB_DESC_DEVICE             0x01
#define USB_DESC_CONFIG             0x02
#define USB_DESC_STRING             0x03
#define USB_DESC_INTERFACE          0x04
#define USB_DESC_ENDPOINT           0x05




typedef struct __attribute__((packed, aligned(16))) {
    uint64_t    parameter;  
    uint32_t    status;     
    uint32_t    control;    
} xhci_trb_t;


typedef struct __attribute__((packed, aligned(64))) {
    uint64_t    ring_segment_base;  
    uint16_t    ring_segment_size;  
    uint16_t    _res1;
    uint32_t    _res2;
} xhci_erst_entry_t;


typedef struct __attribute__((packed)) {
    
    uint32_t    route_string  : 20;
    uint32_t    speed         : 4;
    uint32_t    _r0           : 1;
    uint32_t    mtt           : 1;
    uint32_t    hub           : 1;
    uint32_t    ctx_entries   : 5;
    
    uint16_t    max_exit_lat;
    uint8_t     root_hub_port;
    uint8_t     num_ports;
    
    uint8_t     parent_hub_slot_id;
    uint8_t     parent_port_num;
    uint16_t    tt_think_time  : 2;
    uint16_t    _r1            : 4;
    uint16_t    intr_target    : 10;
    
    uint8_t     usb_dev_addr;
    uint32_t    _r2            : 19;
    uint32_t    slot_state     : 5;
    
    uint32_t    _pad[4];
} xhci_slot_ctx_t;


typedef struct __attribute__((packed)) {
    
    uint32_t    ep_state      : 3;
    uint32_t    _r0           : 5;
    uint32_t    mult          : 2;
    uint32_t    max_pstreams  : 5;
    uint32_t    lsa           : 1;
    uint32_t    interval      : 8;
    uint32_t    _r1           : 8;
    
    uint32_t    _r2           : 1;
    uint32_t    cerr          : 2;  
    uint32_t    ep_type       : 3;  
    uint32_t    _r3           : 1;
    uint32_t    hid           : 1;
    uint32_t    max_burst     : 8;
    uint32_t    max_packet    : 16;
    
    uint64_t    tr_dequeue_ptr;
    
    uint16_t    avg_trb_len;
    uint16_t    max_esit_lo;
    
    uint32_t    _pad[3];
} xhci_ep_ctx_t;


typedef struct __attribute__((packed)) {
    uint32_t    drop_flags;     
    uint32_t    add_flags;      
    uint32_t    _pad[6];
} xhci_input_ctrl_ctx_t;




typedef struct {
    xhci_trb_t *trbs;      
    uint64_t    phys;       
    uint32_t    enqueue;    
    uint32_t    dequeue;    
    uint8_t     cycle;      
    dma_buf_t  *dma;        
} xhci_ring_t;


typedef struct __attribute__((packed)) {
    uint8_t     bLength;
    uint8_t     bDescriptorType;
    uint16_t    bcdUSB;
    uint8_t     bDeviceClass;
    uint8_t     bDeviceSubClass;
    uint8_t     bDeviceProtocol;
    uint8_t     bMaxPacketSize0;
    uint16_t    idVendor;
    uint16_t    idProduct;
    uint16_t    bcdDevice;
    uint8_t     iManufacturer;
    uint8_t     iProduct;
    uint8_t     iSerialNumber;
    uint8_t     bNumConfigurations;
} usb_device_descriptor_t;


typedef struct {
    bool                    present;
    uint8_t                 slot_id;
    uint8_t                 port;           
    uint8_t                 speed;          

    dma_buf_t              *dev_ctx_dma;    
    dma_buf_t              *in_ctx_dma;     
    void                   *dev_ctx;        
    void                   *in_ctx;         

    xhci_ring_t             ep0_ring;       
    uint16_t                max_packet_ep0;

    usb_device_descriptor_t dev_desc;
} xhci_device_t;


typedef struct {
    pci_device_t       *pci;

    
    volatile uint8_t   *cap_base;   
    volatile uint8_t   *op_base;    
    volatile uint32_t  *db_base;    
    volatile uint8_t   *rt_base;    

    
    uint8_t             cap_length;
    uint16_t            hci_version;
    uint32_t            hcs_params1;
    uint32_t            hcs_params2;
    uint32_t            hcc_params1;
    uint8_t             max_slots;
    uint8_t             max_ports;
    bool                csz;          
    uint32_t            ctx_stride;   

    
    dma_buf_t          *dcbaa_dma;
    uint64_t           *dcbaa;        

    
    xhci_ring_t         cmd_ring;

    
    xhci_ring_t         event_ring;
    dma_buf_t          *erst_dma;

    
    dma_buf_t          *scratch_arr_dma;
    dma_buf_t         **scratch_bufs;
    uint32_t            num_scratch;

    
    xhci_device_t       devices[XHCI_MAX_SLOTS + 1];

    int                 irq_vector;
} xhci_controller_t;





int xhci_init(void);


xhci_controller_t *xhci_get_controller(void);