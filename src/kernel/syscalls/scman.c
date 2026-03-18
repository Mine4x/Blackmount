#include "scman.h"
#include "sys_fs.h"
#include <arch/x86_64/syscalls.h>
#include <proc/proc.h>
#include <debug.h>
#include <loaders/bin_loader.h>

/*
 * 0-300 is reserved for syscalls that follow the linux interface
 * the rest for other syscalls
 */

uint64_t load_bin(uint64_t path, uint64_t priority)
{
    x86_64_DisableInterrupts();
    int pid = bin_load_elf((const char*)path, (uint32_t)priority, proc_get_current_pid());
    x86_64_EnableInterrupts();
    return pid;
}

void register_syscalls() {
    x86_64_Syscall_RegisterHandler(0, sys_read);
    x86_64_Syscall_RegisterHandler(1, sys_write);
    x86_64_Syscall_RegisterHandler(2, (SyscallHandler)sys_open);
    x86_64_Syscall_RegisterHandler(3, (SyscallHandler)sys_close);
    x86_64_Syscall_RegisterHandler(24, (SyscallHandler)proc_yield);
    x86_64_Syscall_RegisterHandler(60, (SyscallHandler)proc_exit);
    x86_64_Syscall_RegisterHandler(302, (SyscallHandler)proc_wait_pid);

    x86_64_Syscall_RegisterHandler(301, (SyscallHandler)load_bin);
}