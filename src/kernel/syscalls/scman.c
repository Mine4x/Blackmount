#include "scman.h"
#include "sys_fs.h"
#include <arch/x86_64/syscalls.h>
#include <proc/proc.h>
#include <debug.h>

uint64_t test(uint64_t asd) {
    log_info("X", "%d", asd);

    return 20;
}

void register_syscalls() {
    x86_64_Syscall_RegisterHandler(0, sys_read);
    x86_64_Syscall_RegisterHandler(1, sys_write);
    x86_64_Syscall_RegisterHandler(2, (SyscallHandler)test);
    x86_64_Syscall_RegisterHandler(60, (SyscallHandler)proc_exit);
}