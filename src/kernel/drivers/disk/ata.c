#include <stdint.h>
#include <stdbool.h>
#include <debug.h>
#include <drivers/disk/ata.h>
#include <arch/x86_64/io.h>
#include <block/block.h>
#include <block/block_ata.h>

// ATA bus definitions
#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_IO    0x170
#define ATA_SECONDARY_CTRL  0x376

// Register offsets from base
#define ATA_REG_DATA        0
#define ATA_REG_ERROR       1
#define ATA_REG_FEATURES    1
#define ATA_REG_SECTOR_CNT  2
#define ATA_REG_LBA_LOW     3
#define ATA_REG_LBA_MID     4
#define ATA_REG_LBA_HIGH    5
#define ATA_REG_DRIVE       6
#define ATA_REG_CMD_STATUS  7

// Control register
#define ATA_REG_CONTROL     0
#define ATA_REG_ALT_STATUS  0

// Status register bits
#define ATA_SR_BSY  0x80  // Busy
#define ATA_SR_DRDY 0x40  // Drive ready
#define ATA_SR_DF   0x20  // Drive fault
#define ATA_SR_DSC  0x10  // Drive seek complete
#define ATA_SR_DRQ  0x08  // Data request ready
#define ATA_SR_CORR 0x04  // Corrected data
#define ATA_SR_IDX  0x02  // Index
#define ATA_SR_ERR  0x01  // Error

// Error register bits
#define ATA_ER_BBK   0x80  // Bad block
#define ATA_ER_UNC   0x40  // Uncorrectable data
#define ATA_ER_MC    0x20  // Media changed
#define ATA_ER_IDNF  0x10  // ID not found
#define ATA_ER_MCR   0x08  // Media change request
#define ATA_ER_ABRT  0x04  // Command aborted
#define ATA_ER_TK0NF 0x02  // Track 0 not found
#define ATA_ER_AMNF  0x01  // Address mark not found

// Commands
#define ATA_CMD_READ_PIO       0x20
#define ATA_CMD_READ_PIO_EXT   0x24
#define ATA_CMD_WRITE_PIO      0x30
#define ATA_CMD_WRITE_PIO_EXT  0x34
#define ATA_CMD_CACHE_FLUSH    0xE7
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA
#define ATA_CMD_IDENTIFY       0xEC

// Drive selection bits
#define ATA_DRIVE_MASTER 0xE0
#define ATA_DRIVE_SLAVE  0xF0

// Timeouts (in milliseconds)
#define ATA_TIMEOUT_BSY  1000
#define ATA_TIMEOUT_DRQ  1000

// Maximum sectors per operation
#define ATA_MAX_SECTORS_PIO 256

typedef struct {
    uint16_t io_base;
    uint16_t ctrl_base;
    uint8_t drive_select;  // 0xE0 for master, 0xF0 for slave
    bool present;
    bool lba48_supported;
    uint64_t sector_count;
    char model[41];
    char serial[21];
} ata_device_t;

// Device array: [bus][drive] where bus 0=primary, 1=secondary; drive 0=master, 1=slave
static ata_device_t ata_devices[2][2];

// Forward declarations
static void ata_select_drive(ata_device_t* dev);
static bool ata_wait_busy(ata_device_t* dev, uint32_t timeout_ms);
static bool ata_wait_drq(ata_device_t* dev, uint32_t timeout_ms);
static bool ata_check_error(ata_device_t* dev);
static void ata_400ns_delay(ata_device_t* dev);
static bool ata_identify(ata_device_t* dev);

extern volatile uint32_t g_pit_ticks;

static uint32_t get_tick_count() {
    return g_pit_ticks;
}

static void ata_delay_ms(uint32_t ms) {
    uint32_t start = get_tick_count();
    while (get_tick_count() - start < ms);
}

static void ata_400ns_delay(ata_device_t* dev) {
    for (int i = 0; i < 4; i++) {
        x86_64_inb(dev->ctrl_base + ATA_REG_ALT_STATUS);
    }
}

static bool ata_wait_busy(ata_device_t* dev, uint32_t timeout_ms) {
    uint32_t start = get_tick_count();
    while (x86_64_inb(dev->io_base + ATA_REG_CMD_STATUS) & ATA_SR_BSY) {
        if (get_tick_count() - start > timeout_ms) {
            log_err("ATA", "Timeout waiting for BSY to clear");
            return false;
        }
    }
    return true;
}

