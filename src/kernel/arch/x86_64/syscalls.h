#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <stdint.h>

#define MAX_SYSCALLS 256

// 64-bit syscall handler signature
typedef int64_t (*syscall_handler_t)(
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4,
    uint64_t arg5
);

void syscalls_init(void);

int syscall_register(uint64_t number, syscall_handler_t handler);
int syscall_unregister(uint64_t number);

// Dispatcher called from ASM syscall handler
int64_t syscall_dispatcher(
    uint64_t rax,
    uint64_t rbx,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t rsi,
    uint64_t rdi
);

#endif // SYSCALLS_H
