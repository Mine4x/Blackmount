#pragma once

#include <block/block.h>
#include <stdint.h>

block_device_t* ramdisk_create_blockdev(const char* name, uint64_t size_bytes);
void            ramdisk_destroy_blockdev(block_device_t* dev);