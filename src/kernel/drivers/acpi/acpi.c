#include "acpi.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limine/limine_req.h>
#include <debug.h>

static struct ACPISDTHeader* sdt_root = NULL;
static int using_xsdt = 0;
static uint64_t hhdm = 0;

static int acpi_checksum(void* table, size_t length) {
    uint8_t sum = 0;
    uint8_t* ptr = (uint8_t*)table;

    for (size_t i = 0; i < length; i++)
        sum += ptr[i];

    return sum == 0;
}

void acpi_init() {

    void* rsdp_ptr = limine_get_rsdp();

    if (!rsdp_ptr) {
        log_crit("ACPI", "No RSDP from Limine");
        return;
    }

    hhdm = limine_get_hddm();

    log_info("ACPI", "RSDP ptr (virt): %p", rsdp_ptr);
    log_info("ACPI", "HHDM base: %p", (void*)hhdm);

    struct XSDP_t* rsdp = (struct XSDP_t*)rsdp_ptr;

    if (memcmp(rsdp->Signature, "RSD PTR ", 8) != 0) {
        log_crit("ACPI", "Invalid RSDP signature");
        return;
    }

    if (!acpi_checksum(rsdp, sizeof(struct RSDP_t))) {
        log_crit("ACPI", "RSDP checksum failed");
        return;
    }

    if (rsdp->Revision >= 2) {

        if (!acpi_checksum(rsdp, rsdp->Length)) {
            log_crit("ACPI", "XSDP extended checksum failed");
            return;
        }

        uint64_t xsdt_phys = rsdp->XsdtAddress;

        log_info("ACPI", "XSDT phys: 0x%llx", xsdt_phys);

        sdt_root = (struct ACPISDTHeader*)(xsdt_phys + hhdm);
        using_xsdt = 1;

        log_ok("ACPI", "Using XSDT");

    } else {

        uint32_t rsdt_phys = rsdp->RsdtAddress;

        log_info("ACPI", "RSDT phys: 0x%x", rsdt_phys);

        sdt_root = (struct ACPISDTHeader*)((uint64_t)rsdt_phys + hhdm);
        using_xsdt = 0;

        log_ok("ACPI", "Using RSDT");
    }

    if (!sdt_root) {
        log_crit("ACPI", "SDT root is NULL");
        return;
    }

    log_info("ACPI", "Root table virt: %p", sdt_root);
    log_info("ACPI", "Root table sig: %.4s", sdt_root->Signature);
    log_info("ACPI", "Root table length: %u", sdt_root->Length);

    if (!acpi_checksum(sdt_root, sdt_root->Length)) {
        log_crit("ACPI", "SDT root checksum failed");
        sdt_root = NULL;
        return;
    }

    log_ok("ACPI", "Root SDT checksum valid");
}

void* acpi_find_table(const char* signature) {

    if (!sdt_root) {
        log_warn("ACPI", "acpi_find_table called before init");
        return NULL;
    }

    int entry_size = using_xsdt ? 8 : 4;

    int entry_count =
        (sdt_root->Length - sizeof(struct ACPISDTHeader)) / entry_size;

    log_info("ACPI", "Entry count: %d", entry_count);

    uint8_t* entries =
        (uint8_t*)sdt_root + sizeof(struct ACPISDTHeader);

    for (int i = 0; i < entry_count; i++) {

        uint64_t table_phys;

        if (using_xsdt)
            table_phys = ((uint64_t*)entries)[i];
        else
            table_phys = ((uint32_t*)entries)[i];

        struct ACPISDTHeader* table =
            (struct ACPISDTHeader*)(table_phys + hhdm);

        if (!table)
            continue;

        log_info("ACPI", "Checking table %.4s at %p",
                 table->Signature, table);

        if (memcmp(table->Signature, signature, 4) == 0) {

            if (!acpi_checksum(table, table->Length)) {
                log_warn("ACPI", "Table %.4s checksum invalid",
                         table->Signature);
                return NULL;
            }

            log_ok("ACPI", "Found table %.4s", table->Signature);
            return table;
        }
    }

    log_warn("ACPI", "Table %.4s not found", signature);
    return NULL;
}


