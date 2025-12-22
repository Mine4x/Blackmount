#include "hal.h"
#include <debug.h>
#include <arch/i686/gdt.h>
#include <arch/i686/idt.h>
#include <arch/i686/isr.h>
#include <arch/i686/irq.h>
#include <arch/i686/vga_text.h>

void HAL_Initialize()
{
    VGA_clrscr();
    i686_GDT_Initialize();
    log_ok("Boot/HAL", "Initialized GDT");
    i686_IDT_Initialize();
    log_ok("Boot/HAL", "Initialized IDT");
    i686_ISR_Initialize();
    log_ok("Boot/HAL", "Initialized ISR");
    i686_IRQ_Initialize();
    log_ok("Boot/HAL", "Initialized IRQ");
}
