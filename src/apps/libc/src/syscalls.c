#include <syscalls.h>
#include <stdint.h>

uint64_t syscall6(
    uint64_t number,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4,
    uint64_t arg5,
    uint64_t arg6)
{
    uint64_t ret;

    __asm__ volatile (
        "mov %5, %%r10\n"
        "syscall"
        : "=a"(ret)
        : "a"(number),
          "D"(arg1),
          "S"(arg2),
          "d"(arg3),
          "r"(arg4),
          "r"(arg5),
          "r"(arg6)
        : "rcx", "r11", "memory"
    );

    return ret;
}

void exit(void)
{
    __asm__ volatile (
        "syscall"
        :
        : "a"(SYSCALL_EXIT)
        : "rcx", "r11", "memory"
    );
}