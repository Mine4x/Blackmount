#include <stdint.h>
#include "stdio.h"
#include "memory.h"
#include <hal/hal.h>
#include <arch/i686/irq.h>
#include <debug.h>
#include <heap.h>
#include <drivers/fs/ramdisk.h>
#include <drivers/driverman.h>
#include <drivers/disk/ata.h>
#include <timer/timer.h>
#include <arch/i686/paging.h>
#include <arch/i686/pagefault.h>
#include <proc/proc.h>

extern uint8_t __bss_start;
extern uint8_t __end;

void __attribute__((section(".entry"))) start(uint16_t bootDrive)
{
    memset(&__bss_start, 0, (&__end) - (&__bss_start));
    log_ok("Boot", "Did memset");

    HAL_Initialize();
    log_ok("Boot", "Initialized HAL");

    init_heap();
    log_ok("Boot", "Initialized Heap");

    ramdisk_init_fs();
    log_ok("Boot", "Initialized Ramdisk");

    timer_init();
    log_ok("Boot", "Initialized timer");

    drivers_init();

    log_ok("Kernel", "Initialized all imortant systems");

    log_warn("Kernel", "Initializing EXPERIMENTAL features");
    log_info("Kernel", "Starting Paging");
    paging_init();
    log_info("Kernel", "Starting Pagefault handler");
    i686_PageFault_Initialize();
    log_info("Kernel", "Starting ATA");
    ata_init();
    log_info("Kernel", "Starting Proc");
    proc_init();

    log_info("Kernel", "Creating important files");

    ramdisk_create_dir("/sysbin");
    ramdisk_create_dir("/bin");

    log_ok("Kernel", "Created all important files");
    
    printf("\n\nWelcome to \x1b[30;47mBlackmount\x1b[36;40m OS\n");

    proc_start_scheduling();

end:
    for (;;);
}