static inline void io_wait() {
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}


void acpi_debug_shutdown_info() {
    struct FADT_t* fadt = (struct FADT_t*)acpi_find_table("FACP");
    if (!fadt) {
        log_debug("ACPI", "FADT table not found");
        return;
    }

    log_debug("ACPI", "PM1aCntBlk: 0x%X", (uint16_t)fadt->PM1aCntBlk);
    log_debug("ACPI", "PM1bCntBlk: 0x%X", (uint16_t)fadt->PM1bCntBlk);
    log_debug("ACPI", "PM1aEvtBlk: 0x%X", (uint16_t)fadt->PM1aEvtBlk);
    log_debug("ACPI", "PM1bEvtBlk: 0x%X", (uint16_t)fadt->PM1bEvtBlk);
    log_debug("ACPI", "SmiCmd port: 0x%X", fadt->SmiCmd);
    log_debug("ACPI", "AcpiEnable value: 0x%X", fadt->AcpiEnable);

    uint16_t s5_type = S5_SLEEP_TYPE;
    log_debug("ACPI", "S5 sleep type: 0x%X", s5_type);

    if (fadt->AcpiEnable)
        log_debug("ACPI", "ACPI enable required before shutdown");
    else
        log_debug("ACPI", "ACPI already enabled");
}

static uint8_t s5_slp_typa = 0, s5_slp_typb = 0;

static int acpi_parse_s5(void) {
    struct ACPISDTHeader* dsdt_hdr = NULL;
    struct FADT_t* fadt = (struct FADT_t*)acpi_find_table("FACP");
    if (!fadt) return 0;

    uint64_t dsdt_phys = fadt->Dsdt;
    dsdt_hdr = (struct ACPISDTHeader*)(dsdt_phys + hhdm);

    if (memcmp(dsdt_hdr->Signature, "DSDT", 4) != 0) return 0;

    uint8_t* aml   = (uint8_t*)dsdt_hdr + sizeof(struct ACPISDTHeader);
    uint32_t len   = dsdt_hdr->Length   - sizeof(struct ACPISDTHeader);

    for (uint32_t i = 0; i < len - 8; i++) {
        if (memcmp(&aml[i], "_S5_", 4) != 0) continue;

        uint8_t* p = &aml[i + 4];
        if (p[0] != 0x12) continue;
        p += 2;
        p++;
        if (p[0] == 0x0A) { s5_slp_typa = p[1]; p += 2; }
        if (p[0] == 0x0A) { s5_slp_typb = p[1]; }

        log_ok("ACPI", "_S5_: SLP_TYPa=0x%X SLP_TYPb=0x%X",
               s5_slp_typa, s5_slp_typb);
        return 1;
    }
    return 0;
}

void acpi_shutdown() {
    struct FADT_t* fadt = (struct FADT_t*)acpi_find_table("FACP");
    if (!fadt) return;

    if (fadt->SmiCmd && fadt->AcpiEnable) {
        __asm__ volatile ("outb %0, %1" :: "a"(fadt->AcpiEnable), "Nd"((uint16_t)fadt->SmiCmd));
        uint16_t pm1a_evt = (uint16_t)fadt->PM1aEvtBlk;
        for (int i = 0; i < 300; i++) {
            uint16_t val;
            __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(pm1a_evt));
            if (val & 1) break;
            io_wait();
        }
    }

    uint16_t pm1a = (uint16_t)fadt->PM1aCntBlk;
    uint16_t pm1b = (uint16_t)fadt->PM1bCntBlk;

    uint16_t val_a = ((uint16_t)s5_slp_typa << 10) | SLP_EN;
    uint16_t val_b = ((uint16_t)s5_slp_typb << 10) | SLP_EN;

    __asm__ volatile ("outw %0, %1" :: "a"(val_a), "Nd"(pm1a));
    if (pm1b)
        __asm__ volatile ("outw %0, %1" :: "a"(val_b), "Nd"(pm1b));

    while (1) __asm__ volatile ("hlt");
}