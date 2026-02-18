#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <block/block.h>

/*
 * ATA public interface
 *
 * Supports:
 *  - Device detection (primary/secondary, master/slave)
 *  - LBA28 and LBA48 PIO read/write
 *  - Cache flush
 *  - Simple legacy compatibility helpers
 */

/* Initialize ATA subsystem and detect devices */
void ata_init(void);

/* Read sectors from an ATA device
 *  bus   : 0 = primary, 1 = secondary
 *  drive : 0 = master, 1 = slave
 *  lba   : starting LBA
 *  count : number of sectors
 *  buffer: must be at least count * 512 bytes
 */
bool ata_read_sectors(
    uint8_t bus,
    uint8_t drive,
    uint64_t lba,
    uint16_t count,
    uint8_t* buffer
);

/* Write sectors to an ATA device */
bool ata_write_sectors(
    uint8_t bus,
    uint8_t drive,
    uint64_t lba,
    uint16_t count,
    const uint8_t* buffer
);

/* Flush drive write cache */
bool ata_flush_cache(uint8_t bus, uint8_t drive);

/* Legacy compatibility helpers (primary master only) */
void ata_read_sector(uint32_t lba, uint8_t* buffer);
void ata_write_sector(uint32_t lba, uint8_t* buffer);

block_device_t* ata_create_primary_blockdev(const char* name);

/* Run basic ATA diagnostics (destructive: writes sector 0) */
void test_ata(void);
