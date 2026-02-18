#include "isr.h"
#include "idt.h"
#include "gdt.h"
#include "io.h"
#include <panic/panic.h>
#include <stdio.h>
#include <stddef.h>
#include <debug.h>

#define MODULE          "ISR"

ISRHandler g_ISRHandlers[256];

static const char* const g_Exceptions[] = {
    "Divide by zero error",
    "Debug",
    "Non-maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception ",
    "",
    "",
    "",
    "",
    "",
    "",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    ""
};

void x86_64_ISR_InitializeGates();

void x86_64_ISR_Initialize()
{
    x86_64_ISR_InitializeGates();
    for (int i = 0; i < 256; i++)
        x86_64_IDT_EnableGate(i);
    x86_64_IDT_DisableGate(0x80);
}

void x86_64_ISR_Handler(Registers* regs)
{
    if (g_ISRHandlers[regs->interrupt] != NULL)
        g_ISRHandlers[regs->interrupt](regs);
    else if (regs->interrupt >= 32)
        log_err(MODULE, "Unhandled interrupt %d!", regs->interrupt);
    else 
    {
        log_crit(MODULE, "Unhandled exception %d %s", regs->interrupt, g_Exceptions[regs->interrupt]);
        log_crit(MODULE, "  rax=%016lx  rbx=%016lx  rcx=%016lx  rdx=%016lx",
                 regs->rax, regs->rbx, regs->rcx, regs->rdx);
        log_crit(MODULE, "  rsi=%016lx  rdi=%016lx  rbp=%016lx  rsp=%016lx",
                 regs->rsi, regs->rdi, regs->rbp, regs->rsp);
        log_crit(MODULE, "  r8=%016lx   r9=%016lx   r10=%016lx  r11=%016lx",
                 regs->r8, regs->r9, regs->r10, regs->r11);
        log_crit(MODULE, "  r12=%016lx  r13=%016lx  r14=%016lx  r15=%016lx",
                 regs->r12, regs->r13, regs->r14, regs->r15);
        log_crit(MODULE, "  rip=%016lx  rflags=%016lx",
                 regs->rip, regs->rflags);
        log_crit(MODULE,
    "  cs=%04lx  ss=%04lx  rflags=%016lx",
     regs->cs, regs->ss, regs->rflags);

        log_crit(MODULE, "  interrupt=%x  errorcode=%lx", regs->interrupt, regs->error);
        log_crit(MODULE, "KERNEL PANIC!");

        panic("ISR", "Unhandled exception\nIf you are running on qemu check the output for more information.");
    }
}

void x86_64_ISR_RegisterHandler(int interrupt, ISRHandler handler)
{
    g_ISRHandlers[interrupt] = handler;
    x86_64_IDT_EnableGate(interrupt);
}