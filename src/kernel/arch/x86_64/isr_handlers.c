#include "isr.h"
#include <proc/proc.h>
#include <debug.h>
#include <panic/panic.h>

#define MODULE "Exception-Handler"

static void dump(Registers *regs, const char *msg)
{
    int user = (regs->cs & 0x3) == 3;

    log_crit(MODULE, "%s", msg);
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
    log_crit(MODULE, "  cs=%04lx  ss=%04lx  rflags=%016lx", regs->cs, regs->ss, regs->rflags);
    log_crit(MODULE, "  interrupt=%x  errorcode=%lx", regs->interrupt, regs->error);

    if (user)
    {
        printf("%s\n", msg);
        proc_exit(-1);
        return;
    }

    panic("Exception in kernel mode", msg);
}

void divide_error(Registers *regs) { dump(regs, "Divide by 0 exception"); }
void breakpoint(Registers *regs) { dump(regs, "Breakpoint exception"); }
void overflow(Registers *regs) { dump(regs, "Overflow exception"); }
void bound_range(Registers *regs) { dump(regs, "Bound range exceeded"); }
void invalid_opcode(Registers *regs) { dump(regs, "Invalid opcode"); }
void device_not_available(Registers *regs) { dump(regs, "Device not available"); }
void invalid_tss(Registers *regs) { dump(regs, "Invalid TSS"); }
void segment_not_present(Registers *regs) { dump(regs, "Segment not present"); }
void stack_segment_fault(Registers *regs) { dump(regs, "Stack segment fault"); }
void general_protection(Registers *regs) { dump(regs, "General protection fault"); }
void x87_fpu(Registers *regs) { dump(regs, "x87 floating point exception"); }
void alignment_check(Registers *regs) { dump(regs, "Alignment check"); }
void simd_exception(Registers *regs) { dump(regs, "SIMD floating point exception"); }

void register_exception_handlers(void)
{
    x86_64_ISR_RegisterHandler(0, divide_error);
    x86_64_ISR_RegisterHandler(3, breakpoint);
    x86_64_ISR_RegisterHandler(4, overflow);
    x86_64_ISR_RegisterHandler(5, bound_range);
    x86_64_ISR_RegisterHandler(6, invalid_opcode);
    x86_64_ISR_RegisterHandler(7, device_not_available);
    x86_64_ISR_RegisterHandler(10, invalid_tss);
    x86_64_ISR_RegisterHandler(11, segment_not_present);
    x86_64_ISR_RegisterHandler(12, stack_segment_fault);
    x86_64_ISR_RegisterHandler(13, general_protection);
    x86_64_ISR_RegisterHandler(16, x87_fpu);
    x86_64_ISR_RegisterHandler(17, alignment_check);
    x86_64_ISR_RegisterHandler(19, simd_exception);
}