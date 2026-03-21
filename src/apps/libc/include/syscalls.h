#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* File I/O */
#define SYSCALL_READ        0
#define SYSCALL_WRITE       1
#define SYSCALL_OPEN        2
#define SYSCALL_CLOSE       3
#define SYSCALL_STAT        4
#define SYSCALL_FSTAT       5
#define SYSCALL_LSTAT       6
#define SYSCALL_LSEEK       8
#define SYSCALL_MMAP        9
#define SYSCALL_MPROTECT    10
#define SYSCALL_MUNMAP      11
#define SYSCALL_BRK         12
#define SYSCALL_IOCTL       16
#define SYSCALL_READV       19
#define SYSCALL_WRITEV      20
#define SYSCALL_ACCESS      21
#define SYSCALL_DUP         32
#define SYSCALL_DUP2        33
#define SYSCALL_FCNTL       72
#define SYSCALL_MKDIR       83
#define SYSCALL_RMDIR       84
#define SYSCALL_UNLINK      87
#define SYSCALL_CHMOD       90
#define SYSCALL_FCHMOD      91
#define SYSCALL_CHOWN       92
#define SYSCALL_FCHOWN      93
#define SYSCALL_UMASK       95
#define SYSCALL_GETDENTS64  217

/* Process scheduling */
#define SYSCALL_YIELD       24
#define SYSCALL_NANOSLEEP   35

/* Process lifecycle */
#define SYSCALL_CLONE       56
#define SYSCALL_FORK        57
#define SYSCALL_VFORK       58
#define SYSCALL_EXECVE      59
#define SYSCALL_EXIT        60
#define SYSCALL_WAIT4       61
#define SYSCALL_KILL        62
#define SYSCALL_EXIT_GROUP  231
#define SYSCALL_TGKILL      234

/* Process info */
#define SYSCALL_GETPID      39
#define SYSCALL_GETPPID     110
#define SYSCALL_UNAME       63
#define SYSCALL_GETCWD      79
#define SYSCALL_CHDIR       80
#define SYSCALL_SET_TID_ADDR 218

/* Identity */
#define SYSCALL_GETUID      102
#define SYSCALL_GETGID      104
#define SYSCALL_SETUID      105
#define SYSCALL_SETGID      106
#define SYSCALL_GETEUID     107
#define SYSCALL_GETEGID     108
#define SYSCALL_SETREUID    113
#define SYSCALL_SETREGID    114
#define SYSCALL_SETRESUID   117
#define SYSCALL_GETRESUID   118
#define SYSCALL_SETRESGID   119
#define SYSCALL_GETRESGID   120

/* Kernel-private */
#define SYSCALL_BINRUN      301
#define SYSCALL_WAIT        302
#define SYSCALL_CREATE      303
#define SYSCALL_AUTHU       304

/* =========================================================================
 * Supporting types
 * ========================================================================= */

struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    uint64_t st_atime;
    uint64_t st_atime_ns;
    uint64_t st_mtime;
    uint64_t st_mtime_ns;
    uint64_t st_ctime;
    uint64_t st_ctime_ns;
    int64_t  __unused[3];
} __attribute__((packed));

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

struct iovec {
    void  *iov_base;
    size_t iov_len;
};

struct linux_dirent64 {
    uint64_t       d_ino;
    int64_t        d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[];
} __attribute__((packed));

/* lseek whence values */
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

/* access() mode bits */
#define F_OK  0
#define R_OK  4
#define W_OK  2
#define X_OK  1

/* open() flags (kernel ignores beyond path, defined for ABI compat) */
#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT   0100
#define O_TRUNC  01000
#define O_APPEND 02000

/* fcntl() commands */
#define F_DUPFD   1
#define F_GETFD   2

/* clone() flags */
#define CLONE_VM     0x00000100
#define CLONE_VFORK  0x00004000
#define CLONE_THREAD 0x00010000

/* wait4() options */
#define WNOHANG   1
#define WUNTRACED 2

/* kill() signals */
#define SIGKILL  9
#define SIGTERM 15

