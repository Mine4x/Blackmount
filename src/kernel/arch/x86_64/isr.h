#pragma once
#include <stdint.h>

typedef struct 
{
    // In the reverse order they are pushed:
    
    // Segment registers (pushed last by us, stored as 64-bit values)
    uint64_t gs, fs, es, ds;
    
    // General purpose registers (pushed by us)
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    
    // Interrupt info (pushed by ISR macro)
    uint64_t interrupt, error;
    
    // Pushed automatically by CPU
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) Registers;

typedef void (*ISRHandler)(Registers* regs);

void x86_64_ISR_Initialize();
void x86_64_ISR_RegisterHandler(int interrupt, ISRHandler handler);