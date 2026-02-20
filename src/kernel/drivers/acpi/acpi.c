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