/* stat() mode type bits */
#define S_IFMT   0170000
#define S_IFSOCK 0140000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)

/* =========================================================================
 * Raw syscall primitive
 * ========================================================================= */

uint64_t syscall6(uint64_t number,
                  uint64_t arg1, uint64_t arg2, uint64_t arg3,
                  uint64_t arg4, uint64_t arg5, uint64_t arg6);

/* =========================================================================
 * File I/O
 * ========================================================================= */

uint64_t read(uint64_t fd, void *buf, size_t count);
uint64_t write(uint64_t fd, const void *buf, size_t count);
uint64_t open(const char *path);
uint64_t close(uint64_t fd);
int      stat(const char *path, struct stat *buf);
int      fstat(uint64_t fd, struct stat *buf);
int      lstat(const char *path, struct stat *buf);
int64_t  lseek(uint64_t fd, int64_t offset, int whence);
int      dup(uint64_t fd);
int      dup2(uint64_t oldfd, uint64_t newfd);
int      access(const char *path, int mode);
int      fcntl(uint64_t fd, int cmd, uint64_t arg);
int      readv(uint64_t fd, const struct iovec *iov, int iovcnt);
int      writev(uint64_t fd, const struct iovec *iov, int iovcnt);
int      getdents64(uint64_t fd, struct linux_dirent64 *buf, size_t count);
uint64_t ioctl(int fd, uint64_t req, void *arg);

/* =========================================================================
 * Filesystem management
 * ========================================================================= */

int      mkdir(const char *path, unsigned int mode);
int      rmdir(const char *path);
int      unlink(const char *path);
int      chmod(const char *path, unsigned int mode);
int      fchmod(uint64_t fd, unsigned int mode);
int      chown(const char *path, unsigned int uid, unsigned int gid);
int      fchown(uint64_t fd, unsigned int uid, unsigned int gid);
unsigned int umask(unsigned int mask);
uint64_t create(const char *path, bool is_dir);

/* =========================================================================
 * Memory
 * ========================================================================= */

uint64_t brk(uint64_t addr);
void    *mmap(void *addr, size_t length, int prot, int flags,
              int fd, uint64_t offset);
int      munmap(void *addr, size_t length);
int      mprotect(void *addr, size_t length, int prot);

/* =========================================================================
 * Process lifecycle
 * ========================================================================= */

void     exit(int exit_code);
void     exit_group(int exit_code);
int      fork(void);
int      vfork(void);
int      clone(uint64_t flags, void *child_stack);
uint64_t execve(const char *path, const char **argv, const char **envp);
uint64_t execv(const char *path, const char **argv);
int      wait4(int pid, int *wstatus, int options);
int      waitpid(uint64_t pid);
int      kill(int pid, int sig);
void     yield(void);
uint64_t binrun(const char *path);

/* =========================================================================
 * Process information
 * ========================================================================= */

int  getpid(void);
int  getppid(void);
int  uname(struct utsname *buf);
char *getcwd(char *buf, size_t size);
int  chdir(const char *path);

/* =========================================================================
 * Identity — uid
 * ========================================================================= */

unsigned int getuid(void);
unsigned int geteuid(void);
int          setuid(unsigned int uid);
int          seteuid(unsigned int uid);
int          setreuid(unsigned int ruid, unsigned int euid);
int          setresuid(unsigned int ruid, unsigned int euid, unsigned int suid);
int          getresuid(unsigned int *ruid, unsigned int *euid, unsigned int *suid);
int          user_authenticate(const char* username, const char* password);

/* =========================================================================
 * Identity — gid
 * ========================================================================= */

unsigned int getgid(void);
unsigned int getegid(void);
int          setgid(unsigned int gid);
int          setegid(unsigned int gid);
int          setregid(unsigned int rgid, unsigned int egid);
int          setresgid(unsigned int rgid, unsigned int egid, unsigned int sgid);
int          getresgid(unsigned int *rgid, unsigned int *egid, unsigned int *sgid);

#endif /* SYSCALLS_H */