static bool ata_wait_drq(ata_device_t* dev, uint32_t timeout_ms) {
    uint32_t start = get_tick_count();
    uint8_t status;
    while (!((status = x86_64_inb(dev->io_base + ATA_REG_CMD_STATUS)) & ATA_SR_DRQ)) {
        if (status & ATA_SR_ERR) {
            ata_check_error(dev);
            return false;
        }
        if (get_tick_count() - start > timeout_ms) {
            log_err("ATA", "Timeout waiting for DRQ");
            return false;
        }
    }
    return true;
}

static bool ata_check_error(ata_device_t* dev) {
    uint8_t status = x86_64_inb(dev->io_base + ATA_REG_CMD_STATUS);
    if (status & ATA_SR_ERR) {
        uint8_t err = x86_64_inb(dev->io_base + ATA_REG_ERROR);
        log_err("ATA", "Error status=0x%x error=0x%x", status, err);
        
        if (err & ATA_ER_BBK)   log_err("ATA", "  Bad block");
        if (err & ATA_ER_UNC)   log_err("ATA", "  Uncorrectable data");
        if (err & ATA_ER_IDNF)  log_err("ATA", "  ID not found");
        if (err & ATA_ER_ABRT)  log_err("ATA", "  Command aborted");
        if (err & ATA_ER_TK0NF) log_err("ATA", "  Track 0 not found");
        if (err & ATA_ER_AMNF)  log_err("ATA", "  Address mark not found");
        
        return true;
    }
    if (status & ATA_SR_DF) {
        log_err("ATA", "Drive fault");
        return true;
    }
    return false;
}

static void ata_select_drive(ata_device_t* dev) {
    x86_64_outb(dev->io_base + ATA_REG_DRIVE, dev->drive_select);
    ata_400ns_delay(dev);
}

static bool ata_identify(ata_device_t* dev) {
    ata_select_drive(dev);
    
    // Set sector count and LBA to 0
    x86_64_outb(dev->io_base + ATA_REG_SECTOR_CNT, 0);
    x86_64_outb(dev->io_base + ATA_REG_LBA_LOW, 0);
    x86_64_outb(dev->io_base + ATA_REG_LBA_MID, 0);
    x86_64_outb(dev->io_base + ATA_REG_LBA_HIGH, 0);
    
    // Send IDENTIFY command
    x86_64_outb(dev->io_base + ATA_REG_CMD_STATUS, ATA_CMD_IDENTIFY);
    ata_400ns_delay(dev);
    
    // Check if drive exists
    uint8_t status = x86_64_inb(dev->io_base + ATA_REG_CMD_STATUS);
    if (status == 0) {
        return false;  // Drive does not exist
    }
    
    // Wait for BSY to clear
    if (!ata_wait_busy(dev, ATA_TIMEOUT_BSY)) {
        return false;
    }
    
    // Check if this is an ATA device (not ATAPI)
    uint8_t lba_mid = x86_64_inb(dev->io_base + ATA_REG_LBA_MID);
    uint8_t lba_high = x86_64_inb(dev->io_base + ATA_REG_LBA_HIGH);
    if (lba_mid != 0 || lba_high != 0) {
        // Not an ATA device (probably ATAPI like CD-ROM)
        return false;
    }
    
    // Wait for DRQ
    if (!ata_wait_drq(dev, ATA_TIMEOUT_DRQ)) {
        return false;
    }
    
    // Read identification data
    uint16_t identify_data[256];
    for (int i = 0; i < 256; i++) {
        identify_data[i] = x86_64_inw(dev->io_base + ATA_REG_DATA);
    }
    
    // Parse identification data
    // Model string (words 27-46)
    for (int i = 0; i < 20; i++) {
        dev->model[i * 2] = identify_data[27 + i] >> 8;
        dev->model[i * 2 + 1] = identify_data[27 + i] & 0xFF;
    }
    dev->model[40] = '\0';
    
    // Serial number (words 10-19)
    for (int i = 0; i < 10; i++) {
        dev->serial[i * 2] = identify_data[10 + i] >> 8;
        dev->serial[i * 2 + 1] = identify_data[10 + i] & 0xFF;
    }
    dev->serial[20] = '\0';
    
    // Check for LBA48 support (bit 10 of word 83)
    dev->lba48_supported = (identify_data[83] & (1 << 10)) != 0;
    
    // Get sector count
    if (dev->lba48_supported) {
        // LBA48: words 100-103
        dev->sector_count = ((uint64_t)identify_data[103] << 48) |
                           ((uint64_t)identify_data[102] << 32) |
                           ((uint64_t)identify_data[101] << 16) |
                           ((uint64_t)identify_data[100]);
    } else {
        // LBA28: words 60-61
        dev->sector_count = ((uint32_t)identify_data[61] << 16) |
                           ((uint32_t)identify_data[60]);
    }
    
    dev->present = true;
    return true;
}

