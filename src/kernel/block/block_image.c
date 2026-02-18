#include <block/block.h>
#include "block_image.h"
#include <limine/limine_req.h>
#include <heap.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t*  base;
    uint64_t  size_bytes;
} image_ctx_t;

static bool image_read(block_device_t* dev,
                       uint64_t        lba,
                       uint32_t        count,
                       void*           buf)
{
    image_ctx_t* ctx        = (image_ctx_t*)dev->driver_data;
    uint64_t     byte_off   = (dev->lba_offset + lba) * dev->sector_size;
    uint64_t     byte_count = (uint64_t)count * dev->sector_size;

    if (byte_off + byte_count > ctx->size_bytes) {
        return false;
    }

    memcpy(buf, ctx->base + byte_off, byte_count);
    return true;
}

static bool image_write(block_device_t* dev,
                        uint64_t        lba,
                        uint32_t        count,
                        const void*     buf)
{
    (void)dev;
    (void)lba;
    (void)count;
    (void)buf;
    return false;
}

block_device_t* image_create_blockdev(const char* name, const char* mod_name)
{
    uint64_t size = 0;
    void*    base = limine_get_module(mod_name, &size);

    if (!base) {
        return NULL;
    }

    block_device_t* dev = kmalloc(sizeof(block_device_t));
    image_ctx_t*    ctx = kmalloc(sizeof(image_ctx_t));

    ctx->base       = (uint8_t*)base;
    ctx->size_bytes = size;

    dev->name         = name;
    dev->sector_size  = 512;
    dev->sector_count = size / 512;
    dev->lba_offset   = 0;
    dev->driver_data  = ctx;
    dev->read         = image_read;
    dev->write        = image_write;
    dev->lock         = NULL;

    return dev;
}