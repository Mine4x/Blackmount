#include <stdint.h>
#include "stdio.h"
#include "memory.h"
#include <hal/hal.h>
#include <arch/i686/irq.h>
#include <debug.h>

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

    printf("Welcome to \x1b[30;47mBlackmount\x1b[36;40m OS\n");

    log_debug("Main", "This is a debug msg!");
    log_info("Main", "This is an info msg!");
    log_warn("Main", "This is a warnibng msg!");
    log_err("Main", "This is an error msg!");
    log_crit("Main", "This is a critical msg!");
    log_ok("Main", "This is a ok msg!");
    //i686_IRQ_RegisterHandler(0, timer);

    //crash_me();

end:
    for (;;);
}
