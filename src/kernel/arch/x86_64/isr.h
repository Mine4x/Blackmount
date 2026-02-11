#pragma once
#include <stdint.h>

typedef struct Registers {
    // Pushed by isr_common (top of stack first)
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;

    // Pushed by ISR macro
    uint64_t interrupt;
    uint64_t error;

    // Pushed by CPU
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} Registers;

typedef void (*ISRHandler)(Registers* regs);

void x86_64_ISR_Initialize();
void x86_64_ISR_RegisterHandler(int interrupt, ISRHandler handler);