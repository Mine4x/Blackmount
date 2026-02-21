#include "syscalls.h"
#include <stddef.h>
#include "gdt.h"

/*
 * MSR definitions for SYSCALL
 */
#define IA32_EFER   0xC0000080
#define IA32_STAR   0xC0000081
#define IA32_LSTAR  0xC0000082
#define IA32_FMASK  0xC0000084

#define EFER_SCE    (1ULL << 0)   // SYSCALL enable

#define KERNEL_CS x86_64_GDT_CODE_SEGMENT
#define USER_CS   (x86_64_GDT_USER_CODE_SEGMENT | 0x3)

/*
 * External ASM syscall entry
 */
extern void syscall_handler_asm(void);

/*
 * Syscall table
 */
static syscall_handler_t syscall_table[MAX_SYSCALLS] = {0};

/*
 * MSR helpers
 */
static inline void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFF);
    uint32_t hi = (uint32_t)(value >> 32);

    __asm__ volatile(
        "wrmsr"
        :
        : "c"(msr), "a"(lo), "d"(hi)
    );
}

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;

    __asm__ volatile(
        "rdmsr"
        : "=a"(lo), "=d"(hi)
        : "c"(msr)
    );

    return ((uint64_t)hi << 32) | lo;
}

void syscalls_init(void)
{
    for (uint64_t i = 0; i < MAX_SYSCALLS; i++)
        syscall_table[i] = NULL;

    uint64_t efer = rdmsr(IA32_EFER);
    efer |= EFER_SCE;
    wrmsr(IA32_EFER, efer);

    uint64_t star =
        ((uint64_t)USER_CS   << 48) |
        ((uint64_t)KERNEL_CS << 32);

    wrmsr(IA32_STAR, star);

    wrmsr(IA32_LSTAR, (uint64_t)syscall_handler_asm);

    wrmsr(IA32_FMASK, (1ULL << 9));
}

int syscall_register(uint64_t number, syscall_handler_t handler)
{
    if (number >= MAX_SYSCALLS)
        return -1;

    if (handler == NULL)
        return -1;

    syscall_table[number] = handler;
    return 0;
}

int syscall_unregister(uint64_t number)
{
    if (number >= MAX_SYSCALLS)
        return -1;

    syscall_table[number] = NULL;
    return 0;
}

int64_t syscall_dispatcher(
    uint64_t number,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4,
    uint64_t arg5
)
{
    if (number >= MAX_SYSCALLS)
        return -1;

    syscall_handler_t handler = syscall_table[number];

    if (!handler)
        return -1;

    return handler(arg1, arg2, arg3, arg4, arg5);
}