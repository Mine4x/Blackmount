#pragma once

#include <block/block.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_start;
    uint32_t sector_count;
} mbr_partition_entry_t;

typedef struct __attribute__((packed)) {
    uint8_t              bootstrap[446];
    mbr_partition_entry_t partitions[4];
    uint16_t             signature;
} mbr_t;

bool mbr_register_partitions(block_device_t* dev);