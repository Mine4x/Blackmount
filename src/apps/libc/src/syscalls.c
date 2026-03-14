#include <syscalls.h>

void exit(void)
{
    __asm__ volatile (
        "syscall"
        :
        : "a"(SYSCALL_EXIT)
        : "rcx", "r11", "memory"
    );
}