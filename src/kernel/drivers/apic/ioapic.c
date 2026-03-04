#include "ioapic.h"
#include <drivers/acpi/acpi.h>
#include <limine/limine_req.h>
#include <debug.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>


#define IOAPIC_REG_ID       0x00
#define IOAPIC_REG_VER      0x01
#define IOAPIC_REG_ARB      0x02
#define IOAPIC_REDTBL(n)    (0x10 + 2 * (n))  // low dword

// MMIO register selectors
#define IOREGSEL_OFFSET     0x00
#define IOWIN_OFFSET        0x10

typedef struct {
    uint8_t  id;
    uint64_t virt_base;   // HHDM-mapped address
    uint32_t gsi_base;    // first GSI this IOAPIC handles
    uint32_t gsi_count;   // how many GSIs it handles (from IOAPICVER)
} IOAPICInfo;

typedef struct {
    uint8_t  irq;         // ISA IRQ (0-15)
    uint32_t gsi;         // GSI it maps to
    uint16_t flags;       // polarity + trigger mode from MADT
} ISOEntry;

static IOAPICInfo ioapics[IOAPIC_MAX];
static int        ioapic_count = 0;

static ISOEntry   isos[24];   // at most 24 ISA IRQ overrides
static int        iso_count   = 0;

static uint64_t   hhdm = 0;

static void ioapic_write(uint64_t base, uint8_t reg, uint32_t val) {
    *((volatile uint32_t*)(base + IOREGSEL_OFFSET)) = reg;
    *((volatile uint32_t*)(base + IOWIN_OFFSET))    = val;
}

static uint32_t ioapic_read(uint64_t base, uint8_t reg) {
    *((volatile uint32_t*)(base + IOREGSEL_OFFSET)) = reg;
    return *((volatile uint32_t*)(base + IOWIN_OFFSET));
}


static IOAPICInfo* ioapic_for_gsi(uint32_t gsi) {
    for (int i = 0; i < ioapic_count; i++) {
        IOAPICInfo* a = &ioapics[i];
        if (gsi >= a->gsi_base && gsi < a->gsi_base + a->gsi_count)
            return a;
    }
    return NULL;
}


static void ioapic_write_redir(IOAPICInfo* apic, uint32_t gsi,
                                IOAPICRedirEntry entry) {
    uint32_t pin = gsi - apic->gsi_base;
    ioapic_write(apic->virt_base, IOAPIC_REDTBL(pin),     entry.low);
    ioapic_write(apic->virt_base, IOAPIC_REDTBL(pin) + 1, entry.high);
}

static IOAPICRedirEntry ioapic_read_redir(IOAPICInfo* apic, uint32_t gsi) {
    uint32_t pin = gsi - apic->gsi_base;
    IOAPICRedirEntry e;
    e.low  = ioapic_read(apic->virt_base, IOAPIC_REDTBL(pin));
    e.high = ioapic_read(apic->virt_base, IOAPIC_REDTBL(pin) + 1);
    return e;
}


static void parse_madt(struct MADT* madt) {
    uint8_t* ptr = (uint8_t*)madt + sizeof(struct MADT);
    uint8_t* end = (uint8_t*)madt + madt->Length;

    while (ptr < end) {
        struct MADTEntryHeader* entry = (struct MADTEntryHeader*)ptr;

        switch (entry->type) {

        case 0: { // Local APIC
            struct MADTLocalAPIC* lapic = (struct MADTLocalAPIC*)entry;
            if (lapic->flags & 1)
                log_info("IOAPIC", "CPU: ACPI ID %u, APIC ID %u",
                         lapic->acpi_processor_id, lapic->apic_id);
            break;
        }

        case 1: { // IOAPIC
            if (ioapic_count >= IOAPIC_MAX) {
                log_warn("IOAPIC", "Too many IOAPICs, ignoring");
                break;
            }
            struct MADTIOApic* m = (struct MADTIOApic*)entry;
            IOAPICInfo* a        = &ioapics[ioapic_count];

            a->id        = m->ioapic_id;
            a->virt_base = (uint64_t)m->ioapic_addr + hhdm;
            a->gsi_base  = m->gsi_base;

            // Read max redirection entries from IOAPICVER bits [23:16]
            uint32_t ver   = ioapic_read(a->virt_base, IOAPIC_REG_VER);
            a->gsi_count   = ((ver >> 16) & 0xFF) + 1;

            log_info("IOAPIC", "Found IOAPIC #%d: ID=%u phys=0x%x "
                     "virt=%p GSI base=%u count=%u",
                     ioapic_count, a->id, m->ioapic_addr,
                     (void*)a->virt_base, a->gsi_base, a->gsi_count);

            ioapic_count++;
            break;
        }

        case 2: { // Interrupt Source Override
            if (iso_count >= 24) break;
            struct MADTIntSourceOverride* iso =
                (struct MADTIntSourceOverride*)entry;
            isos[iso_count].irq   = iso->source;
            isos[iso_count].gsi   = iso->gsi;
            isos[iso_count].flags = iso->flags;
            log_info("IOAPIC", "ISO: ISA IRQ %u → GSI %u flags=0x%x",
                     iso->source, iso->gsi, iso->flags);
            iso_count++;
            break;
        }

        case 4: { // NMI Source
            struct MADTNMISource* nmi = (struct MADTNMISource*)entry;
            log_info("IOAPIC", "NMI source: GSI %u flags=0x%x",
                     nmi->gsi, nmi->flags);
            break;
        }

        default:
            break;
        }

        ptr += entry->length;
    }
}


