#include "block_ramdisk.h"
#include <block/block.h>
#include <heap.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <debug.h>

typedef struct {
    uint8_t*  data;
    uint64_t  size_bytes;
} ramdisk_ctx_t;

static bool ramdisk_read(block_device_t* dev,
                         uint64_t        lba,
                         uint32_t        count,
                         void*           buf)
{
    ramdisk_ctx_t* ctx       = (ramdisk_ctx_t*)dev->driver_data;
    uint64_t       byte_off  = (dev->lba_offset + lba) * dev->sector_size;
    uint64_t       byte_count = (uint64_t)count * dev->sector_size;

    if (byte_off + byte_count > ctx->size_bytes)
        return false;

    memcpy(buf, ctx->data + byte_off, byte_count);
    return true;
}

static bool ramdisk_write(block_device_t* dev,
                          uint64_t        lba,
                          uint32_t        count,
                          const void*     buf)
{
    ramdisk_ctx_t* ctx        = (ramdisk_ctx_t*)dev->driver_data;
    uint64_t       byte_off   = (dev->lba_offset + lba) * dev->sector_size;
    uint64_t       byte_count = (uint64_t)count * dev->sector_size;

    if (byte_off + byte_count > ctx->size_bytes)
        return false;

    memcpy(ctx->data + byte_off, buf, byte_count);
    return true;
}

block_device_t* ramdisk_create_blockdev(const char* name, uint64_t size_bytes)
{
    if (size_bytes == 0 || (size_bytes % 512) != 0) {
        log_err("RAMDISK", "size_bytes must be a non-zero multiple of 512");
        return NULL;
    }

    uint8_t* data = kmalloc(size_bytes);
    if (!data) {
        log_err("RAMDISK", "Failed to allocate %llu bytes for ramdisk %s",
                (unsigned long long)size_bytes, name);
        return NULL;
    }
    memset(data, 0, size_bytes);

    ramdisk_ctx_t* ctx = kmalloc(sizeof(ramdisk_ctx_t));
    if (!ctx) {
        kfree(data);
        log_err("RAMDISK", "Failed to allocate context for ramdisk %s", name);
        return NULL;
    }
    ctx->data       = data;
    ctx->size_bytes = size_bytes;

    block_device_t* dev = kmalloc(sizeof(block_device_t));
    if (!dev) {
        kfree(data);
        kfree(ctx);
        log_err("RAMDISK", "Failed to allocate block_device_t for ramdisk %s", name);
        return NULL;
    }

    dev->name         = name;
    dev->sector_size  = 512;
    dev->sector_count = size_bytes / 512;
    dev->lba_offset   = 0;
    dev->driver_data  = ctx;
    dev->read         = ramdisk_read;
    dev->write        = ramdisk_write;
    dev->lock         = NULL;

    log_ok("RAMDISK", "Created ramdisk %s (%llu bytes, %llu sectors)",
           name,
           (unsigned long long)size_bytes,
           (unsigned long long)dev->sector_count);

    return dev;
}

void ramdisk_destroy_blockdev(block_device_t* dev)
{
    if (!dev)
        return;

    ramdisk_ctx_t* ctx = (ramdisk_ctx_t*)dev->driver_data;
    if (ctx) {
        kfree(ctx->data);
        kfree(ctx);
    }

    kfree(dev);
}