#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct block_device block_device_t;

typedef bool (*block_read_fn)(
    block_device_t* dev,
    uint64_t lba,
    uint32_t count,
    void* buffer
);

typedef bool (*block_write_fn)(
    block_device_t* dev,
    uint64_t lba,
    uint32_t count,
    const void* buffer
);

struct block_device {
    const char* name;

    uint64_t sector_count;
    uint32_t sector_size;

    uint64_t lba_offset;     // for partitions

    void* driver_data;
    block_read_fn read;
    block_write_fn write;

    void* lock;              // future mutex
};

bool block_register(block_device_t* dev);
block_device_t* block_get(const char* name);
