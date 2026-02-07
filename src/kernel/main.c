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

extern uint8_t __bss_start;
extern uint8_t __end;

void test1() {
    block_device_t* hda = ata_create_primary_blockdev();
    fat_fs_t* hda_fat = NULL;
    if(hda) {
        hda_fat = fat_mount(hda);
    }
    fat_file_t test;
    fat_open(hda_fat, "test.txt", &test);
    uint32_t file_size = 1024;
    char* buffer = kmalloc(file_size + 1);
    fat_read(&test, buffer, file_size);
    printf("%s\n", buffer);
    kfree(buffer);
}

void test2(uint16_t bootDrive) {
    block_device_t* fda = floppy_create_blockdev("fda", bootDrive);
    fat_fs_t* fda_fat = NULL;
    if (fda) {
        fda_fat = fat_mount(fda);
    }
    fat_file_t test;
    fat_open(fda_fat, "test.txt", &test);
    uint32_t file_size = 1024;
    char* buffer = kmalloc(file_size + 1);
    fat_read(&test, buffer, file_size);
    printf("%s\n", buffer);
    kfree(buffer);
}

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
    log_info("Kernel", "Starting FDC");
    floppy_init();

    log_info("Kernel", "Creating important files");

    ramdisk_create_dir("/sysbin");
    ramdisk_create_dir("/bin");

    log_ok("Kernel", "Created all important files");
   
    log_warn("Kernel", "If you are looking for the shell, it was removed in an older update since a shell doesn't really belong in a Kernel");

    printf("\n\nWelcome to \x1b[30;47mBlackmount\x1b[36;40m OS\n");

    test1();
    test2(bootDrive);

    proc_start_scheduling();

end:
    for (;;);
}
