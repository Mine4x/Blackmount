#include <block/block.h>
#include <drivers/disk/ata.h>
#include <heap.h>
#include <stddef.h>

typedef struct {
    uint8_t bus;
    uint8_t drive;
} ata_ctx_t;

static bool ata_read(block_device_t* dev, uint64_t lba, uint32_t count, void* buf) {
    ata_ctx_t* ctx = dev->driver_data;
    return ata_read_sectors(ctx->bus, ctx->drive,
        dev->lba_offset + lba, count, buf);
}

static bool ata_write(block_device_t* dev, uint64_t lba, uint32_t count, const void* buf) {
    ata_ctx_t* ctx = dev->driver_data;
    return ata_write_sectors(ctx->bus, ctx->drive,
        dev->lba_offset + lba, count, buf);
}

block_device_t* ata_create_blockdev(
    const char* name,
    uint8_t bus,
    uint8_t drive,
    uint64_t sectors
) {
    block_device_t* dev = kmalloc(sizeof(block_device_t));
    ata_ctx_t* ctx = kmalloc(sizeof(ata_ctx_t));

    ctx->bus = bus;
    ctx->drive = drive;

    dev->name = name;
    dev->sector_count = sectors;
    dev->sector_size = 512;
    dev->lba_offset = 0;
    dev->driver_data = ctx;
    dev->read = ata_read;
    dev->write = ata_write;
    dev->lock = NULL;

    return dev;
}
