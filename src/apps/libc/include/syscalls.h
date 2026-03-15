#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <stdint.h>

#define SYSCALL_EXIT 60
#define SYSCALL_WRITE 1

uint64_t syscall6(
    uint64_t number,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4,
    uint64_t arg5,
    uint64_t arg6);

/*
    Exits the current running task
*/
void exit(void);

#endif