void ata_init() {
    log_info("ATA", "Initializing ATA subsystem...");
    
    // Initialize device structures
    for (int bus = 0; bus < 2; bus++) {
        for (int drive = 0; drive < 2; drive++) {
            ata_device_t* dev = &ata_devices[bus][drive];
            dev->io_base = (bus == 0) ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
            dev->ctrl_base = (bus == 0) ? ATA_PRIMARY_CTRL : ATA_SECONDARY_CTRL;
            dev->drive_select = (drive == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
            dev->present = false;
            dev->lba48_supported = false;
            dev->sector_count = 0;
        }
    }
    
    // Detect devices
    for (int bus = 0; bus < 2; bus++) {
        for (int drive = 0; drive < 2; drive++) {
            ata_device_t* dev = &ata_devices[bus][drive];
            
            if (ata_identify(dev)) {
                log_ok("ATA", "Detected %s on %s bus: %s",
                       drive == 0 ? "master" : "slave",
                       bus == 0 ? "primary" : "secondary",
                       dev->model);
                log_info("ATA", "  Serial: %s", dev->serial);
                log_info("ATA", "  Sectors: %llu (%llu MB)",
                         dev->sector_count,
                         (dev->sector_count * 512) / (1024 * 1024));
                log_info("ATA", "  LBA48: %s", dev->lba48_supported ? "yes" : "no");
            }
        }
    }
    
    // Count detected devices
    int device_count = 0;
    for (int bus = 0; bus < 2; bus++) {
        for (int drive = 0; drive < 2; drive++) {
            if (ata_devices[bus][drive].present) {
                device_count++;
            }
        }
    }
    
    if (device_count == 0) {
        log_warn("ATA", "No ATA devices detected");
    } else {
        log_ok("ATA", "Found %d ATA device(s)", device_count);
    }
}

static ata_device_t* ata_get_device(uint8_t bus, uint8_t drive) {
    if (bus >= 2 || drive >= 2) {
        return NULL;
    }
    ata_device_t* dev = &ata_devices[bus][drive];
    return dev->present ? dev : NULL;
}

static bool ata_read_sectors_lba28(ata_device_t* dev, uint32_t lba, uint8_t count, uint8_t* buffer) {
    if (count == 0) count = 256;  // 0 means 256 sectors
    
    ata_select_drive(dev);
    if (!ata_wait_busy(dev, ATA_TIMEOUT_BSY)) return false;
    
    // Send command parameters
    x86_64_outb(dev->io_base + ATA_REG_DRIVE, dev->drive_select | ((lba >> 24) & 0x0F));
    x86_64_outb(dev->io_base + ATA_REG_SECTOR_CNT, count);
    x86_64_outb(dev->io_base + ATA_REG_LBA_LOW,  (uint8_t)lba);
    x86_64_outb(dev->io_base + ATA_REG_LBA_MID,  (uint8_t)(lba >> 8));
    x86_64_outb(dev->io_base + ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    x86_64_outb(dev->io_base + ATA_REG_CMD_STATUS, ATA_CMD_READ_PIO);
    
    // Read sectors
    uint16_t* buf16 = (uint16_t*)buffer;
    for (int sector = 0; sector < count; sector++) {
        if (!ata_wait_busy(dev, ATA_TIMEOUT_BSY)) return false;
        if (!ata_wait_drq(dev, ATA_TIMEOUT_DRQ)) return false;
        
        for (int i = 0; i < 256; i++) {
            buf16[sector * 256 + i] = x86_64_inw(dev->io_base + ATA_REG_DATA);
        }
    }
    
    return !ata_check_error(dev);
}

static bool ata_read_sectors_lba48(ata_device_t* dev, uint64_t lba, uint16_t count, uint8_t* buffer) {
    if (count == 0) count = 65536;  // 0 means 65536 sectors
    
    ata_select_drive(dev);
    if (!ata_wait_busy(dev, ATA_TIMEOUT_BSY)) return false;
    
    // Send high bytes
    x86_64_outb(dev->io_base + ATA_REG_SECTOR_CNT, (count >> 8) & 0xFF);
    x86_64_outb(dev->io_base + ATA_REG_LBA_LOW,  (lba >> 24) & 0xFF);
    x86_64_outb(dev->io_base + ATA_REG_LBA_MID,  (lba >> 32) & 0xFF);
    x86_64_outb(dev->io_base + ATA_REG_LBA_HIGH, (lba >> 40) & 0xFF);
    
    // Send low bytes
    x86_64_outb(dev->io_base + ATA_REG_SECTOR_CNT, count & 0xFF);
    x86_64_outb(dev->io_base + ATA_REG_LBA_LOW,  lba & 0xFF);
    x86_64_outb(dev->io_base + ATA_REG_LBA_MID,  (lba >> 8) & 0xFF);
    x86_64_outb(dev->io_base + ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);
    
    x86_64_outb(dev->io_base + ATA_REG_CMD_STATUS, ATA_CMD_READ_PIO_EXT);
    
    // Read sectors
    uint16_t* buf16 = (uint16_t*)buffer;
    for (int sector = 0; sector < count; sector++) {
        if (!ata_wait_busy(dev, ATA_TIMEOUT_BSY)) return false;
        if (!ata_wait_drq(dev, ATA_TIMEOUT_DRQ)) return false;
        
        for (int i = 0; i < 256; i++) {
            buf16[sector * 256 + i] = x86_64_inw(dev->io_base + ATA_REG_DATA);
        }
    }
    
    return !ata_check_error(dev);
}

static bool ata_write_sectors_lba28(ata_device_t* dev, uint32_t lba, uint8_t count, const uint8_t* buffer) {
    if (count == 0) count = 256;
    
    ata_select_drive(dev);
    if (!ata_wait_busy(dev, ATA_TIMEOUT_BSY)) return false;
    
    // Send command parameters
    x86_64_outb(dev->io_base + ATA_REG_DRIVE, dev->drive_select | ((lba >> 24) & 0x0F));
    x86_64_outb(dev->io_base + ATA_REG_SECTOR_CNT, count);
    x86_64_outb(dev->io_base + ATA_REG_LBA_LOW,  (uint8_t)lba);
    x86_64_outb(dev->io_base + ATA_REG_LBA_MID,  (uint8_t)(lba >> 8));
    x86_64_outb(dev->io_base + ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    x86_64_outb(dev->io_base + ATA_REG_CMD_STATUS, ATA_CMD_WRITE_PIO);
    
    // Write sectors
    const uint16_t* buf16 = (const uint16_t*)buffer;
    for (int sector = 0; sector < count; sector++) {
        if (!ata_wait_busy(dev, ATA_TIMEOUT_BSY)) return false;
        if (!ata_wait_drq(dev, ATA_TIMEOUT_DRQ)) return false;
        
        for (int i = 0; i < 256; i++) {
            x86_64_outw(dev->io_base + ATA_REG_DATA, buf16[sector * 256 + i]);
        }
    }
    
    if (!ata_wait_busy(dev, ATA_TIMEOUT_BSY)) return false;
    return !ata_check_error(dev);
}

static bool ata_write_sectors_lba48(ata_device_t* dev, uint64_t lba, uint16_t count, const uint8_t* buffer) {
    if (count == 0) count = 65536;
    
    ata_select_drive(dev);
    if (!ata_wait_busy(dev, ATA_TIMEOUT_BSY)) return false;
    
    // Send high bytes
    x86_64_outb(dev->io_base + ATA_REG_SECTOR_CNT, (count >> 8) & 0xFF);
    x86_64_outb(dev->io_base + ATA_REG_LBA_LOW,  (lba >> 24) & 0xFF);
    x86_64_outb(dev->io_base + ATA_REG_LBA_MID,  (lba >> 32) & 0xFF);
    x86_64_outb(dev->io_base + ATA_REG_LBA_HIGH, (lba >> 40) & 0xFF);
    
    // Send low bytes
    x86_64_outb(dev->io_base + ATA_REG_SECTOR_CNT, count & 0xFF);
    x86_64_outb(dev->io_base + ATA_REG_LBA_LOW,  lba & 0xFF);
    x86_64_outb(dev->io_base + ATA_REG_LBA_MID,  (lba >> 8) & 0xFF);
    x86_64_outb(dev->io_base + ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);
    
    x86_64_outb(dev->io_base + ATA_REG_CMD_STATUS, ATA_CMD_WRITE_PIO_EXT);
    
    // Write sectors
    const uint16_t* buf16 = (const uint16_t*)buffer;
    for (int sector = 0; sector < count; sector++) {
        if (!ata_wait_busy(dev, ATA_TIMEOUT_BSY)) return false;
        if (!ata_wait_drq(dev, ATA_TIMEOUT_DRQ)) return false;
        
        for (int i = 0; i < 256; i++) {
            x86_64_outw(dev->io_base + ATA_REG_DATA, buf16[sector * 256 + i]);
        }
    }
    
    if (!ata_wait_busy(dev, ATA_TIMEOUT_BSY)) return false;
    return !ata_check_error(dev);
}

bool ata_flush_cache(uint8_t bus, uint8_t drive) {
    ata_device_t* dev = ata_get_device(bus, drive);
    if (!dev) return false;
    
    ata_select_drive(dev);
    if (!ata_wait_busy(dev, ATA_TIMEOUT_BSY)) return false;
    
    uint8_t cmd = dev->lba48_supported ? ATA_CMD_CACHE_FLUSH_EXT : ATA_CMD_CACHE_FLUSH;
    x86_64_outb(dev->io_base + ATA_REG_CMD_STATUS, cmd);
    
    if (!ata_wait_busy(dev, ATA_TIMEOUT_BSY)) return false;
    return !ata_check_error(dev);
}

bool ata_read_sectors(uint8_t bus, uint8_t drive, uint64_t lba, uint16_t count, uint8_t* buffer) {
    ata_device_t* dev = ata_get_device(bus, drive);
    if (!dev) {
        log_err("ATA", "Invalid device %d:%d", bus, drive);
        return false;
    }
    
    if (lba + count > dev->sector_count) {
        log_err("ATA", "Read beyond end of device");
        return false;
    }
    
    // Use LBA48 if necessary and supported
    if (dev->lba48_supported && (lba > 0x0FFFFFFF || count > 256)) {
        return ata_read_sectors_lba48(dev, lba, count, buffer);
    } else {
        return ata_read_sectors_lba28(dev, (uint32_t)lba, (uint8_t)count, buffer);
    }
}

bool ata_write_sectors(uint8_t bus, uint8_t drive, uint64_t lba, uint16_t count, const uint8_t* buffer) {
    ata_device_t* dev = ata_get_device(bus, drive);
    if (!dev) {
        log_err("ATA", "Invalid device %d:%d", bus, drive);
        return false;
    }
    
    if (lba + count > dev->sector_count) {
        log_err("ATA", "Write beyond end of device");
        return false;
    }
    
    // Use LBA48 if necessary and supported
    if (dev->lba48_supported && (lba > 0x0FFFFFFF || count > 256)) {
        return ata_write_sectors_lba48(dev, lba, count, buffer);
    } else {
        return ata_write_sectors_lba28(dev, (uint32_t)lba, (uint8_t)count, buffer);
    }
}

// Compatibility functions for existing code
void ata_read_sector(uint32_t lba, uint8_t* buffer) {
    ata_read_sectors(0, 0, lba, 1, buffer);  // Primary master
}

void ata_write_sector(uint32_t lba, uint8_t* buffer) {
    ata_write_sectors(0, 0, lba, 1, buffer);  // Primary master
}

block_device_t* ata_create_primary_blockdev() {
    // Check if primary master exists
    ata_device_t* dev = ata_get_device(0, 0);
    if (!dev) {
        log_err("ATA", "No primary master device available");
        return NULL;
    }
    
    log_info("ATA", "Creating block device for primary master: %s", dev->model);
    
    block_device_t* blockdev = ata_create_blockdev(
        "hda",           // name
        0,               // bus (primary)
        0,               // drive (master)
        dev->sector_count // sectors
    );
    
    if (blockdev) {
        log_ok("ATA", "Block device 'hda' created successfully");
    } else {
        log_err("ATA", "Failed to create block device");
    }
    
    return blockdev;
}

void test_ata() {
    log_info("ATA", "Running ATA diagnostics...");
    
    // Test primary master
    ata_device_t* dev = ata_get_device(0, 0);
    if (!dev) {
        log_err("ATA", "No primary master device available for testing");
        return;
    }
    
    log_info("ATA", "Testing device: %s", dev->model);
    
    uint8_t buffer[512 * 4];  // Test 4 sectors
    
    // Fill with test pattern
    for (int i = 0; i < 512 * 4; i++) {
        buffer[i] = i % 256;
    }
    
    // Write test
    if (!ata_write_sectors(0, 0, 0, 4, buffer)) {
        log_err("ATA", "Write test failed");
        return;
    }
    
    // Flush cache
    if (!ata_flush_cache(0, 0)) {
        log_err("ATA", "Cache flush failed");
        return;
    }
    
    // Clear buffer
    memset(buffer, 0, 512 * 4);
    
    // Read test
    if (!ata_read_sectors(0, 0, 0, 4, buffer)) {
        log_err("ATA", "Read test failed");
        return;
    }
    
    // Verify
    for (int i = 0; i < 512 * 4; i++) {
        if (buffer[i] != i % 256) {
            log_err("ATA", "Data verification failed at byte %d (expected %d, got %d)", 
                    i, i % 256, buffer[i]);
            return;
        }
    }
    
    log_ok("ATA", "All diagnostics passed - 4 sectors read/write OK");
}