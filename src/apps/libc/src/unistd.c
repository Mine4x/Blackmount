#include <unistd.h>
#include <stdint.h>
#include <stddef.h>

uint64_t syscall6(uint64_t number,
                  uint64_t arg1, uint64_t arg2, uint64_t arg3,
                  uint64_t arg4, uint64_t arg5, uint64_t arg6)
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

/* =========================================================================
 * File I/O
 * ========================================================================= */

uint64_t read(uint64_t fd, void *buf, size_t count)
{
    return syscall6(SYSCALL_READ, fd, (uint64_t)buf, count, 0, 0, 0);
}

uint64_t write(uint64_t fd, const void *buf, size_t count)
{
    return syscall6(SYSCALL_WRITE, fd, (uint64_t)buf, count, 0, 0, 0);
}

uint64_t open(const char *path)
{
    return syscall6(SYSCALL_OPEN, (uint64_t)path, 0, 0, 0, 0, 0);
}

uint64_t close(uint64_t fd)
{
    return syscall6(SYSCALL_CLOSE, fd, 0, 0, 0, 0, 0);
}

int stat(const char *path, struct stat *buf)
{
    return (int)syscall6(SYSCALL_STAT, (uint64_t)path, (uint64_t)buf, 0, 0, 0, 0);
}

int fstat(uint64_t fd, struct stat *buf)
{
    return (int)syscall6(SYSCALL_FSTAT, fd, (uint64_t)buf, 0, 0, 0, 0);
}

int lstat(const char *path, struct stat *buf)
{
    return (int)syscall6(SYSCALL_LSTAT, (uint64_t)path, (uint64_t)buf, 0, 0, 0, 0);
}

int64_t lseek(uint64_t fd, int64_t offset, int whence)
{
    return (int64_t)syscall6(SYSCALL_LSEEK, fd, (uint64_t)offset, (uint64_t)whence, 0, 0, 0);
}

int dup(uint64_t fd)
{
    return (int)syscall6(SYSCALL_DUP, fd, 0, 0, 0, 0, 0);
}

int dup2(uint64_t oldfd, uint64_t newfd)
{
    return (int)syscall6(SYSCALL_DUP2, oldfd, newfd, 0, 0, 0, 0);
}

int access(const char *path, int mode)
{
    return (int)syscall6(SYSCALL_ACCESS, (uint64_t)path, (uint64_t)mode, 0, 0, 0, 0);
}

int fcntl(uint64_t fd, int cmd, uint64_t arg)
{
    return (int)syscall6(SYSCALL_FCNTL, fd, (uint64_t)cmd, arg, 0, 0, 0);
}

int readv(uint64_t fd, const struct iovec *iov, int iovcnt)
{
    return (int)syscall6(SYSCALL_READV, fd, (uint64_t)iov, (uint64_t)iovcnt, 0, 0, 0);
}

int writev(uint64_t fd, const struct iovec *iov, int iovcnt)
{
    return (int)syscall6(SYSCALL_WRITEV, fd, (uint64_t)iov, (uint64_t)iovcnt, 0, 0, 0);
}

int getdents64(uint64_t fd, struct linux_dirent64 *buf, size_t count)
{
    return (int)syscall6(SYSCALL_GETDENTS64, fd, (uint64_t)buf, count, 0, 0, 0);
}

uint64_t ioctl(int fd, uint64_t req, void *arg)
{
    return syscall6(SYSCALL_IOCTL, (uint64_t)fd, req, (uint64_t)arg, 0, 0, 0);
}

/* =========================================================================
 * Filesystem management
 * ========================================================================= */

int mkdir(const char *path, unsigned int mode)
{
    return (int)syscall6(SYSCALL_MKDIR, (uint64_t)path, (uint64_t)mode, 0, 0, 0, 0);
}

int rmdir(const char *path)
{
    return (int)syscall6(SYSCALL_RMDIR, (uint64_t)path, 0, 0, 0, 0, 0);
}