static void ioapic_mask_all(void) {
    for (int i = 0; i < ioapic_count; i++) {
        IOAPICInfo* a = &ioapics[i];
        for (uint32_t pin = 0; pin < a->gsi_count; pin++) {
            IOAPICRedirEntry e = {0};
            e.mask  = 1;
            e.vector = 0xFF; // safe placeholder
            ioapic_write(a->virt_base, IOAPIC_REDTBL(pin),     e.low);
            ioapic_write(a->virt_base, IOAPIC_REDTBL(pin) + 1, e.high);
        }
    }
}


static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void pic_disable(void) {
    // Remap PIC to vectors 0x20/0x28 first to avoid spurious exceptions,
    // then mask all lines on both chips.
    outb(0x20, 0x11); outb(0xA0, 0x11);  // ICW1: init + ICW4
    outb(0x21, 0x20); outb(0xA1, 0x28);  // ICW2: vector offsets
    outb(0x21, 0x04); outb(0xA1, 0x02);  // ICW3: cascade
    outb(0x21, 0x01); outb(0xA1, 0x01);  // ICW4: 8086 mode
    outb(0x21, 0xFF); outb(0xA1, 0xFF);  // mask everything
    log_ok("IOAPIC", "Legacy 8259 PIC disabled");
}

void ioapic_init(void) {
    hhdm = limine_get_hddm();

    struct MADT* madt = (struct MADT*)acpi_find_table("APIC");
    if (!madt) {
        log_crit("IOAPIC", "MADT not found");
        return;
    }
    log_info("IOAPIC", "MADT found at %p, length %u", madt, madt->Length);

    parse_madt(madt);

    if (ioapic_count == 0) {
        log_crit("IOAPIC", "No IOAPICs found in MADT");
        return;
    }

    pic_disable();
    ioapic_mask_all();

    log_ok("IOAPIC", "Init complete — %d IOAPIC(s), %d ISO(s)",
           ioapic_count, iso_count);
}

void ioapic_route(uint32_t gsi, uint8_t vector, uint8_t delivery_mode,
                  int active_low, int level_triggered, uint8_t dest_apic_id) {
    IOAPICInfo* apic = ioapic_for_gsi(gsi);
    if (!apic) {
        log_err("IOAPIC", "No IOAPIC for GSI %u", gsi);
        return;
    }

    IOAPICRedirEntry e = {0};
    e.vector        = vector;
    e.delivery_mode = delivery_mode;
    e.dest_mode     = 0;              // physical
    e.polarity      = active_low ? 1 : 0;
    e.trigger_mode  = level_triggered ? 1 : 0;
    e.mask          = 0;              // unmasked
    e.destination   = dest_apic_id;

    ioapic_write_redir(apic, gsi, e);

    log_info("IOAPIC", "Routed GSI %u → vector 0x%x (APIC %u) "
             "polarity=%s trigger=%s",
             gsi, vector, dest_apic_id,
             active_low ? "low" : "high",
             level_triggered ? "level" : "edge");
}

void ioapic_route_isa_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id) {
    uint32_t gsi         = irq;   // default: GSI == IRQ
    int      active_low  = 0;     // ISA default: active high
    int      level_trig  = 0;     // ISA default: edge triggered

    // Check MADT Interrupt Source Overrides
    for (int i = 0; i < iso_count; i++) {
        if (isos[i].irq == irq) {
            gsi = isos[i].gsi;
            uint16_t f = isos[i].flags;

            // Polarity: bits [1:0]  — 00/01=active high, 11=active low
            uint8_t pol = f & 0x3;
            if (pol == 0x3) active_low = 1;

            // Trigger:  bits [3:2]  — 00/01=edge, 11=level
            uint8_t trig = (f >> 2) & 0x3;
            if (trig == 0x3) level_trig = 1;

            log_info("IOAPIC", "ISA IRQ %u overridden → GSI %u", irq, gsi);
            break;
        }
    }

    ioapic_route(gsi, vector, IOAPIC_DELIV_FIXED,
                 active_low, level_trig, dest_apic_id);
}

void ioapic_mask_gsi(uint32_t gsi) {
    IOAPICInfo* apic = ioapic_for_gsi(gsi);
    if (!apic) { log_err("IOAPIC", "mask: no IOAPIC for GSI %u", gsi); return; }
    IOAPICRedirEntry e = ioapic_read_redir(apic, gsi);
    e.mask = 1;
    ioapic_write_redir(apic, gsi, e);
}

void ioapic_unmask_gsi(uint32_t gsi) {
    IOAPICInfo* apic = ioapic_for_gsi(gsi);
    if (!apic) { log_err("IOAPIC", "unmask: no IOAPIC for GSI %u", gsi); return; }
    IOAPICRedirEntry e = ioapic_read_redir(apic, gsi);
    e.mask = 0;
    ioapic_write_redir(apic, gsi, e);
}