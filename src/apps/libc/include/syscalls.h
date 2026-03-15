#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <stdint.h>
#include <stddef.h>

#define SYSCALL_READ 0
#define SYSCALL_WRITE 1
#define SYSCALL_EXIT 60
/*
 * Calls a syscall with 6 arguments
 */
uint64_t syscall6(
    uint64_t number,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4,
    uint64_t arg5,
    uint64_t arg6);

/*
 * Exits the current running task
 */
void exit(int exit_code);

/*
 * Writes something to a file and returns the count of bytes written
 */
uint64_t write(uint64_t fd, const void* buf, size_t count);

/*
 * Reads something from a file
 */
uint64_t read(uint64_t fd, void* buf, size_t count);

#endif