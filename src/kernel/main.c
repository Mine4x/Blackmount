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
#include <arch/x86_64/paging.h>
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
#include <panic/panic.h>

extern uint8_t __bss_start;
extern uint8_t __bss_end;

void test1() {
    while (true)
    {
        log_info("TEST", "Test 1");
    }
    
}

void test2() {
    while (true)
    {
        log_info("TEST", "Test 2");
    }
    
}


void kmain(void)
{   
    memset(&__bss_start, 0, (&__bss_end) - (&__bss_start));
    log_ok("Boot", "Cleared BSS");

    limine_init();
    log_ok("Boot", "Populated limine info");

    loadConfig();
    log_ok("Kernel", "Loaded Config");
    
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

    //proc_create(test1, 0, 0);
    //proc_create(test2, 0, 0);
    //proc_start_scheduling();
    // TODO: Fix

    halt();
}
