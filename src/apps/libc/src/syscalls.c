#include <syscalls.h>
#include <stdint.h>
#include <stddef.h>

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

void exit(int exit_code)
{
    syscall6(SYSCALL_EXIT, (uint64_t)exit_code, 0, 0, 0, 0, 0);
}

uint64_t write(uint64_t fd, const void* buf, size_t count) {
    return syscall6(
        SYSCALL_WRITE,     // syscall number
        fd,                // arg1 = fd
        (uint64_t)buf,     // arg2 = buffer pointer
        count,             // arg3 = number of bytes
        0,                 // arg4 = unused1
        0,                 // arg5 = unused2
        0                  // arg6 = unused3
    );
}