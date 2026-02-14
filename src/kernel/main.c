#include <stdint.h>
#include "stdio.h"
#include "memory.h"
#include <hal/hal.h>
#include <arch/x86_64/irq.h>
#include <debug.h>
#include <heap.h>
#include <drivers/fs/ramdisk.h>
#include <drivers/driverman.h>
#include <drivers/disk/ata.h>
#include <timer/timer.h>
#include <arch/x86_64/pagefault.h>
#include <proc/proc.h>
#include <drivers/disk/floppy.h>
#include <block/block.h>
#include <drivers/fs/fat/fat.h>
#include <block/block_floppy.h>
#include <config/config.h>
#include <arch/x86_64/syscalls.h>
#include <syscalls/scman.h>
#include "halt.h"
#include <arch/x86_64/io.h>
#include <limine/limine_req.h>
#include <fb/framebuffer.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <fb/font/fontloader.h>
#include <fb/textrenderer.h>

extern uint8_t __bss_start;
extern uint8_t __bss_end;


void kmain(void)
{   
    memset(&__bss_start, 0, (&__bss_end) - (&__bss_start));
    log_ok("Boot", "Cleared BSS");

    limine_init();
    log_ok("Boot", "Populated limine info");

    pmm_init();
    log_ok("Boot", "Started PMM");

    vmm_init();
    log_ok("Boot", "Started VMM");

    fb_init(limine_get_fb());
    fb_clear(0x000000);
    font_init();
    tr_init(0xFFFFFF, 0x000000);
    if (font_load("default.bdf")) {
        log_ok("Fonts", "Loaded default font");
    } else {
        log_crit("Fonts", "Couln't load default fonts");
        log_info("Fonts", "Using fallback font.");
    }

    init_heap();
    log_ok("Boot", "Initialized Heap");

    loadConfig();
    log_ok("Boot", "Loaded Config");
    
    HAL_Initialize();
    log_ok("Boot", "Initialized HAL");

    timer_init();
    log_ok("Boot", "Initialized timer");

    drivers_init();
    log_ok("Boot", "Initialized initial drivers");

    x86_64_PageFault_Initialize();
    log_ok("Boot", "Initialized Pagefault handler");

    proc_init();
    log_ok("Boot", "Initialized Multitasking");
    
    log_info("Kernel", "Loading syscalls");
    syscalls_init();
    register_syscalls();

    x86_64_EnableInterrupts();

    log_ok("Kernel", "Initialized all imortant systems");

    printf("\n\nWelcome to \x1b[30;47mBlackmount\x1b[36;40m OS\n");
    
    proc_start_scheduling();

    halt();
}
