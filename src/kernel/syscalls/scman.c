#include "scman.h"
#include "write.h"
#include <arch/x86_64/syscalls.h>
#include <proc/proc.h>

void register_syscalls() {
    syscall_register(1, sys_write);
    syscall_register(60, (syscall_handler_t)proc_exit);
}