#include "scman.h"
#include "write.h"
#include <arch/x86_64/syscalls.h>

void register_syscalls() {
    syscall_register(1, sys_write);
}