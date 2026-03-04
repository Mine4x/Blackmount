#pragma once
#include "isr.h"
#include <stdint.h>

// ISA IRQs 0-15 map to vectors 0x20-0x2F (same as before)
// Extra GSIs 16-255 map to vectors 0x30-0x12F
#define IRQ_VECTOR_BASE     0x20
#define IRQ_MAX             256

typedef void (*IRQHandler)(Registers* regs);

// ── Existing API — no changes needed in any driver ───────────────────────────
void x86_64_IRQ_Initialize();
void x86_64_IRQ_RegisterHandler(int irq, IRQHandler handler);
void x86_64_IRQ_Unmask(int irq);

// ── New: for non-ISA GSIs (PCI etc.) ─────────────────────────────────────────
// gsi      — global system interrupt number (from ACPI)
// vector   — IDT vector you want it delivered on (>= 0x30 recommended)
// handler  — your handler
void x86_64_IRQ_RegisterGSI(uint32_t gsi, uint8_t vector, IRQHandler handler);
void x86_64_IRQ_MaskGSI(uint32_t gsi);
void x86_64_IRQ_UnmaskGSI(uint32_t gsi);

// ── EOI — call at end of any LAPIC-sourced handler ───────────────────────────
void x86_64_IRQ_EOI(void);