#include "pagefault.h"
#include "isr.h"
#include <debug.h>
#include <stdint.h>
#include <stdio.h>

#define MODULE "PAGE FAULT"
#define MODULE_DF "DOUBLE FAULT"

static inline uint32_t read_cr2(void) {
    uint32_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

void i686_PageFault_Handler(Registers* regs) {
    uint32_t faulting_address = read_cr2();
    uint32_t err = regs->error;
    
    // Parse error code bits
    int present = err & 0x1;    // 0 = not present, 1 = protection fault
    int write = err & 0x2;      // 0 = read, 1 = write
    int user = err & 0x4;       // 0 = kernel, 1 = user mode
    int reserved = err & 0x8;   // 1 = reserved bits set
    int ifetch = err & 0x10;    // 1 = instruction fetch
    
    log_crit(MODULE, "Page fault at EIP=%x", regs->eip);
    log_crit(MODULE, "Faulting address: 0x%x", faulting_address);
    
    // Build detailed error message
    log_err(MODULE, "Cause: %s %s in %s%s%s",
            present ? "Protection violation" : "Page not present",
            write ? "write" : "read",
            user ? "user mode" : "kernel mode",
            reserved ? " (reserved bits set)" : "",
            ifetch ? " during instruction fetch" : "");
    
    // Print register state
    log_debug(MODULE, "Register dump:");
    log_debug(MODULE, "  EAX=0x%08x  EBX=0x%08x  ECX=0x%08x  EDX=0x%08x", 
              regs->eax, regs->ebx, regs->ecx, regs->edx);
    log_debug(MODULE, "  ESI=0x%08x  EDI=0x%08x  EBP=0x%08x  ESP=0x%08x",
              regs->esi, regs->edi, regs->ebp, regs->esp);
    log_debug(MODULE, "  EIP=0x%08x  EFLAGS=0x%08x", regs->eip, regs->eflags);
    log_debug(MODULE, "  CS=0x%04x  DS=0x%04x  SS=0x%04x", 
              regs->cs, regs->ds, regs->ss);
    log_debug(MODULE, "  Error code: 0x%x", err);
    
    log_crit(MODULE, "Cannot recover - halting system");
    printf("\nKERNEL PANIC: Page Fault at 0x%x\n", faulting_address);
    
    // Halt the system
    __asm__ volatile("cli");
    for(;;) {
        __asm__ volatile("hlt");
    }
}

void i686_DoubleFault_Handler(Registers* regs) {
    log_crit(MODULE_DF, "Double fault exception!");
    log_crit(MODULE_DF, "This is a critical system error");
    
    log_err(MODULE_DF, "Error code: 0x%x (always 0)", regs->error);
    log_err(MODULE_DF, "EIP: 0x%08x", regs->eip);
    log_err(MODULE_DF, "ESP: 0x%08x  EBP: 0x%08x", regs->esp, regs->ebp);
    log_err(MODULE_DF, "CS: 0x%04x  SS: 0x%04x", regs->cs, regs->ss);
    
    log_warn(MODULE_DF, "Common causes:");
    log_warn(MODULE_DF, "  - Stack overflow");
    log_warn(MODULE_DF, "  - Invalid stack segment");
    log_warn(MODULE_DF, "  - Exception handler caused another exception");
    log_warn(MODULE_DF, "  - Invalid TSS");
    
    log_crit(MODULE_DF, "System halted - cannot recover");
    printf("\nKERNEL PANIC: Double Fault\n");
    
    // Double faults are unrecoverable
    __asm__ volatile("cli");
    for(;;) {
        __asm__ volatile("hlt");
    }
}

void i686_PageFault_Initialize(void) {
    // Register page fault handler (interrupt 14)
    i686_ISR_RegisterHandler(14, i686_PageFault_Handler);
    
    // Register double fault handler (interrupt 8)
    i686_ISR_RegisterHandler(8, i686_DoubleFault_Handler);
    
    log_ok("PAGING", "Page fault and double fault handlers installed");
}