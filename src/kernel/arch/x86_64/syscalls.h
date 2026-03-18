#pragma once
#include <stdint.h>

#define SYSCALL_MAX_COUNT 500

typedef uint64_t (*SyscallHandler)(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                                   uint64_t arg4, uint64_t arg5, uint64_t arg6);

void x86_64_Syscall_Initialize(void);

void x86_64_Syscall_SetKernelStack(uint64_t rsp);

void x86_64_Syscall_RegisterHandler(uint64_t number, SyscallHandler handler);