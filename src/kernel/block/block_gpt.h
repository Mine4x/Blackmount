#pragma once

#include <block/block.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct __attribute__((packed)) {
    uint8_t  data[16];
} gpt_guid_t;

typedef struct __attribute__((packed)) {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t my_lba;
    uint64_t alternate_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    gpt_guid_t disk_guid;
    uint64_t partition_entry_lba;
    uint32_t partition_entry_count;
    uint32_t partition_entry_size;
    uint32_t partition_array_crc32;
} gpt_header_t;

typedef struct __attribute__((packed)) {
    gpt_guid_t type_guid;
    gpt_guid_t unique_guid;
    uint64_t   start_lba;
    uint64_t   end_lba;
    uint64_t   attributes;
    uint16_t   name[36];
} gpt_partition_entry_t;

#define GPT_SIGNATURE 0x5452415020494645ULL

bool gpt_register_partitions(block_device_t* dev);