int unlink(const char *path)
{
    return (int)syscall6(SYSCALL_UNLINK, (uint64_t)path, 0, 0, 0, 0, 0);
}

int chmod(const char *path, unsigned int mode)
{
    return (int)syscall6(SYSCALL_CHMOD, (uint64_t)path, (uint64_t)mode, 0, 0, 0, 0);
}

int fchmod(uint64_t fd, unsigned int mode)
{
    return (int)syscall6(SYSCALL_FCHMOD, fd, (uint64_t)mode, 0, 0, 0, 0);
}

int chown(const char *path, unsigned int uid, unsigned int gid)
{
    return (int)syscall6(SYSCALL_CHOWN, (uint64_t)path, (uint64_t)uid, (uint64_t)gid, 0, 0, 0);
}

int fchown(uint64_t fd, unsigned int uid, unsigned int gid)
{
    return (int)syscall6(SYSCALL_FCHOWN, fd, (uint64_t)uid, (uint64_t)gid, 0, 0, 0);
}

unsigned int umask(unsigned int mask)
{
    return (unsigned int)syscall6(SYSCALL_UMASK, (uint64_t)mask, 0, 0, 0, 0, 0);
}

uint64_t create(const char *path, bool is_dir)
{
    return syscall6(SYSCALL_CREATE, (uint64_t)path, (uint64_t)is_dir, 0, 0, 0, 0);
}

/* =========================================================================
 * Memory
 * ========================================================================= */

uint64_t brk(uint64_t addr)
{
    return syscall6(SYSCALL_BRK, addr, 0, 0, 0, 0, 0);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, uint64_t offset)
{
    return (void *)syscall6(SYSCALL_MMAP,
                            (uint64_t)addr, (uint64_t)length,
                            (uint64_t)prot, (uint64_t)flags,
                            (uint64_t)fd,   offset);
}

int munmap(void *addr, size_t length)
{
    return (int)syscall6(SYSCALL_MUNMAP, (uint64_t)addr, (uint64_t)length, 0, 0, 0, 0);
}

int mprotect(void *addr, size_t length, int prot)
{
    return (int)syscall6(SYSCALL_MPROTECT, (uint64_t)addr, (uint64_t)length, (uint64_t)prot, 0, 0, 0);
}

/* =========================================================================
 * Process lifecycle
 * ========================================================================= */

