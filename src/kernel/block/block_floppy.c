#include <block/block.h>
#include <heap.h>
#include <stddef.h>
#include <string.h>
#include <drivers/disk/floppy.h>

typedef struct {
    uint8_t drive;
} floppy_ctx_t;

static bool floppy_block_read(block_device_t* dev, uint64_t lba, uint32_t count, void* buf) {
    floppy_ctx_t* ctx = dev->driver_data;
    uint32_t remaining = count;
    uint32_t offset = 0;

    while (remaining > 0) {
        uint8_t chunk = remaining > 18 ? 18 : remaining;
        if (!floppy_read_sectors(ctx->drive, (uint32_t)(lba + offset), chunk, (uint8_t*)buf + offset * dev->sector_size)) {
            return false;
        }
        remaining -= chunk;
        offset += chunk;
    }
    return true;
}

static bool floppy_block_write(block_device_t* dev, uint64_t lba, uint32_t count, const void* buf) {
    floppy_ctx_t* ctx = dev->driver_data;
    uint32_t remaining = count;
    uint32_t offset = 0;

    while (remaining > 0) {
        uint8_t chunk = remaining > 18 ? 18 : remaining;
        if (!floppy_write_sectors(ctx->drive, (uint32_t)(lba + offset), chunk, (const uint8_t*)buf + offset * dev->sector_size)) {
            return false;
        }
        remaining -= chunk;
        offset += chunk;
    }
    return true;
}

block_device_t* floppy_create_blockdev(const char* name, uint8_t drive) {
    block_device_t* dev = kmalloc(sizeof(block_device_t));
    floppy_ctx_t* ctx = kmalloc(sizeof(floppy_ctx_t));

    ctx->drive = drive;

    floppy_geometry_t geo = floppy_get_geometry(drive);

    dev->name = name;
    dev->sector_count = (uint64_t)geo.tracks * geo.heads * geo.sectors_per_track;
    dev->sector_size = geo.bytes_per_sector;
    dev->lba_offset = 0;
    dev->driver_data = ctx;
    dev->read = floppy_block_read;
    dev->write = floppy_block_write;
    dev->lock = NULL;

    return dev;
}
