#pragma once
#include <stdint.h>
#include <stdbool.h>

void x86_64_outb(uint16_t port, uint8_t value);
uint8_t x86_64_inb(uint16_t port);
void x86_64_outw(uint16_t port, uint16_t value);
uint16_t x86_64_inw(uint16_t port);
void x86_64_outl(uint16_t port, uint32_t value);
uint32_t x86_64_inl(uint16_t port);

void x86_64_EnableInterrupts();
void x86_64_DisableInterrupts();

void x86_64_iowait();
void x86_64_Panic();