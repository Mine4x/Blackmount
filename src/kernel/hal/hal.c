#include "hal.h"
#include <debug.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/irq.h>
#include <drivers/acpi/acpi.h>
#include <timer/timer.h>

void cpu_enable_sse(void)
{
    uint64_t cr0, cr4;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);   // clear CR0.EM (FPU emulation)
    cr0 |=  (1ULL << 1);   // set   CR0.MP (monitor coprocessor)
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);    // set CR4.OSFXSR    (enable FXSAVE/FXRSTOR + SSE)
    cr4 |= (1ULL << 10);   // set CR4.OSXMMEXCPT (enable SSE exception handling)
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));
}

void HAL_Initialize()
{
    cpu_enable_sse();
    log_ok("Boot/HAL", "Initialized SSE");

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