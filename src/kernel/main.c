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
#include <drivers/disk/floppy.h>
#include <block/block.h>
#include <drivers/fs/fat/fat.h>
#include <block/block_floppy.h>
#include <config/config.h>
#include <arch/i686/syscalls.h>
#include <syscalls/scman.h>

extern uint8_t __bss_start;
extern uint8_t __end;

void __attribute__((section(".entry"))) start(uint16_t bootDrive)
{
    memset(&__bss_start, 0, (&__end) - (&__bss_start));
    log_ok("Boot", "Cleared BSS");

    HAL_Initialize();
    log_ok("Boot", "Initialized HAL");

    init_heap();
    log_ok("Boot", "Initialized Heap");

    timer_init();
    log_ok("Boot", "Initialized timer");

    drivers_init();
    log_ok("Boot", "Initialized initial drivers");

    paging_init();
    log_ok("Boot", "Initialized Paging");

    i686_PageFault_Initialize();
    log_ok("Boot", "Initialized Pagefault handler");
    
    proc_init();
    log_ok("Boot", "Initialized Multitasking");
    
    log_info("Kernel", "Mounting Floppy bootdrive");
    block_device_t* boot_block = floppy_create_blockdev("boot", bootDrive);
    fat_fs_t* boot_fs = fat_mount(boot_block);
    
    log_info("Kernel", "Loading Config from boot drive");
    loadConfig(boot_fs);
    
    log_info("Kernel", "Loading syscalls");
    syscalls_init();
    register_syscalls();

    log_ok("Kernel", "Initialized all imortant systems");

    printf("\n\nWelcome to \x1b[30;47mBlackmount\x1b[36;40m OS\n");

    proc_start_scheduling();

end:
    for (;;);
}
