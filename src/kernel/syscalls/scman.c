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
    int pid = bin_load_elf((const char *)path, (uint32_t)priority, proc_get_current_pid());
    x86_64_EnableInterrupts();
    return pid;
}

/* syscall 59 - execve(const char *path, const char *const argv[], const char *const envp[]) */
uint64_t sys_execve(uint64_t path, uint64_t argv, uint64_t envp)
{
    const char  *prog = (const char *)path;
    const char **av   = (const char **)argv;
    const char **ev   = (const char **)envp;

    int argc = 0;
    int envc = 0;

    if (av)
        while (av[argc]) argc++;

    if (ev)
        while (ev[envc]) envc++;

    x86_64_DisableInterrupts();
    int pid = bin_load_elf_argv(prog, 1, proc_get_current_pid(),
                                argc, av, envc, ev);
    x86_64_EnableInterrupts();
    return (uint64_t)pid;
}

void register_syscalls()
{
    x86_64_Syscall_RegisterHandler(0,   sys_read);
    x86_64_Syscall_RegisterHandler(1,   sys_write);
    x86_64_Syscall_RegisterHandler(2,   (SyscallHandler)sys_open);
    x86_64_Syscall_RegisterHandler(3,   (SyscallHandler)sys_close);
    x86_64_Syscall_RegisterHandler(12,  (SyscallHandler)proc_brk);
    x86_64_Syscall_RegisterHandler(16,  (SyscallHandler)sys_ioctl);
    x86_64_Syscall_RegisterHandler(24,  (SyscallHandler)proc_yield);
    x86_64_Syscall_RegisterHandler(59,  (SyscallHandler)sys_execve);
    x86_64_Syscall_RegisterHandler(60,  (SyscallHandler)proc_exit);
    x86_64_Syscall_RegisterHandler(217, (SyscallHandler)sys_getdents64);
    x86_64_Syscall_RegisterHandler(301, (SyscallHandler)load_bin);
    x86_64_Syscall_RegisterHandler(302, (SyscallHandler)proc_wait_pid);
}