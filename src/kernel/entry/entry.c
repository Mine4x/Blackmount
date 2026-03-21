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
#include <drivers/apic/lapic.h>
#include <drivers/acpi/acpi.h>
#include <drivers/pci/pci.h>
#include <panic/panic.h>
#include <mem/dma.h>
#include <drivers/usb/xhci/xhci.h>
#include <drivers/usb/xhci/hid_keyboard.h>
#include <loaders/bin_loader.h>
#include <device/device.h>
#include <console/console.h>
#include <user/user.h>

extern uint8_t __bss_start;
extern uint8_t __bss_end;

static void ok(const char* string)
{
    //printf("[  \x1b[32mOK\x1b[0m  ] %s\n", string);
}

static void fail(const char* string) {
    printf("[\033[1;31mFAILED\x1b[0m] %s\n", string);
}

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
    ok("Initialized Text rendering");

    HAL_Initialize();
    log_ok("Boot", "Initialized HAL");
    ok("Initialized HAL");

    init_heap();
    log_ok("Boot", "Initialized Heap");
    ok("Initialized Heap");

    device_init();
    log_ok("Boot", "Initialized Device list");
    ok("Initialized Device list");

    dma_init();
    log_ok("boot", "Initialized DMA Allocator");
    ok("Initialized DMA Allocator");

    timer_init();
    lapic_timer_start(LAPIC_TIMER_PERIODIC, 100);
    log_ok("Boot", "Initialized timer");
    ok("Initialized PIT");

    acpi_init();
    log_ok("Boot", "Initialized acpi");
    ok("Initialized ACPI");

    pci_init();
    log_ok("Boot", "Initialized pci");
    ok("Initialized PCI");

    hid_keyboard_init(); // Works on some keyboards but not others; Further testing using more keyboard required

    x86_64_EnableInterrupts();
    int response = xhci_init_device();
    if (response == 0) {
        log_ok("Boot", "Initialized xHCI");
        ok("Initialized xHCI");
        response = xhci_start_device();
        if (response == 0) {
            log_ok("Boot", "Started xHCI");
            ok("Started xHCI");
        } else {
            log_err("Boot", "xHCI init exited with error: %d", response);
            fail("Unable to start xHCI");
        }
    } else {
        log_err("Boot", "xHCI init exited with error: %d", response);
        fail("Unable to initialize xHCI");
    }
    x86_64_DisableInterrupts();

    loadConfig();
    log_ok("Boot", "Loaded Kernel Config");
    ok("Loaded Kernel Config");

    if (user_init() < 0)
    {
        panic("Boot", "Unable to initalize usersystem");
    }
    log_ok("Boot", "Initialized usersystem");
    
    VFS_Init();
    log_ok("Boot", "Initialized VFS");
    ok("Initialized VFS");

    VFS_Create("/root", true);
    user_load_from_disk();
    user_save_to_disk();

    fb_make_dev();
    console_make_dev();

    proc_init();
    log_ok("Boot", "Initialized Multitasking");
    ok("Initialized Multitasking");

    drivers_init();
    log_ok("Boot", "Initialized initial drivers");
    ok("Initialized initial drivers");

    log_info("Kernel", "Loading syscalls");
    x86_64_Syscall_Initialize();
    register_syscalls();
    log_ok("Kernel", "Loaded and registerd syscalls");
    ok("Loaded and registerd syscalls");

    int r = bin_load_elf("/bin/misys", 10, 0);
    if (r < 0)
    {
        log_err("Kernel", "Failed to load binary: %d", r);
    }
    
    x86_64_EnableInterrupts();

    log_ok("Kernel", "Initialized all imortant systems");
    ok("Kernel Started completly");

    proc_start_scheduling();

    halt();
}