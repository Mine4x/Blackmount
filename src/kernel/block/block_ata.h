#pragma once
#include <block/block.h>

static bool block_ata_read(block_device_t* dev, uint64_t lba, uint32_t count, void* buf);

static bool block_ata_write(block_device_t* dev, uint64_t lba, uint32_t count, const void* buf);

block_device_t* ata_create_blockdev(
    const char* name,
    uint8_t bus,
    uint8_t drive,
    uint64_t sectors
);