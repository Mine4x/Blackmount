#include <stdint.h>
#include <debug.h>
#include <drivers/disk/ata.h>
#include <arch/i686/io.h>

// Primary ATA bus ports
#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECTOR_CNT  0x1F2
#define ATA_LBA_LOW     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HIGH    0x1F5
#define ATA_DRIVE       0x1F6
#define ATA_CMD_STATUS  0x1F7

// Status register bits
#define ATA_SR_BSY  0x80  // Busy
#define ATA_SR_DRQ  0x08  // Data request ready
#define ATA_SR_ERR  0x01  // Error

// Commands
#define ATA_CMD_READ_PIO  0x20
#define ATA_CMD_WRITE_PIO 0x30

void ata_wait_busy() {
    while (i686_inb(ATA_CMD_STATUS) & ATA_SR_BSY);
}

void ata_wait_drq() {
    while (!(i686_inb(ATA_CMD_STATUS) & ATA_SR_DRQ));
}

void ata_read_sector(uint32_t lba, uint8_t* buffer) {
    ata_wait_busy();

    i686_outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    i686_outb(ATA_SECTOR_CNT, 1);
    i686_outb(ATA_LBA_LOW,  (uint8_t)lba);
    i686_outb(ATA_LBA_MID,  (uint8_t)(lba >> 8));
    i686_outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    i686_outb(ATA_CMD_STATUS, ATA_CMD_READ_PIO);

    ata_wait_busy();
    ata_wait_drq();

    uint16_t* buf16 = (uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        buf16[i] = i686_inw(ATA_DATA);
    }
}

void ata_write_sector(uint32_t lba, uint8_t* buffer) {
    ata_wait_busy();

    i686_outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    i686_outb(ATA_SECTOR_CNT, 1);
    i686_outb(ATA_LBA_LOW,  (uint8_t)lba);
    i686_outb(ATA_LBA_MID,  (uint8_t)(lba >> 8));
    i686_outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    i686_outb(ATA_CMD_STATUS, ATA_CMD_WRITE_PIO);

    ata_wait_busy();
    ata_wait_drq();

    uint16_t* buf16 = (uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        i686_outw(ATA_DATA, buf16[i]);
    }

    ata_wait_busy();
}


void test_ata() {
    uint8_t buffer[512];
    
    for (int i = 0; i < 512; i++) {
        buffer[i] = i % 256;
    }
    ata_write_sector(0, buffer);
    
    memset(buffer, 0, 512);
    
    ata_read_sector(0, buffer);
    
    for (int i = 0; i < 512; i++) {
        if (buffer[i] != i % 256) {
            log_err("ATA", "ATA test failed at byte %d", i);
            return;
        }
    }
    log_ok("ATA", "ATA test passed!");
}