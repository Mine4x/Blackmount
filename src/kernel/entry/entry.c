#include <stdint.h>
#include <memory.h>
#include <limine/limine_req.h>
#include <debug.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <fb/framebuffer.h>
#include <fb/textrenderer.h>
#include <fb/font/fontloader.h>
#include <heap.h>
#include <config/config.h>
#include <hal/hal.h>
#include <timer/timer.h>
#include <drivers/driverman.h>
#include <arch/x86_64/pagefault.h>
#include <proc/proc.h>
#include <arch/x86_64/syscalls.h>
#include <syscalls/scman.h>
#include <arch/x86_64/io.h>
#include <halt.h>
#include <hal/vfs.h>
#include <drivers/disk/ata.h>

#include <block/block_image.h>
#include <drivers/acpi/acpi.h>
#include <drivers/pci/pci.h>

extern uint8_t __bss_start;
extern uint8_t __bss_end;

void kmain(void)
{
    memset(&__bss_start, 0, (&__bss_end) - (&__bss_start));
    log_ok("Boot", "Cleared BSS");

    limine_init();
    log_ok("Boot", "Populated limine info");

    pmm_init();
    log_ok("Boot", "Initialized PMM");

    vmm_init();
    log_ok("Boot", "Initialized VMM");

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
    log_ok("Boot", "Initialized Text rendering");
    
    init_heap();
    log_ok("Boot", "Initialized Heap");

    loadConfig();
    log_ok("Boot", "Loaded Kernel Config");

    HAL_Initialize();
    log_ok("Boot", "Initialized HAL");

    x86_64_PageFault_Initialize();
    log_ok("Boot", "Initialized Pagefault handler");

    VFS_Init();
    log_ok("Boot", "Initialized VFS");

    timer_init();
    log_ok("Boot", "Initialized timer");

    drivers_init();
    log_ok("Boot", "Initialized initial drivers");

    acpi_init();
    log_ok("Boot", "Initialized acpi");

    pci_init();
    log_ok("Boot", "Initialized pci");

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