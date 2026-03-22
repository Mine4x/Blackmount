#include "block_mbr.h"
#include <block/block.h>
#include <heap.h>
#include <string.h>
#include <debug.h>
#include <stddef.h>

#define MBR_SIGNATURE    0xAA55
#define MAX_NAME_LEN     32

static char* make_partition_name(const char* base, int index) {
    int base_len = strlen(base);
    char* name = kmalloc(base_len + MAX_NAME_LEN);
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

bool mbr_register_partitions(block_device_t* dev) {
    mbr_t* mbr = kmalloc(sizeof(mbr_t));

    if (!dev->read(dev, 0, 1, mbr)) {
        log_err("MBR", "Failed to read sector 0 from %s", dev->name);
        kfree(mbr);
        return false;
    }

    if (mbr->signature != MBR_SIGNATURE) {
        log_err("MBR", "Invalid MBR signature on %s: 0x%04x", dev->name, mbr->signature);
        kfree(mbr);
        return false;
    }

    int registered = 0;

    for (int i = 0; i < 4; i++) {
        mbr_partition_entry_t* entry = &mbr->partitions[i];

        if (entry->type == 0x00 || entry->sector_count == 0) {
            continue;
        }

        block_device_t* part = kmalloc(sizeof(block_device_t));

        part->name         = make_partition_name(dev->name, i);
        part->sector_size  = dev->sector_size;
        part->sector_count = entry->sector_count;
        part->lba_offset   = dev->lba_offset + entry->lba_start;
        part->driver_data  = dev->driver_data;
        part->read         = dev->read;
        part->write        = dev->write;
        part->lock         = NULL;

        if (block_register(part)) {
            log_ok("MBR", "Partition %s: type=0x%02x lba=%u sectors=%u",
                part->name, entry->type, entry->lba_start, entry->sector_count);
            registered++;
        } else {
            log_err("MBR", "Failed to register partition %s", part->name);
            kfree((void*)part->name);
            kfree(part);
        }
    }

    kfree(mbr);
    return registered > 0;
}