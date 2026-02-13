#include <drivers/driverman.h>
#include <debug.h>
#include <drivers/input/keyboard/ps2.h>
#include <heap.h>
#include <stdio.h>
#include <drivers/disk/ata.h>
#include <drivers/disk/floppy.h>
#include <config/config.h>
#include <util/str_to_int.h>

// TODO: Should scan for devices an load drivers but wont be implemented for now

static void input_keyboard_binding(char c) {
    printf("%c", c);
}


void drivers_init(void) {
    log_info("Drivers", "Starting Keyboard drivers");
    ps2_keyboard_init();
    ps2_keyboard_bind(&input_keyboard_binding);
    log_info("Drivers", "Started Keyboard drivers");\
    log_info("Drivers", "Starting Disk drivers");
    floppy_init();
    log_debug("Drivers", "Started FDC");
    ata_init();
    log_debug("Drivers", "Started ATA driver");
    log_info("Drivers", "Started Disk drivers");\

    log_ok("Drivers", "All drivers started");
}