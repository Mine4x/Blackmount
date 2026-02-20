#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <arch/x86_64/irq.h>

#define PCI_REG_VENDOR_ID       0x00
#define PCI_REG_DEVICE_ID       0x02
#define PCI_REG_COMMAND         0x04
#define PCI_REG_STATUS          0x06
#define PCI_REG_REVISION_ID     0x08
#define PCI_REG_PROG_IF         0x09
#define PCI_REG_SUBCLASS        0x0A
#define PCI_REG_CLASS           0x0B
#define PCI_REG_CACHE_LINE      0x0C
#define PCI_REG_LATENCY         0x0D
#define PCI_REG_HEADER_TYPE     0x0E
#define PCI_REG_BIST            0x0F
#define PCI_REG_BAR0            0x10
#define PCI_REG_BAR1            0x14
#define PCI_REG_BAR2            0x18
#define PCI_REG_BAR3            0x1C
#define PCI_REG_BAR4            0x20
#define PCI_REG_BAR5            0x24
#define PCI_REG_SUBSYS_VENDOR   0x2C
#define PCI_REG_SUBSYS_ID       0x2E
#define PCI_REG_CAPABILITIES    0x34
#define PCI_REG_INTERRUPT_LINE  0x3C
#define PCI_REG_INTERRUPT_PIN   0x3D

#define PCI_CMD_IO_SPACE        (1 << 0)
#define PCI_CMD_MEM_SPACE       (1 << 1)
#define PCI_CMD_BUS_MASTER      (1 << 2)
#define PCI_CMD_INT_DISABLE     (1 << 10)

#define PCI_CAP_MSI             0x05
#define PCI_CAP_MSIX            0x11

#define PCI_MSI_CTRL_ENABLE     (1 << 0)
#define PCI_MSI_CTRL_64BIT      (1 << 7)
#define PCI_MSI_CTRL_MASKING    (1 << 8)

#define PCI_MSIX_CTRL_ENABLE    (1 << 15)
#define PCI_MSIX_CTRL_MASK_ALL  (1 << 14)
#define PCI_MSIX_CTRL_TABLE_SZ  0x07FF

#define PCI_BAR_TYPE_IO         0x01
#define PCI_BAR_TYPE_MEM_MASK   0x06
#define PCI_BAR_TYPE_MEM32      0x00
#define PCI_BAR_TYPE_MEM64      0x04
#define PCI_BAR_PREFETCHABLE    0x08

#define PCI_MAX_BARS            6

#define PCI_MSIX_MAX_VECTORS    2048

typedef enum {
    PCI_IRQ_NONE   = 0,
    PCI_IRQ_INTX   = 1,
    PCI_IRQ_MSI    = 2,
    PCI_IRQ_MSIX   = 3,
} pci_irq_mode_t;

typedef struct {
    uint64_t    phys_base;      // physical base address (after masking flags)
    uint64_t    virt_base;      // virtual (mapped) address; 0 if not yet mapped
    uint64_t    size;           // size in bytes
    bool        is_io;          // true = port I/O, false = memory-mapped
    bool        is_64bit;       // true = 64-bit BAR (spans two config slots)
    bool        prefetchable;
} pci_bar_t;

typedef struct pci_device {
    // Location
    uint16_t    segment;
    uint8_t     bus;
    uint8_t     slot;
    uint8_t     function;

    // Identity
    uint16_t    vendor_id;
    uint16_t    device_id;
    uint8_t     class_code;
    uint8_t     subclass;
    uint8_t     prog_if;
    uint8_t     revision;
    uint8_t     header_type;

    // Subsystem
    uint16_t    subsys_vendor_id;
    uint16_t    subsys_id;

    // BARs
    pci_bar_t   bars[PCI_MAX_BARS];

    // Interrupt state
    pci_irq_mode_t  irq_mode;
    int             irq_vector;     // INTx line or MSI vector base
    uint8_t         msi_cap_off;    // offset of MSI cap in config space (0 = none)
    uint8_t         msix_cap_off;   // offset of MSI-X cap in config space (0 = none)
    volatile uint32_t *msix_table;  // mapped MSI-X table (NULL if unused)
    uint16_t        msix_table_size;

    // Linked list
    struct pci_device *next;
} pci_device_t;

void pci_init(void);

pci_device_t *pci_get_devices(void);

pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id);


pci_device_t *pci_find_device_by_class(uint8_t class_code, uint8_t subclass);


uint8_t  pci_read_config8 (pci_device_t *dev, uint16_t offset);
uint16_t pci_read_config16(pci_device_t *dev, uint16_t offset);
uint32_t pci_read_config32(pci_device_t *dev, uint16_t offset);

void pci_write_config8 (pci_device_t *dev, uint16_t offset, uint8_t  value);
void pci_write_config16(pci_device_t *dev, uint16_t offset, uint16_t value);
void pci_write_config32(pci_device_t *dev, uint16_t offset, uint32_t value);


uint16_t pci_get_command(pci_device_t *dev);
void     pci_set_command(pci_device_t *dev, uint16_t cmd);


void pci_enable_bus_mastering(pci_device_t *dev);
void pci_probe_bars(pci_device_t *dev);
uintptr_t pci_map_bar(pci_device_t *dev, int index);
int pci_enable_intx(pci_device_t *dev, IRQHandler handler);
int pci_enable_msi(pci_device_t *dev, int vector, IRQHandler handler);
int pci_enable_msix(pci_device_t *dev, int vector_base,
                    IRQHandler *handlers, uint16_t count);

void pci_disable_irq(pci_device_t *dev);

uint8_t  pci_io_read8 (pci_device_t *dev, int bar_index, uint32_t offset);
uint16_t pci_io_read16(pci_device_t *dev, int bar_index, uint32_t offset);
uint32_t pci_io_read32(pci_device_t *dev, int bar_index, uint32_t offset);
void pci_io_write8 (pci_device_t *dev, int bar_index, uint32_t offset, uint8_t  val);
void pci_io_write16(pci_device_t *dev, int bar_index, uint32_t offset, uint16_t val);
void pci_io_write32(pci_device_t *dev, int bar_index, uint32_t offset, uint32_t val);

uint8_t  pci_mmio_read8 (pci_device_t *dev, int bar_index, uint64_t offset);
uint16_t pci_mmio_read16(pci_device_t *dev, int bar_index, uint64_t offset);
uint32_t pci_mmio_read32(pci_device_t *dev, int bar_index, uint64_t offset);
uint64_t pci_mmio_read64(pci_device_t *dev, int bar_index, uint64_t offset);
void pci_mmio_write8 (pci_device_t *dev, int bar_index, uint64_t offset, uint8_t  val);
void pci_mmio_write16(pci_device_t *dev, int bar_index, uint64_t offset, uint16_t val);
void pci_mmio_write32(pci_device_t *dev, int bar_index, uint64_t offset, uint32_t val);
void pci_mmio_write64(pci_device_t *dev, int bar_index, uint64_t offset, uint64_t val);

void *pci_mmio_map_impl(uint64_t phys, uint64_t size);