#ifndef FLOPPY_H
#define FLOPPY_H

#include <stdint.h>
#include <stdbool.h>
#include <arch/x86_64/irq.h>

// Floppy disk geometry
typedef struct {
    uint8_t heads;
    uint8_t tracks;
    uint8_t sectors_per_track;
    uint16_t bytes_per_sector;
} floppy_geometry_t;

// Initialize the floppy driver
bool floppy_init(void);

// Read sectors from floppy
bool floppy_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void* buffer);

// Write sectors to floppy
bool floppy_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void* buffer);

void floppy_irq_handler(Registers* regs);

// Get geometry info
floppy_geometry_t floppy_get_geometry(uint8_t drive);

#endif // FLOPPY_H