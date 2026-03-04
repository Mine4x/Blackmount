#ifndef IOAPIC_H
#define IOAPIC_H

#include <stdint.h>
#include <stddef.h>

struct MADT {
    char     Signature[4];
    uint32_t Length;
    uint8_t  Revision;
    uint8_t  Checksum;
    char     OEMID[6];
    char     OEMTableID[8];
    uint32_t OEMRevision;
    uint32_t CreatorID;
    uint32_t CreatorRevision;
    uint32_t local_apic_addr;
    uint32_t flags;
} __attribute__((packed));

struct MADTEntryHeader {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

// Type 1: Local APIC
struct MADTLocalAPIC {
    struct MADTEntryHeader header;
    uint8_t  acpi_processor_id;
    uint8_t  apic_id;
    uint32_t flags;
} __attribute__((packed));

// Type 2: IOAPIC
struct MADTIOApic {
    struct MADTEntryHeader header;
    uint8_t  ioapic_id;
    uint8_t  reserved;
    uint32_t ioapic_addr;
    uint32_t gsi_base;
} __attribute__((packed));

// Type 3: Interrupt Source Override (legacy ISA IRQ remapping)
struct MADTIntSourceOverride {
    struct MADTEntryHeader header;
    uint8_t  bus;           // always 0 (ISA)
    uint8_t  source;        // ISA IRQ
    uint32_t gsi;           // global system interrupt it maps to
    uint16_t flags;         // polarity + trigger mode
} __attribute__((packed));

// Type 4: NMI Source
struct MADTNMISource {
    struct MADTEntryHeader header;
    uint16_t flags;
    uint32_t gsi;
} __attribute__((packed));

typedef union {
    struct {
        uint8_t  vector;            // IDT vector [0xFF]
        uint8_t  delivery_mode : 3; // 000=fixed, 010=SMI, 100=NMI, 111=ExtINT
        uint8_t  dest_mode     : 1; // 0=physical, 1=logical
        uint8_t  delivery_stat : 1; // RO: 0=idle, 1=pending
        uint8_t  polarity      : 1; // 0=active high, 1=active low
        uint8_t  remote_irr    : 1; // RO (level-triggered only)
        uint8_t  trigger_mode  : 1; // 0=edge, 1=level
        uint8_t  mask          : 1; // 1=masked (disabled)
        uint64_t reserved      : 39;
        uint8_t  destination;       // APIC ID (physical mode)
    } __attribute__((packed));
    struct {
        uint32_t low;
        uint32_t high;
    };
} IOAPICRedirEntry;


#define IOAPIC_MAX          8     // max IOAPICs in system

#define IOAPIC_DELIV_FIXED  0x0
#define IOAPIC_DELIV_SMI    0x2
#define IOAPIC_DELIV_NMI    0x4
#define IOAPIC_DELIV_INIT   0x5
#define IOAPIC_DELIV_EXTINT 0x7

void ioapic_init(void);

// Route a GSI to an IDT vector on a given LAPIC destination
void ioapic_route(uint32_t gsi, uint8_t vector, uint8_t delivery_mode,
                  int active_low, int level_triggered, uint8_t dest_apic_id);

// Route a legacy ISA IRQ — automatically applies MADT overrides
void ioapic_route_isa_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id);

void ioapic_mask_gsi(uint32_t gsi);
void ioapic_unmask_gsi(uint32_t gsi);

#endif