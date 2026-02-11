#include "hal.h"
#include <debug.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/irq.h>

void HAL_Initialize()
{
    x86_64_GDT_Initialize();
    log_ok("Boot/HAL", "Initialized GDT");
    x86_64_IDT_Initialize();
    log_ok("Boot/HAL", "Initialized IDT");
    x86_64_ISR_Initialize();
    log_ok("Boot/HAL", "Initialized ISR");
    x86_64_IRQ_Initialize();
    log_ok("Boot/HAL", "Initialized IRQ");
}
