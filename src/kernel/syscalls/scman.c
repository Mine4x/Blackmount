#include "scman.h"
#include "write.h"
#include <arch/i686/syscalls.h>

void register_syscalls() {
    syscall_register(4, sys_write);
}