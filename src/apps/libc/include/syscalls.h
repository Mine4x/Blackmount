#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <stdint.h>
#include <stddef.h>

#define SYSCALL_READ 0
#define SYSCALL_WRITE 1
#define SYSCALL_OPEN 2
#define SYSCALL_CLOSE 3
#define SYSCALL_BRK 12
#define SYSCALL_IOCTL 16
#define SYSCALL_EXECVE 59
#define SYSCALL_EXIT 60
#define SYSCALL_BINRUN 301
#define SYSCALL_WAIT 302

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
 * Reads something from a file into a buffer and returns the bytes read
 */
uint64_t read(uint64_t fd, void* buf, size_t count);

/*
 * Opens a file and returns the FD
 */
uint64_t open(const char* path);

/*
 * Closes a open file
 */
uint64_t close(uint64_t fd);

/*
 * Wait for a child process to exit
 */
uint64_t waitpid(uint64_t pid);

/*
 * Executes a binary as a child process and returns the pid
 */
uint64_t binrun(const char* path);

uint64_t brk(uint64_t addr);

/*
 * Communicates with devices
 */
uint64_t ioctl(int fd, uint64_t req, void *arg);

uint64_t execve(const char *path, const char **argv, const char **envp);

uint64_t execv(const char *path, const char **argv);

#endif