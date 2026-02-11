#include "syscalls.h"
#include "idt.h"
#include <stdint.h>

static syscall_handler_t syscall_table[MAX_SYSCALLS] = {0};

extern void syscall_handler_asm(void);

void syscalls_init(void) {
    for (uint64_t i = 0; i < MAX_SYSCALLS; i++) {
        syscall_table[i] = 0;
    }

    // int 0x80, DPL=3, present, interrupt gate
    x86_64_IDT_SetGate(0x80, syscall_handler_asm, 0x08, 0xEE);
}

int syscall_register(uint64_t number, syscall_handler_t handler) {
    if (number >= MAX_SYSCALLS) {
        return -1;
    }

    if (!handler) {
        return -1;
    }

    syscall_table[number] = handler;
    return 0;
}

int syscall_unregister(uint64_t number) {
    if (number >= MAX_SYSCALLS) {
        return -1;
    }

    syscall_table[number] = 0;
    return 0;
}

int64_t syscall_dispatcher(
    uint64_t rax,
    uint64_t rbx,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t rsi,
    uint64_t rdi
) {
    if (rax >= MAX_SYSCALLS) {
        return -1;
    }

    if (!syscall_table[rax]) {
        return -1;
    }

    return syscall_table[rax](rbx, rcx, rdx, rsi, rdi);
}
