#include <block/block.h>
#include <heap.h>
#include <stddef.h>
#include <string.h>
#include <drivers/disk/floppy.h>

static bool floppy_block_read(block_device_t* dev, uint64_t lba, uint32_t count, void* buf);

static bool floppy_block_write(block_device_t* dev, uint64_t lba, uint32_t count, const void* buf);

block_device_t* floppy_create_blockdev(const char* name, uint8_t drive);