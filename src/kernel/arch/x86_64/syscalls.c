#include "syscalls.h"
#include <stddef.h>
#include <stdint.h>
#include <proc/proc.h>
#include "io.h"

#define MSR_EFER    0xC0000080u   /* Extended Feature Enable Register      */
#define MSR_STAR    0xC0000081u   /* Syscall Target Address (segment bases) */
#define MSR_LSTAR   0xC0000082u   /* Long-mode Syscall Target RIP           */
#define MSR_FMASK   0xC0000084u   /* Syscall RFLAGS mask                    */

#define EFER_SCE    (UINT64_C(1) << 0)

static SyscallHandler g_SyscallHandlers[SYSCALL_MAX_COUNT];

uint64_t x86_64_Syscall_KernelStack = 0;

static inline void wrmsr(uint32_t msr, uint64_t value)
{
    __asm__ volatile (
        "wrmsr"
        :: "c"(msr),
           "a"((uint32_t)(value)),
           "d"((uint32_t)(value >> 32))
    );
}

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

extern void x86_64_Syscall_Entry(void);

void x86_64_Syscall_Initialize(void)
{
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);

    wrmsr(MSR_STAR,
          ((uint64_t)0x10u << 48) |
          ((uint64_t)0x08u << 32));

    wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)x86_64_Syscall_Entry);

    wrmsr(MSR_FMASK, 0x200u);
}

void x86_64_Syscall_SetKernelStack(uint64_t rsp)
{
    x86_64_Syscall_KernelStack = rsp;
}

void x86_64_Syscall_RegisterHandler(uint64_t number, SyscallHandler handler)
{
    if (number < SYSCALL_MAX_COUNT)
        g_SyscallHandlers[number] = handler;
}

uint64_t x86_64_Syscall_Dispatch(uint64_t number,
                                  uint64_t arg1, uint64_t arg2, uint64_t arg3,
                                  uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    if (number < SYSCALL_MAX_COUNT && g_SyscallHandlers[number] != NULL)
    {
        proc_enter_syscall();
        uint64_t r = g_SyscallHandlers[number](arg1, arg2, arg3, arg4, arg5, arg6);
        proc_exit_syscall();
        return r;
    }
    
    return (uint64_t)-1;
}