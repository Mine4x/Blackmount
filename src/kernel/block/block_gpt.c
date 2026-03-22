#include "block_gpt.h"
#include <block/block.h>
#include <heap.h>
#include <string.h>
#include <debug.h>
#include <stddef.h>

static bool guid_is_zero(const gpt_guid_t* g) {
    for (int i = 0; i < 16; i++) {
        if (g->data[i] != 0)
            return false;
    }
    return true;
}

static char* make_partition_name(const char* base, int index) {
    int base_len = strlen(base);
    char* name = kmalloc(base_len + 32);
    int i = 0;
    while (base[i]) {
        name[i] = base[i];
        i++;
    }
    name[i++] = 'p';

    int n = index + 1;
    int digits = 0;
    int tmp = n;
    do { digits++; tmp /= 10; } while (tmp > 0);

    for (int d = digits - 1; d >= 0; d--) {
        name[i + d] = '0' + (n % 10);
        n /= 10;
    }
    name[i + digits] = '\0';
    return name;
}

bool gpt_register_partitions(block_device_t* dev) {
    uint8_t* sector = kmalloc(dev->sector_size);

    if (!dev->read(dev, 1, 1, sector)) {
        log_err("GPT", "Failed to read LBA 1 from %s", dev->name);
        kfree(sector);
        return false;
    }

    gpt_header_t header;
    memcpy(&header, sector, sizeof(gpt_header_t));
    kfree(sector);

    if (header.signature != GPT_SIGNATURE) {
        log_err("GPT", "Invalid GPT signature on %s", dev->name);
        return false;
    }

    if (header.partition_entry_size < sizeof(gpt_partition_entry_t)) {
        log_err("GPT", "Unexpected partition entry size %u on %s",
            header.partition_entry_size, dev->name);
        return false;
    }

    uint32_t entries_per_sector = dev->sector_size / header.partition_entry_size;
    uint32_t total_sectors =
        (header.partition_entry_count + entries_per_sector - 1) / entries_per_sector;

    uint8_t* table = kmalloc(total_sectors * dev->sector_size);

    if (!dev->read(dev, header.partition_entry_lba, total_sectors, table)) {
        log_err("GPT", "Failed to read partition table from %s", dev->name);
        kfree(table);
        return false;
    }

    int registered = 0;

    for (uint32_t i = 0; i < header.partition_entry_count; i++) {
        gpt_partition_entry_t entry;
        memcpy(&entry,
            table + i * header.partition_entry_size,
            sizeof(gpt_partition_entry_t));

        if (guid_is_zero(&entry.type_guid))
            continue;

        if (entry.start_lba == 0 || entry.end_lba < entry.start_lba)
            continue;

        block_device_t* part = kmalloc(sizeof(block_device_t));

        part->name         = make_partition_name(dev->name, i);
        part->sector_size  = dev->sector_size;
        part->sector_count = entry.end_lba - entry.start_lba + 1;
        part->lba_offset   = dev->lba_offset + entry.start_lba;
        part->driver_data  = dev->driver_data;
        part->read         = dev->read;
        part->write        = dev->write;
        part->lock         = NULL;

        if (block_register(part)) {
            log_ok("GPT", "Partition %s: start=%llu sectors=%llu",
                part->name,
                (unsigned long long)entry.start_lba,
                (unsigned long long)part->sector_count);
            registered++;
        } else {
            log_err("GPT", "Failed to register partition %s", part->name);
            kfree((void*)part->name);
            kfree(part);
        }
    }

    kfree(table);
    return registered > 0;
}