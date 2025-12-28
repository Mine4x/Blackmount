#include <stdint.h>
#include "stdio.h"
#include "memory.h"
#include <hal/hal.h>
#include <arch/i686/irq.h>
#include <debug.h>
#include <heap.h>
#include <fs/fs.h>
#include <drivers/driverman.h>
//#include <input/keyboard/ps2.h>
//#include <apps/mountshell.h>
//#include <apps/bin/osfetch.h>

extern uint8_t __bss_start;
extern uint8_t __end;

void crash_me();

void timer(Registers* regs)
{
    printf(".");
}

void __attribute__((section(".entry"))) start(uint16_t bootDrive)
{
    memset(&__bss_start, 0, (&__end) - (&__bss_start));
    log_ok("Boot", "Did memset");

    HAL_Initialize();
    log_ok("Boot", "Initialized HAL");

    init_heap();
    log_ok("Boot", "Initialized Heap");

    init_fs();
    log_ok("Boot", "Initialized RamFS");

    log_ok("Kernel", "Initialized all imortant systems");

    log_info("Kernel", "Creating important files");

    create_dir("/sysbin");
    create_file("/sysbin/mount_shell");

    create_dir("/bin");
    create_file("/bin/osfetch");
    
    //void (*mss)() = &mountshell_start;
    //set_file_callback("/sysbin/mount_shell", mss);

    //void (*osf)() = &osfetch_start;
    //set_file_callback("/bin/osfetch", osf);

    printf("\n\nWelcome to \x1b[30;47mBlackmount\x1b[36;40m OS\n");

    drivers_init();

    char name[64];

    printf("Enter your name: ");
    scanf("%s", name);

    printf("\nHello, %s", name);

    //execute_file("/bin/osfetch");
    //execute_file("/sysbin/mount_shell");

    //i686_IRQ_RegisterHandler(0, timer);

    //crash_me();

end:
    for (;;);
}
