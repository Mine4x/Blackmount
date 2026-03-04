#pragma once
#include "isr.h"
#include <stdint.h>

#define IRQ_VECTOR_BASE     0x20
#define IRQ_MAX             256

typedef void (*IRQHandler)(Registers* regs);

uint8_t get_next_vector(void);

void x86_64_IRQ_Initialize();
void x86_64_IRQ_RegisterHandler(int irq, IRQHandler handler);
void x86_64_IRQ_Unmask(int irq);

void x86_64_IRQ_RegisterGSI(uint32_t gsi, uint8_t vector, IRQHandler handler);
void x86_64_IRQ_MaskGSI(uint32_t gsi);
void x86_64_IRQ_UnmaskGSI(uint32_t gsi);

void x86_64_IRQ_EOI(void);

uint8_t x86_64_IRQ_AllocVector(void);

void x86_64_IRQ_RegisterVector(uint8_t vector, IRQHandler handler);