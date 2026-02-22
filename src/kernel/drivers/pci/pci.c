#include "pci.h"
#include <heap.h>
#include <drivers/acpi/acpi.h>
#include <arch/x86_64/irq.h>
#include <mem/vmm.h>
#include <debug.h>
#include <stddef.h>
#include <stdint.h>

#define PCI_MODULE "PCI"

static inline void out32(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint32_t in32(uint16_t port) {
    uint32_t val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void out16(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint16_t in16(uint16_t port) {
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void out8(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t in8(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

typedef struct {
    char        signature[4];       // "MCFG"
    uint32_t    length;
    uint8_t     revision;
    uint8_t     checksum;
    char        oem_id[6];
    char        oem_table_id[8];
    uint32_t    oem_revision;
    uint32_t    creator_id;
    uint32_t    creator_revision;
    uint64_t    reserved;
    // followed by one or more mcfg_entry_t
} __attribute__((packed)) mcfg_table_t;

typedef struct {
    uint64_t    base_address;       // physical base of ECAM region
    uint16_t    segment_group;
    uint8_t     start_bus;
    uint8_t     end_bus;
    uint32_t    reserved;
} __attribute__((packed)) mcfg_entry_t;

#define MAX_MCFG_ENTRIES 16

static struct {
    uint64_t    base;
    uint16_t    segment;
    uint8_t     start_bus;
    uint8_t     end_bus;
} g_ecam_entries[MAX_MCFG_ENTRIES];

static int          g_ecam_count  = 0;
static pci_device_t *g_device_list = NULL;
static bool          g_pci_initialised = false;

// Returns a pointer to the ECAM config page for a given device, or NULL if
// this bus is not covered by any ECAM region.
static volatile uint32_t *ecam_address(uint16_t seg, uint8_t bus,
                                       uint8_t slot, uint8_t func,
                                       uint16_t offset)
{
    for (int i = 0; i < g_ecam_count; i++) {
        if (g_ecam_entries[i].segment == seg &&
            bus >= g_ecam_entries[i].start_bus &&
            bus <= g_ecam_entries[i].end_bus)
        {
            uint64_t phys = g_ecam_entries[i].base
                          + ((uint64_t)(bus  - g_ecam_entries[i].start_bus) << 20)
                          + ((uint64_t)slot  << 15)
                          + ((uint64_t)func  << 12)
                          + offset;
            // Direct-map: assume MMIO regions are identity-mapped or mapped by
            // the caller at initialisation.  Adjust the cast if you use a
            // higher-half offset (e.g. add KERNEL_PHYS_OFFSET).
            return (volatile uint32_t *)(uintptr_t)phys;
        }
    }
    return NULL;
}

// Legacy port-I/O config address
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

static uint32_t legacy_config_addr(uint8_t bus, uint8_t slot,
                                   uint8_t func, uint8_t offset)
{
    return (1u        << 31)
         | ((uint32_t)bus  << 16)
         | ((uint32_t)slot << 11)
         | ((uint32_t)func <<  8)
         | (offset & 0xFC);
}

static uint32_t config_read32_raw(uint16_t seg, uint8_t bus,
                                  uint8_t slot, uint8_t func, uint16_t off)
{
    volatile uint32_t *ecam = ecam_address(seg, bus, slot, func, off & ~3u);
    if (ecam) return *ecam;

    // Fallback: legacy port I/O (only accessible for seg 0, offset < 256)
    out32(PCI_CONFIG_ADDRESS, legacy_config_addr(bus, slot, func, (uint8_t)off));
    return in32(PCI_CONFIG_DATA);
}

static void config_write32_raw(uint16_t seg, uint8_t bus,
                               uint8_t slot, uint8_t func,
                               uint16_t off, uint32_t val)
{
    volatile uint32_t *ecam = ecam_address(seg, bus, slot, func, off & ~3u);
    if (ecam) { *ecam = val; return; }

    out32(PCI_CONFIG_ADDRESS, legacy_config_addr(bus, slot, func, (uint8_t)off));
    out32(PCI_CONFIG_DATA, val);
}

uint32_t pci_read_config32(pci_device_t *dev, uint16_t offset)
{
    return config_read32_raw(dev->segment, dev->bus, dev->slot,
                             dev->function, offset);
}

uint16_t pci_read_config16(pci_device_t *dev, uint16_t offset)
{
    uint32_t val = config_read32_raw(dev->segment, dev->bus, dev->slot,
                                     dev->function, offset & ~3u);
    return (uint16_t)(val >> ((offset & 2) * 8));
}

uint8_t pci_read_config8(pci_device_t *dev, uint16_t offset)
{
    uint32_t val = config_read32_raw(dev->segment, dev->bus, dev->slot,
                                     dev->function, offset & ~3u);
    return (uint8_t)(val >> ((offset & 3) * 8));
}

void pci_write_config32(pci_device_t *dev, uint16_t offset, uint32_t value)
{
    config_write32_raw(dev->segment, dev->bus, dev->slot,
                       dev->function, offset, value);
}

void pci_write_config16(pci_device_t *dev, uint16_t offset, uint16_t value)
{
    uint32_t cur = config_read32_raw(dev->segment, dev->bus, dev->slot,
                                     dev->function, offset & ~3u);
    int shift = (offset & 2) * 8;
    cur &= ~(0xFFFFu << shift);
    cur |=  ((uint32_t)value << shift);
    config_write32_raw(dev->segment, dev->bus, dev->slot,
                       dev->function, offset & ~3u, cur);
}

void pci_write_config8(pci_device_t *dev, uint16_t offset, uint8_t value)
{
    uint32_t cur = config_read32_raw(dev->segment, dev->bus, dev->slot,
                                     dev->function, offset & ~3u);
    int shift = (offset & 3) * 8;
    cur &= ~(0xFFu << shift);
    cur |=  ((uint32_t)value << shift);
    config_write32_raw(dev->segment, dev->bus, dev->slot,
                       dev->function, offset & ~3u, cur);
}

uint16_t pci_get_command(pci_device_t *dev)
{
    return pci_read_config16(dev, PCI_REG_COMMAND);
}

void pci_set_command(pci_device_t *dev, uint16_t cmd)
{
    pci_write_config16(dev, PCI_REG_COMMAND, cmd);
}

void pci_enable_bus_mastering(pci_device_t *dev)
{
    uint16_t cmd = pci_get_command(dev);
    cmd |= PCI_CMD_BUS_MASTER | PCI_CMD_MEM_SPACE;
    pci_set_command(dev, cmd);
}

// Returns the config-space offset of the capability with the given ID, or 0.
static uint8_t pci_find_cap(pci_device_t *dev, uint8_t cap_id)
{
    // Check capabilities pointer is valid (status bit 4)
    uint16_t status = pci_read_config16(dev, PCI_REG_STATUS);
    if (!(status & (1 << 4))) return 0;

    uint8_t ptr = pci_read_config8(dev, PCI_REG_CAPABILITIES) & 0xFC;
    int limit = 48; // guard against malformed firmware

    while (ptr && limit--) {
        uint8_t id   = pci_read_config8(dev, ptr);
        uint8_t next = pci_read_config8(dev, ptr + 1) & 0xFC;
        if (id == cap_id) return ptr;
        ptr = next;
    }
    return 0;
}

void pci_probe_bars(pci_device_t *dev)
{
    int bar_count = (dev->header_type == 0) ? 6 : 2;

    for (int i = 0; i < bar_count; ) {
        uint16_t reg = PCI_REG_BAR0 + i * 4;
        uint32_t orig = pci_read_config32(dev, reg);

        dev->bars[i].is_io      = orig & PCI_BAR_TYPE_IO;
        dev->bars[i].is_64bit   = false;
        dev->bars[i].prefetchable = false;

        // BAR is unimplemented / empty
        if (orig == 0 || orig == 0xFFFFFFFF) {
            log_debug(PCI_MODULE, "    BAR%d: empty (raw=0x%lx)", i, (unsigned long)orig);
            i++;
            continue;
        }

        if (dev->bars[i].is_io) {
            // I/O space BAR
            pci_write_config32(dev, reg, 0xFFFFFFFF);
            uint32_t sz = pci_read_config32(dev, reg);
            pci_write_config32(dev, reg, orig);

            sz &= ~0x3u;
            dev->bars[i].phys_base = orig & ~0x3u;
            dev->bars[i].size      = (~sz + 1) & 0xFFFF;

            log_debug(PCI_MODULE, "    BAR%d: I/O  base=0x%lx  size=0x%lx",
                      i,
                      (unsigned long)dev->bars[i].phys_base,
                      (unsigned long)dev->bars[i].size);
            i++;
        } else {
            // Memory space BAR
            uint8_t type = (orig & PCI_BAR_TYPE_MEM_MASK);
            dev->bars[i].prefetchable = orig & PCI_BAR_PREFETCHABLE;

            if (type == PCI_BAR_TYPE_MEM64) {
                dev->bars[i].is_64bit = true;

                uint32_t orig_hi = pci_read_config32(dev, reg + 4);

                pci_write_config32(dev, reg,     0xFFFFFFFF);
                pci_write_config32(dev, reg + 4, 0xFFFFFFFF);
                uint32_t sz_lo = pci_read_config32(dev, reg);
                uint32_t sz_hi = pci_read_config32(dev, reg + 4);
                pci_write_config32(dev, reg,     orig);
                pci_write_config32(dev, reg + 4, orig_hi);

                dev->bars[i].phys_base = ((uint64_t)(orig & ~0xFu))
                                       | ((uint64_t)orig_hi << 32);
                uint64_t sz64 = ((uint64_t)(sz_lo & ~0xFu))
                              | ((uint64_t)sz_hi << 32);
                dev->bars[i].size = (~sz64 + 1);

                uint32_t pb_lo = (uint32_t)(dev->bars[i].phys_base & 0xFFFFFFFF);
                uint32_t pb_hi = (uint32_t)(dev->bars[i].phys_base >> 32);
                uint32_t sz_p  = (uint32_t)(dev->bars[i].size & 0xFFFFFFFF);

                log_debug(PCI_MODULE,
                          "    BAR%d: MEM64 base=0x%lx_%lx  size=0x%lx%s",
                          i,
                          (unsigned long)pb_hi, (unsigned long)pb_lo,
                          (unsigned long)sz_p,
                          dev->bars[i].prefetchable ? "  (prefetchable)" : "");

                // Skip next slot (consumed by upper 32 bits)
                dev->bars[i + 1].phys_base = 0;
                dev->bars[i + 1].size      = 0;
                i += 2;
            } else {
                // 32-bit memory BAR
                pci_write_config32(dev, reg, 0xFFFFFFFF);
                uint32_t sz = pci_read_config32(dev, reg);
                pci_write_config32(dev, reg, orig);

                sz &= ~0xFu;
                dev->bars[i].phys_base = orig & ~0xFu;
                dev->bars[i].size      = (~sz + 1);

                log_debug(PCI_MODULE,
                          "    BAR%d: MEM32 base=0x%lx  size=0x%lx%s",
                          i,
                          (unsigned long)dev->bars[i].phys_base,
                          (unsigned long)dev->bars[i].size,
                          dev->bars[i].prefetchable ? "  (prefetchable)" : "");
                i++;
            }
        }
    }
}

uintptr_t pci_map_bar(pci_device_t *dev, int index)
{
    pci_bar_t *bar = &dev->bars[index];
    if (bar->is_io || bar->size == 0) return 0;
    if (bar->virt_base) return (uintptr_t)bar->virt_base;

    void *virt = pci_mmio_map_impl(bar->phys_base, bar->size);
    bar->virt_base = (uint64_t)(uintptr_t)virt;
    return (uintptr_t)virt;
}

int pci_enable_intx(pci_device_t *dev, IRQHandler handler)
{
    uint8_t irq_line = pci_read_config8(dev, PCI_REG_INTERRUPT_LINE);
    uint8_t irq_pin  = pci_read_config8(dev, PCI_REG_INTERRUPT_PIN);

    if (irq_pin == 0 || irq_line == 0xFF) return -1;

    // Clear INT_DISABLE bit so the device can assert INTx
    uint16_t cmd = pci_get_command(dev);
    cmd &= ~PCI_CMD_INT_DISABLE;
    pci_set_command(dev, cmd);

    // IRQ lines 0–15 are legacy ISA/PIC lines; map to IDT vectors 32–47
    int vector = irq_line + 32;

    x86_64_IRQ_RegisterHandler(vector, handler);
    x86_64_IRQ_Unmask(vector);

    dev->irq_mode   = PCI_IRQ_INTX;
    dev->irq_vector = vector;

    return vector;
}

int pci_enable_msi(pci_device_t *dev, int vector, IRQHandler handler)
{
    uint8_t cap = pci_find_cap(dev, PCI_CAP_MSI);
    if (!cap) return -1;

    dev->msi_cap_off = cap;

    uint16_t ctrl = pci_read_config16(dev, cap + 2);
    bool is_64bit = ctrl & PCI_MSI_CTRL_64BIT;

    // Disable legacy INTx
    uint16_t cmd = pci_get_command(dev);
    cmd |= PCI_CMD_INT_DISABLE;
    pci_set_command(dev, cmd);

    // MSI message address: 0xFEE[dest:19:12] always targets BSP (APIC id 0)
    // Message data encodes the vector and edge/assert trigger.
    uint32_t msg_addr = 0xFEE00000u;   // IA-32 MSI address, APIC ID 0, no re-direction
    uint16_t msg_data = (uint16_t)(vector & 0xFF);  // edge-triggered, assert

    pci_write_config32(dev, cap + 4, msg_addr);
    if (is_64bit) {
        pci_write_config32(dev, cap + 8, 0);          // upper address
        pci_write_config16(dev, cap + 12, msg_data);
    } else {
        pci_write_config16(dev, cap + 8, msg_data);
    }

    // Force single-vector, enable MSI
    ctrl &= ~(0x7u << 4);   // clear MME (multiple message enable)
    ctrl |= PCI_MSI_CTRL_ENABLE;
    pci_write_config16(dev, cap + 2, ctrl);

    x86_64_IRQ_RegisterHandler(vector, handler);
    x86_64_IRQ_Unmask(vector);

    dev->irq_mode   = PCI_IRQ_MSI;
    dev->irq_vector = vector;

    return 0;
}

int pci_enable_msix(pci_device_t *dev, int vector_base,
                    IRQHandler *handlers, uint16_t count)
{
    uint8_t cap = pci_find_cap(dev, PCI_CAP_MSIX);
    if (!cap) return -1;

    dev->msix_cap_off = cap;

    uint16_t ctrl  = pci_read_config16(dev, cap + 2);
    uint16_t table_sz = (ctrl & PCI_MSIX_CTRL_TABLE_SZ) + 1;

    if (count > table_sz) count = table_sz;
    if (count > PCI_MSIX_MAX_VECTORS) return -1;

    // Locate the MSI-X table: BIR (BAR index) and offset
    uint32_t table_info = pci_read_config32(dev, cap + 4);
    uint8_t  bir        = table_info & 0x7;
    uint32_t table_off  = table_info & ~0x7u;

    // Map the BAR that holds the MSI-X table if not already mapped
    if (!dev->bars[bir].virt_base) {
        if (!pci_map_bar(dev, bir)) return -1;
    }

    volatile uint32_t *msix_table =
        (volatile uint32_t *)(uintptr_t)(dev->bars[bir].virt_base + table_off);
    dev->msix_table      = msix_table;
    dev->msix_table_size = table_sz;

    // Disable all interrupts first, then configure each requested entry
    ctrl |= PCI_MSIX_CTRL_MASK_ALL | PCI_MSIX_CTRL_ENABLE;
    pci_write_config16(dev, cap + 2, ctrl);

    uint16_t cmd = pci_get_command(dev);
    cmd |= PCI_CMD_INT_DISABLE;   // mask legacy INTx
    pci_set_command(dev, cmd);

    for (uint16_t i = 0; i < count; i++) {
        int vector = vector_base + i;
        uint32_t msg_addr = 0xFEE00000u;
        uint32_t msg_data = (uint32_t)(vector & 0xFF);

        // Each MSI-X entry is 16 bytes: addr_lo, addr_hi, data, ctrl
        uint32_t entry_base = i * 4;    // 4 dwords per entry
        msix_table[entry_base + 0] = msg_addr;
        msix_table[entry_base + 1] = 0;            // upper address
        msix_table[entry_base + 2] = msg_data;
        msix_table[entry_base + 3] &= ~(1u << 0);  // clear per-entry mask bit

        x86_64_IRQ_RegisterHandler(vector, handlers[i]);
        x86_64_IRQ_Unmask(vector);
    }

    // Unmask globally
    ctrl &= ~PCI_MSIX_CTRL_MASK_ALL;
    pci_write_config16(dev, cap + 2, ctrl);

    dev->irq_mode   = PCI_IRQ_MSIX;
    dev->irq_vector = vector_base;

    return 0;
}

void pci_disable_irq(pci_device_t *dev)
{
    // Set INT_DISABLE bit
    uint16_t cmd = pci_get_command(dev);
    cmd |= PCI_CMD_INT_DISABLE;
    pci_set_command(dev, cmd);

    // Disable MSI
    if (dev->msi_cap_off) {
        uint16_t ctrl = pci_read_config16(dev, dev->msi_cap_off + 2);
        ctrl &= ~PCI_MSI_CTRL_ENABLE;
        pci_write_config16(dev, dev->msi_cap_off + 2, ctrl);
    }

    // Disable MSI-X
    if (dev->msix_cap_off) {
        uint16_t ctrl = pci_read_config16(dev, dev->msix_cap_off + 2);
        ctrl &= ~PCI_MSIX_CTRL_ENABLE;
        ctrl |=  PCI_MSIX_CTRL_MASK_ALL;
        pci_write_config16(dev, dev->msix_cap_off + 2, ctrl);
    }

    dev->irq_mode   = PCI_IRQ_NONE;
    dev->irq_vector = -1;
}

static pci_device_t *probe_function(uint16_t seg, uint8_t bus,
                                    uint8_t slot, uint8_t func)
{
    // Vendor ID of 0xFFFF means no device
    uint32_t id_reg = config_read32_raw(seg, bus, slot, func, PCI_REG_VENDOR_ID);
    if ((id_reg & 0xFFFF) == 0xFFFF) return NULL;

    log_debug(PCI_MODULE, "  Probing %lu:%lu:%lu.%lu  vendor=0x%lx device=0x%lx",
              (unsigned long)seg, (unsigned long)bus,
              (unsigned long)slot, (unsigned long)func,
              (unsigned long)(id_reg & 0xFFFF),
              (unsigned long)(id_reg >> 16));

    pci_device_t *dev = (pci_device_t *)kmalloc(sizeof(pci_device_t));
    if (!dev) {
        log_err(PCI_MODULE, "  kmalloc failed for device %lu:%lu:%lu.%lu",
                (unsigned long)seg, (unsigned long)bus,
                (unsigned long)slot, (unsigned long)func);
        return NULL;
    }

    // Zero-initialise
    for (int i = 0; i < (int)sizeof(pci_device_t); i++)
        ((uint8_t *)dev)[i] = 0;

    dev->segment  = seg;
    dev->bus      = bus;
    dev->slot     = slot;
    dev->function = func;

    dev->vendor_id   = (uint16_t)(id_reg & 0xFFFF);
    dev->device_id   = (uint16_t)(id_reg >> 16);

    uint32_t class_reg = config_read32_raw(seg, bus, slot, func, PCI_REG_REVISION_ID);
    dev->revision  = (uint8_t) class_reg;
    dev->prog_if   = (uint8_t)(class_reg >>  8);
    dev->subclass  = (uint8_t)(class_reg >> 16);
    dev->class_code= (uint8_t)(class_reg >> 24);

    dev->header_type = config_read32_raw(seg, bus, slot, func, PCI_REG_CACHE_LINE) >> 16 & 0x7F;

    uint32_t sub_reg = config_read32_raw(seg, bus, slot, func, PCI_REG_SUBSYS_VENDOR);
    dev->subsys_vendor_id = (uint16_t)(sub_reg & 0xFFFF);
    dev->subsys_id        = (uint16_t)(sub_reg >> 16);

    dev->irq_mode   = PCI_IRQ_NONE;
    dev->irq_vector = -1;

    log_debug(PCI_MODULE,
              "    class=0x%lx sub=0x%lx prog_if=0x%lx hdr=0x%lx",
              (unsigned long)dev->class_code, (unsigned long)dev->subclass,
              (unsigned long)dev->prog_if,    (unsigned long)dev->header_type);

    // Cache capability offsets
    dev->msi_cap_off  = pci_find_cap(dev, PCI_CAP_MSI);
    dev->msix_cap_off = pci_find_cap(dev, PCI_CAP_MSIX);

    if (dev->msi_cap_off)
        log_debug(PCI_MODULE, "    MSI cap at 0x%lx", (unsigned long)dev->msi_cap_off);
    if (dev->msix_cap_off)
        log_debug(PCI_MODULE, "    MSI-X cap at 0x%lx", (unsigned long)dev->msix_cap_off);

    log_debug(PCI_MODULE, "    Probing BARs ...");
    pci_probe_bars(dev);
    log_debug(PCI_MODULE, "    BARs done");

    return dev;
}

static void scan_bus(uint16_t seg, uint8_t bus);

static void scan_slot(uint16_t seg, uint8_t bus, uint8_t slot)
{
    uint32_t id = config_read32_raw(seg, bus, slot, 0, PCI_REG_VENDOR_ID);
    if ((id & 0xFFFF) == 0xFFFF) return;

    log_debug(PCI_MODULE, "Found device at %lu:%lu:%lu.0, probing ...",
              (unsigned long)seg, (unsigned long)bus, (unsigned long)slot);

    pci_device_t *dev = probe_function(seg, bus, slot, 0);
    if (!dev) return;

    dev->next      = g_device_list;
    g_device_list  = dev;

    uint8_t hdr = dev->header_type;

    // If PCI-to-PCI bridge, recurse into the secondary bus
    if ((hdr & 0x7F) == 0x01) {
        uint8_t secondary_bus = pci_read_config8(dev, 0x19);
        log_info(PCI_MODULE, "  PCI-to-PCI bridge at %lu:%lu:%lu.0 -> secondary bus %lu",
                 (unsigned long)seg, (unsigned long)bus,
                 (unsigned long)slot, (unsigned long)secondary_bus);
        scan_bus(seg, secondary_bus);
    }

    // Probe additional functions if multi-function device
    if (hdr & 0x80) {
        log_debug(PCI_MODULE, "  Multi-function device at %lu:%lu:%lu, scanning functions 1-7",
                  (unsigned long)seg, (unsigned long)bus, (unsigned long)slot);
        for (uint8_t func = 1; func < 8; func++) {
            pci_device_t *fn = probe_function(seg, bus, slot, func);
            if (fn) {
                log_debug(PCI_MODULE, "    Found function %lu", (unsigned long)func);
                fn->next      = g_device_list;
                g_device_list = fn;
            }
        }
    }
}

static void scan_bus(uint16_t seg, uint8_t bus)
{
    for (uint8_t slot = 0; slot < 32; slot++)
        scan_slot(seg, bus, slot);
}

void pci_init(void)
{
    if (g_pci_initialised) {
        log_warn(PCI_MODULE, "pci_init() called more than once, ignoring");
        return;
    }
    g_pci_initialised = true;

    log_info(PCI_MODULE, "Initialising PCI/PCIe subsystem");

    mcfg_table_t *mcfg = (mcfg_table_t *)acpi_find_table("MCFG");
    if (mcfg) {
        log_debug(PCI_MODULE, "Found ACPI MCFG table at %p (length=%lu)",
                  (void *)mcfg, (unsigned long)mcfg->length);

        if (mcfg->length < sizeof(mcfg_table_t)) {
            log_err(PCI_MODULE, "MCFG table length %lu is too small, ignoring",
                    (unsigned long)mcfg->length);
        } else {
            uint32_t entry_count = (mcfg->length - sizeof(mcfg_table_t))
                                 / sizeof(mcfg_entry_t);
            log_debug(PCI_MODULE, "MCFG entry count: %lu", (unsigned long)entry_count);

            mcfg_entry_t *entries = (mcfg_entry_t *)(mcfg + 1);

            for (uint32_t i = 0; i < entry_count && i < MAX_MCFG_ENTRIES; i++) {
                uint32_t base_lo = (uint32_t)(entries[i].base_address & 0xFFFFFFFF);
                uint32_t base_hi = (uint32_t)(entries[i].base_address >> 32);

                log_debug(PCI_MODULE,
                          "  ECAM[%lu]: base=0x%lx_%lx  seg=%lu  bus %lu-%lu",
                          (unsigned long)i,
                          (unsigned long)base_hi,
                          (unsigned long)base_lo,
                          (unsigned long)entries[i].segment_group,
                          (unsigned long)entries[i].start_bus,
                          (unsigned long)entries[i].end_bus);

                g_ecam_entries[g_ecam_count].base      = entries[i].base_address;
                g_ecam_entries[g_ecam_count].segment   = entries[i].segment_group;
                g_ecam_entries[g_ecam_count].start_bus = entries[i].start_bus;
                g_ecam_entries[g_ecam_count].end_bus   = entries[i].end_bus;
                g_ecam_count++;
            }
        }
    } else {
        log_warn(PCI_MODULE, "No ACPI MCFG table found, falling back to legacy port I/O");
    }

    if (g_ecam_count > 0) {
        log_info(PCI_MODULE, "Using PCIe ECAM for config space access (%d region(s))",
                 g_ecam_count);

        for (int i = 0; i < g_ecam_count; i++) {
            // Map the entire ECAM region into virtual address space before
            // touching any config registers - unmapped ECAM = instant page fault.
            uint64_t ecam_size = (uint64_t)(g_ecam_entries[i].end_bus
                                          - g_ecam_entries[i].start_bus + 1) << 20;
            void *ecam_virt = pci_mmio_map_impl(g_ecam_entries[i].base, ecam_size);

            if (!ecam_virt) {
                log_warn(PCI_MODULE,
                         "  Failed to map ECAM region %d (phys=0x%lx size=0x%lx), "
                         "will use port I/O fallback",
                         i,
                         (unsigned long)g_ecam_entries[i].base,
                         (unsigned long)ecam_size);
                // Do NOT overwrite base - leave it as the physical address.
                // ecam_address() will still find the entry; on bare-metal with
                // identity mapping this works directly; otherwise port I/O
                // fallback in config_read32_raw handles it.
            } else {
                // Redirect the base to the mapped virtual address
                g_ecam_entries[i].base = (uint64_t)(uintptr_t)ecam_virt;
                log_debug(PCI_MODULE,
                          "  ECAM region %d mapped: phys size=0x%lx  virt=%p",
                          i, (unsigned long)ecam_size, ecam_virt);
            }

            log_debug(PCI_MODULE, "Scanning ECAM region %d: seg=%lu bus %lu-%lu",
                      i,
                      (unsigned long)g_ecam_entries[i].segment,
                      (unsigned long)g_ecam_entries[i].start_bus,
                      (unsigned long)g_ecam_entries[i].end_bus);

            for (int bus = (int)g_ecam_entries[i].start_bus;
                 bus <= (int)g_ecam_entries[i].end_bus; bus++)
            {
                log_debug(PCI_MODULE, "  Scanning bus %d ...", bus);
                scan_bus(g_ecam_entries[i].segment, (uint8_t)bus);
            }
        }
    } else {
        log_info(PCI_MODULE, "Using legacy port I/O for config space access");
        for (int bus = 0; bus < 256; bus++) {
            log_debug(PCI_MODULE, "  Scanning bus %d ...", bus);
            scan_bus(0, (uint8_t)bus);
        }
    }

    int dev_count = 0;
    for (pci_device_t *d = g_device_list; d; d = d->next) dev_count++;
    log_ok(PCI_MODULE, "Enumeration complete: %d device(s) found", dev_count);
}

pci_device_t *pci_get_devices(void) { return g_device_list; }

pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id)
{
    for (pci_device_t *d = g_device_list; d; d = d->next) {
        if ((vendor_id == 0xFFFF || d->vendor_id == vendor_id) &&
            (device_id == 0xFFFF || d->device_id == device_id))
            return d;
    }
    return NULL;
}

pci_device_t *pci_find_device_by_class(uint8_t class_code, uint8_t subclass)
{
    for (pci_device_t *d = g_device_list; d; d = d->next) {
        if ((class_code == 0xFF || d->class_code == class_code) &&
            (subclass   == 0xFF || d->subclass   == subclass))
            return d;
    }
    return NULL;
}

uint8_t pci_io_read8(pci_device_t *dev, int bar_index, uint32_t offset)
{
    return in8((uint16_t)(dev->bars[bar_index].phys_base + offset));
}
uint16_t pci_io_read16(pci_device_t *dev, int bar_index, uint32_t offset)
{
    return in16((uint16_t)(dev->bars[bar_index].phys_base + offset));
}
uint32_t pci_io_read32(pci_device_t *dev, int bar_index, uint32_t offset)
{
    return in32((uint16_t)(dev->bars[bar_index].phys_base + offset));
}
void pci_io_write8(pci_device_t *dev, int bar_index, uint32_t offset, uint8_t val)
{
    out8((uint16_t)(dev->bars[bar_index].phys_base + offset), val);
}
void pci_io_write16(pci_device_t *dev, int bar_index, uint32_t offset, uint16_t val)
{
    out16((uint16_t)(dev->bars[bar_index].phys_base + offset), val);
}
void pci_io_write32(pci_device_t *dev, int bar_index, uint32_t offset, uint32_t val)
{
    out32((uint16_t)(dev->bars[bar_index].phys_base + offset), val);
}

uint8_t pci_mmio_read8(pci_device_t *dev, int bar_index, uint64_t offset)
{
    return *(volatile uint8_t *)(uintptr_t)(dev->bars[bar_index].virt_base + offset);
}
uint16_t pci_mmio_read16(pci_device_t *dev, int bar_index, uint64_t offset)
{
    return *(volatile uint16_t *)(uintptr_t)(dev->bars[bar_index].virt_base + offset);
}
uint32_t pci_mmio_read32(pci_device_t *dev, int bar_index, uint64_t offset)
{
    return *(volatile uint32_t *)(uintptr_t)(dev->bars[bar_index].virt_base + offset);
}
uint64_t pci_mmio_read64(pci_device_t *dev, int bar_index, uint64_t offset)
{
    return *(volatile uint64_t *)(uintptr_t)(dev->bars[bar_index].virt_base + offset);
}
void pci_mmio_write8(pci_device_t *dev, int bar_index, uint64_t offset, uint8_t val)
{
    *(volatile uint8_t *)(uintptr_t)(dev->bars[bar_index].virt_base + offset) = val;
}
void pci_mmio_write16(pci_device_t *dev, int bar_index, uint64_t offset, uint16_t val)
{
    *(volatile uint16_t *)(uintptr_t)(dev->bars[bar_index].virt_base + offset) = val;
}
void pci_mmio_write32(pci_device_t *dev, int bar_index, uint64_t offset, uint32_t val)
{
    *(volatile uint32_t *)(uintptr_t)(dev->bars[bar_index].virt_base + offset) = val;
}
void pci_mmio_write64(pci_device_t *dev, int bar_index, uint64_t offset, uint64_t val)
{
    *(volatile uint64_t *)(uintptr_t)(dev->bars[bar_index].virt_base + offset) = val;
}

#define PCI_MMIO_VIRT_BASE  0xFFFFFF7F00000000ULL
#define PCI_MMIO_VIRT_SIZE  0x0000000100000000ULL   // 4 GiB window

static uint64_t g_mmio_virt_bump = PCI_MMIO_VIRT_BASE;

void *pci_mmio_map_impl(uint64_t phys, uint64_t size)
{
    if (size == 0) return NULL;

    // Round base down and size up to page boundaries
    uint64_t phys_aligned = phys & PAGE_MASK;
    uint64_t page_offset  = phys - phys_aligned;
    uint64_t size_aligned = (size + page_offset + PAGE_SIZE - 1) & PAGE_MASK;
    size_t   pages        = size_aligned / PAGE_SIZE;

    // Bump-allocate virtual address space (no need to ever free MMIO mappings)
    uint64_t virt = g_mmio_virt_bump;
    if (virt + size_aligned > PCI_MMIO_VIRT_BASE + PCI_MMIO_VIRT_SIZE) {
        // Out of MMIO virtual address space - should not happen in practice
        return NULL;
    }
    g_mmio_virt_bump += size_aligned;

    // MMIO flags: present, writable, no caching, write-through, no execute
    uint64_t flags = PAGE_PRESENT | PAGE_WRITE | PAGE_NOCACHE
                   | PAGE_WRITETHROUGH | PAGE_NX;

    address_space_t *kspace = vmm_get_kernel_space();
    if (!vmm_map_range(kspace, (void *)virt, (void *)phys_aligned, pages, flags)) {
        g_mmio_virt_bump -= size_aligned;   // roll back on failure
        return NULL;
    }

    // Return the virtual address that corresponds exactly to 'phys'
    return (void *)(uintptr_t)(virt + page_offset);
}