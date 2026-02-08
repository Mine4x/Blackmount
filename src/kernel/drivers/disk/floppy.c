#include "floppy.h"
#include <debug.h>
#include <string.h>

// FDC I/O Ports
#define FDC_DOR  0x3F2  // Digital Output Register
#define FDC_MSR  0x3F4  // Main Status Register
#define FDC_DATA 0x3F5  // Data FIFO
#define FDC_CCR  0x3F7  // Configuration Control Register

// DMA Ports
#define DMA_ADDR   0x04
#define DMA_COUNT  0x05
#define DMA_PAGE   0x81
#define DMA_MODE   0x0B
#define DMA_RESET  0x0C
#define DMA_UNMASK 0x0A

// FDC Commands
#define CMD_SPECIFY         0x03
#define CMD_WRITE_DATA      0xC5
#define CMD_READ_DATA       0xE6
#define CMD_RECALIBRATE     0x07
#define CMD_SENSE_INTERRUPT 0x08
#define CMD_SEEK            0x0F
#define CMD_VERSION         0x10

// MSR bits
#define MSR_RQM  0x80  // Request for Master
#define MSR_DIO  0x40  // Data Input/Output
#define MSR_NDMA 0x20  // Non-DMA mode
#define MSR_BUSY 0x10  // FDC is busy

// DOR bits
#define DOR_RESET  0x00
#define DOR_DMAEN  0x08
#define DOR_MOTA   0x10
#define DOR_MOTB   0x20
#define DOR_MOTC   0x40
#define DOR_MOTD   0x80

// 1.44MB geometry
static floppy_geometry_t g_geometry = {
    .heads = 2,
    .tracks = 80,
    .sectors_per_track = 18,
    .bytes_per_sector = 512
};

// DMA buffer (must be in lower 16MB and not cross 64KB boundary)
static uint8_t __attribute__((aligned(0x8000))) g_dma_buffer[512 * 18];
static volatile bool g_irq_received = false;

extern void i686_outb(uint16_t port, uint8_t value);
extern uint8_t i686_inb(uint16_t port);
extern void i686_iowait(void);
extern uint8_t i686_DisableInterrupts(void);
extern uint8_t i686_EnableInterrupts(void);

// Wait for status
static bool fdc_wait_ready(bool write) {
    for (int i = 0; i < 600; i++) {
        uint8_t msr = i686_inb(FDC_MSR);
        if ((msr & MSR_RQM) && (write || (msr & MSR_DIO))) {
            return true;
        }
        i686_iowait();
    }
    log_err("FDC", "Timeout waiting for controller ready");
    return false;
}

// Send byte to FDC
static bool fdc_write_byte(uint8_t byte) {
    if (!fdc_wait_ready(true)) return false;
    i686_outb(FDC_DATA, byte);
    return true;
}

// Read byte from FDC
static bool fdc_read_byte(uint8_t* byte) {
    if (!fdc_wait_ready(false)) return false;
    *byte = i686_inb(FDC_DATA);
    return true;
}

// Setup DMA for transfer
static void fdc_setup_dma(void* buffer, uint16_t length, bool write) {
    uint32_t addr = (uint32_t)buffer;
    uint8_t mode = write ? 0x4A : 0x46; // Read=0x46, Write=0x4A
    
    i686_outb(DMA_RESET, 0xFF);
    i686_outb(DMA_MODE, mode);
    i686_outb(DMA_ADDR, (addr >> 0) & 0xFF);
    i686_outb(DMA_ADDR, (addr >> 8) & 0xFF);
    i686_outb(DMA_PAGE, (addr >> 16) & 0xFF);
    i686_outb(DMA_COUNT, (length - 1) & 0xFF);
    i686_outb(DMA_COUNT, ((length - 1) >> 8) & 0xFF);
    i686_outb(DMA_UNMASK, 0x02);
}

// Motor control
static void fdc_motor(uint8_t drive, bool on) {
    uint8_t motor_bit = DOR_MOTA << drive;
    uint8_t dor = DOR_DMAEN | (on ? motor_bit : 0) | drive;
    i686_outb(FDC_DOR, dor);
    
    if (on) {
        // Wait for motor spin-up (500ms approximation)
        for (volatile int i = 0; i < 500000; i++);
    }
}

// Reset controller
static bool fdc_reset(void) {
    log_info("FDC", "Resetting controller");
    
    i686_outb(FDC_DOR, DOR_RESET);
    i686_iowait();
    i686_outb(FDC_DOR, DOR_DMAEN);
    
    // Wait for interrupt
    for (volatile int i = 0; i < 100000 && !g_irq_received; i++);
    g_irq_received = false;
    
    // Sense interrupt for all drives
    for (int i = 0; i < 4; i++) {
        fdc_write_byte(CMD_SENSE_INTERRUPT);
        uint8_t st0, cyl;
        fdc_read_byte(&st0);
        fdc_read_byte(&cyl);
    }
    
    // Configure controller
    i686_outb(FDC_CCR, 0); // 500 Kbps
    
    fdc_write_byte(CMD_SPECIFY);
    fdc_write_byte(0xDF); // SRT=3ms, HUT=240ms
    fdc_write_byte(0x02); // HLT=16ms, DMA mode
    
    return true;
}

