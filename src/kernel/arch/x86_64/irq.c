#include "irq.h"
#include "isr.h"
#include "io.h"
#include <drivers/apic/ioapic.h>
#include <drivers/apic/lapic.h>
#include <debug.h>
#include <stddef.h>

#define MODULE "IRQ"

// Expanded handler table — indexed by IDT vector
static IRQHandler g_IRQHandlers[IRQ_MAX] = {0};


void x86_64_IRQ_Handler(Registers* regs) {
    int vector = (int)regs->interrupt;

    if (vector < IRQ_VECTOR_BASE || vector >= IRQ_MAX) {
        log_warn(MODULE, "IRQ_Handler: vector 0x%x out of range", vector);
        lapic_eoi();
        return;
    }

    IRQHandler handler = g_IRQHandlers[vector];
    if (handler != NULL) {
        handler(regs);
    } else {
        log_warn(MODULE, "Unhandled vector 0x%x (IRQ %d)",
                 vector, vector - IRQ_VECTOR_BASE);
    }

    lapic_eoi();
}

void x86_64_IRQ_Initialize() {
    // Register our central dispatch for all vectors in our range
    for (int v = IRQ_VECTOR_BASE; v < IRQ_MAX; v++)
        x86_64_ISR_RegisterHandler(v, x86_64_IRQ_Handler);

    // Bring up LAPIC and IOAPIC (IOAPIC masks everything on init,
    // PIC is disabled inside ioapic_init → pic_disable())
    lapic_init();
    ioapic_init();

    log_ok(MODULE, "IRQ subsystem ready — IOAPIC+LAPIC backend, "
                   "%d vectors available", IRQ_MAX - IRQ_VECTOR_BASE);
}

// Register a handler for ISA IRQ 0-15 (same as before)
void x86_64_IRQ_RegisterHandler(int irq, IRQHandler handler) {
    if (irq < 0 || irq >= 16) {
        log_warn(MODULE, "RegisterHandler: IRQ %d out of ISA range", irq);
        return;
    }
    g_IRQHandlers[IRQ_VECTOR_BASE + irq] = handler;
}

// Unmask an ISA IRQ — routes it through IOAPIC for the first time,
// or just unmasks it if already routed.
void x86_64_IRQ_Unmask(int irq) {
    if (irq < 0 || irq >= 16) {
        log_warn(MODULE, "Unmask: IRQ %d out of ISA range", irq);
        return;
    }
    uint8_t vector = (uint8_t)(IRQ_VECTOR_BASE + irq);
    // ioapic_route_isa_irq applies MADT overrides automatically
    ioapic_route_isa_irq((uint8_t)irq, vector, lapic_id());
}

void x86_64_IRQ_RegisterGSI(uint32_t gsi, uint8_t vector, IRQHandler handler) {
    if (vector < IRQ_VECTOR_BASE || vector >= IRQ_MAX) {
        log_warn(MODULE, "RegisterGSI: vector 0x%x out of range", vector);
        return;
    }
    g_IRQHandlers[vector] = handler;
    // Route immediately — PCI is edge-triggered active-low by default,
    // adjust flags if your device needs level-triggered
    ioapic_route(gsi, vector, IOAPIC_DELIV_FIXED,
                 /*active_low=*/1, /*level_triggered=*/0, lapic_id());
    log_info(MODULE, "GSI %u → vector 0x%x registered", gsi, vector);
}

void x86_64_IRQ_MaskGSI(uint32_t gsi) {
    ioapic_mask_gsi(gsi);
}

void x86_64_IRQ_UnmaskGSI(uint32_t gsi) {
    ioapic_unmask_gsi(gsi);
}

void x86_64_IRQ_EOI(void) {
    lapic_eoi();
}

#define VECTOR_ALLOC_BASE   0x30
#define VECTOR_ALLOC_MAX    0xEF   // leave 0xEF-0xFF for LAPIC timer/spurious/error

static uint8_t g_next_vector = VECTOR_ALLOC_BASE;

uint8_t x86_64_IRQ_AllocVector(void) {
    if (g_next_vector >= VECTOR_ALLOC_MAX) {
        log_err(MODULE, "AllocVector: vector space exhausted");
        return 0;
    }
    return g_next_vector++;
}

void x86_64_IRQ_RegisterVector(uint8_t vector, IRQHandler handler) {
    if (vector < VECTOR_ALLOC_BASE || vector >= IRQ_MAX) {
        log_warn(MODULE, "RegisterVector: vector 0x%x out of range", vector);
        return;
    }
    g_IRQHandlers[vector] = handler;
}

uint8_t get_next_vector(void) {
    return g_next_vector;
}