#include "irq.h"
#include "pic.h"
#include "i8259.h"
#include "io.h"
#include <stddef.h>
#include <util/arrays.h>
#include "stdio.h"
#include <debug.h>

#define PIC_REMAP_OFFSET        0x20
#define MODULE                  "PIC"

IRQHandler g_IRQHandlers[16] = {0};
static const PICDriver* g_Driver = NULL;

void x86_64_IRQ_Handler(Registers* regs)
{
    int irq = regs->interrupt - PIC_REMAP_OFFSET;
    
    // Bounds check
    if (irq < 0 || irq >= 16) {
        log_warn(MODULE, "Invalid IRQ number: %d", irq);
        return;
    }
    
    if (g_IRQHandlers[irq] != NULL) {
        g_IRQHandlers[irq](regs);
    } else {
        log_warn(MODULE, "Unhandled IRQ %d...", irq);
    }
    
    if (g_Driver != NULL) {
        g_Driver->SendEndOfInterrupt(irq);
    }
}

void x86_64_IRQ_Initialize()
{
    const PICDriver* drivers[] = {
        i8259_GetDriver(),
    };
    
    for (int i = 0; i < SIZE(drivers); i++) {
        if (drivers[i]->Probe()) {
            g_Driver = drivers[i];
        }
    }
    
    if (g_Driver == NULL) {
        log_warn(MODULE, "No PIC found!");
        return;
    }
    
    log_info(MODULE, "Found %s PIC.", g_Driver->Name);
    g_Driver->Initialize(PIC_REMAP_OFFSET, PIC_REMAP_OFFSET + 8, false);
    
    // Register ISR handlers
    for (int i = 0; i < 16; i++)
        x86_64_ISR_RegisterHandler(PIC_REMAP_OFFSET + i, x86_64_IRQ_Handler);
    
    // DON'T enable interrupts here - let drivers do it when ready
    // DON'T unmask IRQs here - let each driver unmask its own IRQ
}

void x86_64_IRQ_RegisterHandler(int irq, IRQHandler handler)
{
    g_IRQHandlers[irq] = handler;
}

void x86_64_IRQ_Unmask(int irq) {
    if (g_Driver != NULL) {
        g_Driver->Unmask(irq);
    }
}