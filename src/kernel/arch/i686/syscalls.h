#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <stdint.h>

#define MAX_SYSCALLS    256

typedef int32_t (*syscall_handler_t)(uint32_t arg1, uint32_t arg2, uint32_t arg3, 
                                     uint32_t arg4, uint32_t arg5);

void syscalls_init(void);

int syscall_register(uint32_t number, syscall_handler_t handler);

int syscall_unregister(uint32_t number);

int32_t syscall_dispatcher(uint32_t eax, uint32_t ebx, uint32_t ecx, 
                           uint32_t edx, uint32_t esi, uint32_t edi);

#endif // SYSCALLS_H