// Calibrate drive (seek to track 0)
static bool fdc_calibrate(uint8_t drive) {
    log_debug("FDC", "Calibrating drive %d", drive);
    
    fdc_motor(drive, true);
    
    for (int attempt = 0; attempt < 10; attempt++) {
        g_irq_received = false;
        
        fdc_write_byte(CMD_RECALIBRATE);
        fdc_write_byte(drive);
        
        // Wait for IRQ
        for (volatile int i = 0; i < 100000 && !g_irq_received; i++);
        
        fdc_write_byte(CMD_SENSE_INTERRUPT);
        uint8_t st0, cyl;
        fdc_read_byte(&st0);
        fdc_read_byte(&cyl);
        
        if (cyl == 0) {
            log_ok("FDC", "Drive %d calibrated", drive);
            return true;
        }
    }
    
    log_err("FDC", "Failed to calibrate drive %d", drive);
    return false;
}

// Seek to cylinder
static bool fdc_seek(uint8_t drive, uint8_t cylinder, uint8_t head) {
    g_irq_received = false;
    
    fdc_write_byte(CMD_SEEK);
    fdc_write_byte((head << 2) | drive);
    fdc_write_byte(cylinder);
    
    // Wait for IRQ
    for (volatile int i = 0; i < 100000 && !g_irq_received; i++);
    
    fdc_write_byte(CMD_SENSE_INTERRUPT);
    uint8_t st0, cyl;
    fdc_read_byte(&st0);
    fdc_read_byte(&cyl);
    
    return cyl == cylinder;
}

// Convert LBA to CHS
static void lba_to_chs(uint32_t lba, uint8_t* cyl, uint8_t* head, uint8_t* sector) {
    *cyl = lba / (g_geometry.heads * g_geometry.sectors_per_track);
    *head = (lba / g_geometry.sectors_per_track) % g_geometry.heads;
    *sector = (lba % g_geometry.sectors_per_track) + 1; // Sectors are 1-based
}

// Read/Write operation
static bool fdc_rw_operation(uint8_t drive, uint8_t cyl, uint8_t head, 
                             uint8_t sector, uint8_t count, bool write) {
    fdc_motor(drive, true);
    
    if (!fdc_seek(drive, cyl, head)) {
        log_err("FDC", "Seek failed to C:%d H:%d", cyl, head);
        fdc_motor(drive, false);
        return false;
    }
    
    size_t transfer_size = count * g_geometry.bytes_per_sector;
    fdc_setup_dma(g_dma_buffer, transfer_size, write);
    
    g_irq_received = false;
    
    fdc_write_byte(write ? CMD_WRITE_DATA : CMD_READ_DATA);
    fdc_write_byte((head << 2) | drive);
    fdc_write_byte(cyl);
    fdc_write_byte(head);
    fdc_write_byte(sector);
    fdc_write_byte(2); // 512 bytes/sector
    fdc_write_byte(g_geometry.sectors_per_track);
    fdc_write_byte(0x1B); // GAP3 length
    fdc_write_byte(0xFF); // Data length (unused)
    
    // Wait for IRQ
    for (volatile int i = 0; i < 1000000 && !g_irq_received; i++);
    
    if (!g_irq_received) {
        log_err("FDC", "Timeout waiting for transfer completion");
        fdc_motor(drive, false);
        return false;
    }
    
    // Read result bytes
    uint8_t st0, st1, st2;
    fdc_read_byte(&st0);
    fdc_read_byte(&st1);
    fdc_read_byte(&st2);
    fdc_read_byte(&cyl);
    fdc_read_byte(&head);
    fdc_read_byte(&sector);
    fdc_read_byte(&count); // bytes per sector code
    
    fdc_motor(drive, false);
    
    if ((st0 & 0xC0) != 0) {
        log_err("FDC", "Transfer error: ST0=%x ST1=%x ST2=%x", st0, st1, st2);
        return false;
    }
    
    return true;
}

void floppy_irq_handler(void) {
    g_irq_received = true;
}

// Initialize driver
bool floppy_init(void) {
    log_info("FDC", "Initializing floppy disk controller");
    
    if (!fdc_reset()) {
        log_err("FDC", "Reset failed");
        return false;
    }
    
    // Check FDC version
    fdc_write_byte(CMD_VERSION);
    uint8_t version;
    if (!fdc_read_byte(&version)) {
        log_err("FDC", "Failed to read version");
        return false;
    }
    
    log_info("FDC", "Controller version: 0x%x", version);
    
    if (!fdc_calibrate(0)) {
        log_warn("FDC", "Drive 0 calibration failed - may not be present");
    }
    
    log_ok("FDC", "Initialization complete");
    return true;
}

// Read sectors
bool floppy_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void* buffer) {
    if (count > 18) {
        log_err("FDC", "Cannot read more than 18 sectors at once");
        return false;
    }
    
    uint8_t cyl, head, sector;
    lba_to_chs(lba, &cyl, &head, &sector);
    
    log_debug("FDC", "Read LBA %d (C:%d H:%d S:%d) count=%d", lba, cyl, head, sector, count);
    
    if (!fdc_rw_operation(drive, cyl, head, sector, count, false)) {
        return false;
    }
    
    memcpy(buffer, g_dma_buffer, count * g_geometry.bytes_per_sector);
    return true;
}

// Write sectors
bool floppy_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void* buffer) {
    if (count > 18) {
        log_err("FDC", "Cannot write more than 18 sectors at once");
        return false;
    }
    
    uint8_t cyl, head, sector;
    lba_to_chs(lba, &cyl, &head, &sector);
    
    log_debug("FDC", "Write LBA %d (C:%d H:%d S:%d) count=%d", lba, cyl, head, sector, count);
    
    memcpy(g_dma_buffer, buffer, count * g_geometry.bytes_per_sector);
    
    return fdc_rw_operation(drive, cyl, head, sector, count, true);
}

// Get geometry
floppy_geometry_t floppy_get_geometry(uint8_t drive) {
    return g_geometry;
}