#include "pagefault.h"
#include "isr.h"
#include <debug.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <panic/panic.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <proc/proc.h>

#define MODULE    "PAGE FAULT"
#define MODULE_DF "DOUBLE FAULT"

static inline uint64_t read_cr2(void) {
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

static bool try_demand_page(uint64_t faulting_address, uint64_t err)
{
    int present = err & 0x1;
    int user    = err & 0x4;

    if (present || !user)
        return false;

    if (!proc_is_valid_demand_addr(faulting_address))
        return false;

    uint64_t page_vaddr = faulting_address & ~((uint64_t)PAGE_SIZE - 1);

    address_space_t* kspace = vmm_get_kernel_space();
    void* mapped = vmm_alloc_page(kspace, (void*)page_vaddr, VMM_USER_PAGE);
    if (!mapped) {
        log_err(MODULE, "OOM: cannot satisfy demand page at 0x%lx", faulting_address);
        return false;
    }

    memset((void*)page_vaddr, 0, PAGE_SIZE);

    log_debug(MODULE, "Demand page mapped: virt=0x%lx", page_vaddr);
    return true;
}

void x86_64_PageFault_Handler(Registers* regs)
{
    uint64_t faulting_address = read_cr2();
    uint64_t err = regs->error;

    if (try_demand_page(faulting_address, err))
        return;

    int present  = err & 0x1;
    int write    = err & 0x2;
    int user     = err & 0x4;
    int reserved = err & 0x8;
    int ifetch   = err & 0x10;

    log_crit(MODULE, "Page fault at RIP=0x%016lx", regs->rip);
    log_crit(MODULE, "Faulting address: 0x%016lx", faulting_address);
    log_err(MODULE, "Cause: %s %s in %s%s%s",
            present  ? "Protection violation" : "Page not present",
            write    ? "write" : "read",
            user     ? "user mode" : "kernel mode",
            reserved ? " (reserved bits set)" : "",
            ifetch   ? " during instruction fetch" : "");
    log_debug(MODULE, "Register dump:");
    log_debug(MODULE, "  RAX=0x%016lx  RBX=0x%016lx  RCX=0x%016lx  RDX=0x%016lx",
              regs->rax, regs->rbx, regs->rcx, regs->rdx);
    log_debug(MODULE, "  RSI=0x%016lx  RDI=0x%016lx  RBP=0x%016lx  RSP=0x%016lx",
              regs->rsi, regs->rdi, regs->rbp, regs->rsp);
    log_debug(MODULE, "  R8 =0x%016lx  R9 =0x%016lx  R10=0x%016lx  R11=0x%016lx",
              regs->r8, regs->r9, regs->r10, regs->r11);
    log_debug(MODULE, "  R12=0x%016lx  R13=0x%016lx  R14=0x%016lx  R15=0x%016lx",
              regs->r12, regs->r13, regs->r14, regs->r15);
    log_debug(MODULE, "  RIP=0x%016lx  RFLAGS=0x%016lx", regs->rip, regs->rflags);
    log_debug(MODULE, "  CS=0x%04lx  SS=0x%04lx", regs->cs, regs->ss);
    log_debug(MODULE, "  Error code: 0x%lx", err);
    log_crit(MODULE, "Cannot recover - halting system");
    
    if (user)
    {
        printf("Segmentation fault(CPU dumped)\n");
        proc_exit(-1);
        return;
    }

    panic("Pagefault exception",
          "Pagefault triggered\nIf you are running on qemu check the output for more information.");
}

void x86_64_DoubleFault_Handler(Registers* regs) {
    log_crit(MODULE_DF, "Double fault exception!");
    log_crit(MODULE_DF, "This is a critical system error");
    log_err(MODULE_DF, "Error code: 0x%lx (always 0)", regs->error);
    log_err(MODULE_DF, "RIP: 0x%016lx", regs->rip);
    log_err(MODULE_DF, "RSP: 0x%016lx  RBP: 0x%016lx", regs->rsp, regs->rbp);
    log_err(MODULE_DF, "CS: 0x%04lx  SS: 0x%04lx", regs->cs, regs->ss);
    log_warn(MODULE_DF, "Common causes:");
    log_warn(MODULE_DF, "  - Stack overflow");
    log_warn(MODULE_DF, "  - Invalid stack segment");
    log_warn(MODULE_DF, "  - Exception handler caused another exception");
    log_warn(MODULE_DF, "  - Invalid TSS");
    log_warn(MODULE_DF, "  - IST stack corruption");
    log_crit(MODULE_DF, "System halted - cannot recover");
    panic("Doublefault exception",
          "Doublefault triggered\nIf you are running on qemu check the output for more information.");
}

void x86_64_PageFault_Initialize(void) {
    x86_64_ISR_RegisterHandler(14, x86_64_PageFault_Handler);
    x86_64_ISR_RegisterHandler(8,  x86_64_DoubleFault_Handler);
    log_ok("PAGING", "Page fault and double fault handlers installed");
}