#include "hal.h"
#include <debug.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/irq.h>
#include <drivers/acpi/acpi.h>
#include <timer/timer.h>

void HAL_Initialize()
{
    x86_64_GDT_Initialize();
    log_ok("Boot/HAL", "Initialized GDT");

    x86_64_IDT_Initialize();
    log_ok("Boot/HAL", "Initialized IDT");

    x86_64_ISR_Initialize();
    log_ok("Boot/HAL", "Initialized ISR");

    acpi_init();
    log_ok("Boot/HAL", "Initialized ACPI");

    x86_64_IRQ_Initialize();
    log_ok("Boot/HAL", "Initialized IRQ (IOAPIC+LAPIC)");

    x86_64_PageFault_Initialize();
    log_ok("Boot/HAL", "Initialized Pagefault handler");
}