void exit(int exit_code)
{
    syscall6(SYSCALL_EXIT, (uint64_t)exit_code, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}

void exit_group(int exit_code)
{
    syscall6(SYSCALL_EXIT_GROUP, (uint64_t)exit_code, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}

int fork(void)
{
    return (int)syscall6(SYSCALL_FORK, 0, 0, 0, 0, 0, 0);
}

int vfork(void)
{
    return (int)syscall6(SYSCALL_VFORK, 0, 0, 0, 0, 0, 0);
}

int clone(uint64_t flags, void *child_stack)
{
    return (int)syscall6(SYSCALL_CLONE, flags, (uint64_t)child_stack, 0, 0, 0, 0);
}

uint64_t execve(const char *path, const char **argv, const char **envp)
{
    return syscall6(SYSCALL_EXECVE, (uint64_t)path, (uint64_t)argv, (uint64_t)envp, 0, 0, 0);
}

uint64_t execv(const char *path, const char **argv)
{
    return execve(path, argv, (const char **)0);
}

int wait4(int pid, int *wstatus, int options)
{
    return (int)syscall6(SYSCALL_WAIT4,
                         (uint64_t)pid, (uint64_t)wstatus, (uint64_t)options,
                         0, 0, 0);
}

int waitpid(uint64_t pid)
{
    return (int)syscall6(SYSCALL_WAIT, pid, 0, 0, 0, 0, 0);
}

int kill(int pid, int sig)
{
    return (int)syscall6(SYSCALL_KILL, (uint64_t)pid, (uint64_t)sig, 0, 0, 0, 0);
}

void yield(void)
{
    syscall6(SYSCALL_YIELD, 0, 0, 0, 0, 0, 0);
}

uint64_t binrun(const char *path)
{
    return syscall6(SYSCALL_BINRUN, (uint64_t)path, (uint64_t)10, 0, 0, 0, 0);
}

/* =========================================================================
 * Process information
 * ========================================================================= */

int getpid(void)
{
    return (int)syscall6(SYSCALL_GETPID, 0, 0, 0, 0, 0, 0);
}

int getppid(void)
{
    return (int)syscall6(SYSCALL_GETPPID, 0, 0, 0, 0, 0, 0);
}

char *getcwd(char *buf, size_t size)
{
    uint64_t ret = syscall6(SYSCALL_GETCWD, (uint64_t)buf, (uint64_t)size, 0, 0, 0, 0);
    return (ret == (uint64_t)-14ULL) ? (char *)0 : buf;
}

int chdir(const char *path)
{
    return (int)syscall6(SYSCALL_CHDIR, (uint64_t)path, 0, 0, 0, 0, 0);
}

/* =========================================================================
 * Identity — uid
 * ========================================================================= */

unsigned int getuid(void)
{
    return (unsigned int)syscall6(SYSCALL_GETUID, 0, 0, 0, 0, 0, 0);
}

unsigned int geteuid(void)
{
    return (unsigned int)syscall6(SYSCALL_GETEUID, 0, 0, 0, 0, 0, 0);
}

int setuid(unsigned int uid)
{
    return (int)syscall6(SYSCALL_SETUID, (uint64_t)uid, 0, 0, 0, 0, 0);
}

int seteuid(unsigned int uid)
{
    return (int)syscall6(SYSCALL_SETUID, (uint64_t)uid, 0, 0, 0, 0, 0);
}

int setreuid(unsigned int ruid, unsigned int euid)
{
    return (int)syscall6(SYSCALL_SETREUID, (uint64_t)ruid, (uint64_t)euid, 0, 0, 0, 0);
}

int setresuid(unsigned int ruid, unsigned int euid, unsigned int suid)
{
    return (int)syscall6(SYSCALL_SETRESUID, (uint64_t)ruid, (uint64_t)euid, (uint64_t)suid, 0, 0, 0);
}

int getresuid(unsigned int *ruid, unsigned int *euid, unsigned int *suid)
{
    return (int)syscall6(SYSCALL_GETRESUID, (uint64_t)ruid, (uint64_t)euid, (uint64_t)suid, 0, 0, 0);
}

/* =========================================================================
 * Identity — gid
 * ========================================================================= */

unsigned int getgid(void)
{
    return (unsigned int)syscall6(SYSCALL_GETGID, 0, 0, 0, 0, 0, 0);
}

unsigned int getegid(void)
{
    return (unsigned int)syscall6(SYSCALL_GETEGID, 0, 0, 0, 0, 0, 0);
}

int setgid(unsigned int gid)
{
    return (int)syscall6(SYSCALL_SETGID, (uint64_t)gid, 0, 0, 0, 0, 0);
}

int setegid(unsigned int gid)
{
    return (int)syscall6(SYSCALL_SETGID, (uint64_t)gid, 0, 0, 0, 0, 0);
}

int setregid(unsigned int rgid, unsigned int egid)
{
    return (int)syscall6(SYSCALL_SETREGID, (uint64_t)rgid, (uint64_t)egid, 0, 0, 0, 0);
}

int setresgid(unsigned int rgid, unsigned int egid, unsigned int sgid)
{
    return (int)syscall6(SYSCALL_SETRESGID, (uint64_t)rgid, (uint64_t)egid, (uint64_t)sgid, 0, 0, 0);
}

int getresgid(unsigned int *rgid, unsigned int *egid, unsigned int *sgid)
{
    return (int)syscall6(SYSCALL_GETRESGID, (uint64_t)rgid, (uint64_t)egid, (uint64_t)sgid, 0, 0, 0);
}

int user_authenticate(const char* username, const char* password)
{
    return (int)syscall6(SYSCALL_AUTHU, (uint64_t)username, (uint64_t)password, 0 ,0,0,0);
}

/* =========================================================================
 * UNIX domain sockets
 * ========================================================================= */

int socket(int domain, int type, int protocol)
{
    return (int)syscall6(SYSCALL_SOCKET,
                         (uint64_t)domain, (uint64_t)type, (uint64_t)protocol,
                         0, 0, 0);
}

int bind(int sockfd, const struct sockaddr_un *addr, socklen_t addrlen)
{
    return (int)syscall6(SYSCALL_BIND,
                         (uint64_t)sockfd, (uint64_t)addr, (uint64_t)addrlen,
                         0, 0, 0);
}

int listen(int sockfd, int backlog)
{
    return (int)syscall6(SYSCALL_LISTEN,
                         (uint64_t)sockfd, (uint64_t)backlog,
                         0, 0, 0, 0);
}

int accept(int sockfd, struct sockaddr_un *addr, socklen_t *addrlen)
{
    return (int)syscall6(SYSCALL_ACCEPT,
                         (uint64_t)sockfd, (uint64_t)addr, (uint64_t)addrlen,
                         0, 0, 0);
}

int connect(int sockfd, const struct sockaddr_un *addr, socklen_t addrlen)
{
    return (int)syscall6(SYSCALL_CONNECT,
                         (uint64_t)sockfd, (uint64_t)addr, (uint64_t)addrlen,
                         0, 0, 0);
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    return (ssize_t)syscall6(SYSCALL_SENDTO,
                             (uint64_t)sockfd, (uint64_t)buf, (uint64_t)len,
                             (uint64_t)flags, 0, 0);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    return (ssize_t)syscall6(SYSCALL_RECVFROM,
                             (uint64_t)sockfd, (uint64_t)buf, (uint64_t)len,
                             (uint64_t)flags, 0, 0);
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr_un *dest_addr, socklen_t addrlen)
{
    return (ssize_t)syscall6(SYSCALL_SENDTO,
                             (uint64_t)sockfd, (uint64_t)buf, (uint64_t)len,
                             (uint64_t)flags, (uint64_t)dest_addr,
                             (uint64_t)addrlen);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr_un *src_addr, socklen_t *addrlen)
{
    return (ssize_t)syscall6(SYSCALL_RECVFROM,
                             (uint64_t)sockfd, (uint64_t)buf, (uint64_t)len,
                             (uint64_t)flags, (uint64_t)src_addr,
                             (uint64_t)addrlen);
}

int shutdown(int sockfd, int how)
{
    return (int)syscall6(SYSCALL_SHUTDOWN,
                         (uint64_t)sockfd, (uint64_t)how,
                         0, 0, 0, 0);
}

int getsockname(int sockfd, struct sockaddr_un *addr, socklen_t *addrlen)
{
    return (int)syscall6(SYSCALL_GETSOCKNAME,
                         (uint64_t)sockfd, (uint64_t)addr, (uint64_t)addrlen,
                         0, 0, 0);
}

int getpeername(int sockfd, struct sockaddr_un *addr, socklen_t *addrlen)
{
    return (int)syscall6(SYSCALL_GETPEERNAME,
                         (uint64_t)sockfd, (uint64_t)addr, (uint64_t)addrlen,
                         0, 0, 0);
}

int setsockopt(int sockfd, int level, int optname,
               const void *optval, socklen_t optlen)
{
    return (int)syscall6(SYSCALL_SETSOCKOPT,
                         (uint64_t)sockfd, (uint64_t)level, (uint64_t)optname,
                         (uint64_t)optval, (uint64_t)optlen, 0);
}

int getsockopt(int sockfd, int level, int optname,
               void *optval, socklen_t *optlen)
{
    return (int)syscall6(SYSCALL_GETSOCKOPT,
                         (uint64_t)sockfd, (uint64_t)level, (uint64_t)optname,
                         (uint64_t)optval, (uint64_t)optlen, 0);
}