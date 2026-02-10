#include "syscalls.h"
#include "idt.h"

static syscall_handler_t syscall_table[MAX_SYSCALLS] = {0};

extern void syscall_handler_asm(void);

void syscalls_init(void) {
    for (int i = 0; i < MAX_SYSCALLS; i++) {
        syscall_table[i] = 0;
    }
    
    i686_IDT_SetGate(0x80, syscall_handler_asm, 0x08, 0xEE);
}

int syscall_register(uint32_t number, syscall_handler_t handler) {
    if (number >= MAX_SYSCALLS) {
        return -1;
    }
    
    if (handler == 0) {
        return -1;
    }
    
    syscall_table[number] = handler;
    return 0;
}

int syscall_unregister(uint32_t number) {
    if (number >= MAX_SYSCALLS) {
        return -1;
    }
    
    syscall_table[number] = 0;
    return 0;
}

int32_t syscall_dispatcher(uint32_t eax, uint32_t ebx, uint32_t ecx, 
                           uint32_t edx, uint32_t esi, uint32_t edi) {
    if (eax >= MAX_SYSCALLS) {
        return -1;
    }

    if (syscall_table[eax] == 0) {
        return -1;
    }
    
    return syscall_table[eax](ebx, ecx, edx, esi, edi);
}