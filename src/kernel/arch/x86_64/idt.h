#pragma once
#include <stdint.h>

typedef enum
{
    // Task gates and 16-bit gates are NOT supported in x86_64 long mode
    IDT_FLAG_GATE_INTERRUPT         = 0xE,  // 64-bit Interrupt Gate
    IDT_FLAG_GATE_TRAP              = 0xF,  // 64-bit Trap Gate
    
    IDT_FLAG_RING0                  = (0 << 5),
    IDT_FLAG_RING1                  = (1 << 5),
    IDT_FLAG_RING2                  = (2 << 5),
    IDT_FLAG_RING3                  = (3 << 5),
    
    IDT_FLAG_PRESENT                = 0x80,
} IDT_FLAGS;

void x86_64_IDT_Initialize();
void x86_64_IDT_DisableGate(int interrupt);
void x86_64_IDT_EnableGate(int interrupt);
void x86_64_IDT_SetGate(int interrupt, void* base, uint16_t segmentDescriptor, uint8_t flags);
void x86_64_IDT_SetGateWithIST(int interrupt, void* base, uint16_t segmentDescriptor, uint8_t flags, uint8_t ist);