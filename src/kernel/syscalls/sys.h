#pragma once
#include <stdint.h>
#include <stddef.h>

uint64_t sys_getpid(void);
uint64_t sys_getppid(void);
uint64_t sys_getuid(void);
uint64_t sys_geteuid(void);
uint64_t sys_getgid(void);
uint64_t sys_getegid(void);
uint64_t sys_setuid(uint64_t uid);
uint64_t sys_seteuid(uint64_t uid);
uint64_t sys_setgid(uint64_t gid);
uint64_t sys_setreuid(uint64_t ruid, uint64_t euid);
uint64_t sys_setregid(uint64_t rgid, uint64_t egid);
uint64_t sys_setresuid(uint64_t ruid, uint64_t euid, uint64_t suid);
uint64_t sys_getresuid(uint64_t ruid_ptr, uint64_t euid_ptr, uint64_t suid_ptr);
uint64_t sys_setresgid(uint64_t rgid, uint64_t egid, uint64_t sgid);
uint64_t sys_getresgid(uint64_t rgid_ptr, uint64_t egid_ptr, uint64_t sgid_ptr);

uint64_t sys_fork(void);
uint64_t sys_vfork(void);
uint64_t sys_clone(uint64_t flags, uint64_t child_stack,
                   uint64_t ptid, uint64_t ctid, uint64_t tls);

uint64_t sys_wait4(uint64_t pid, uint64_t wstatus_ptr,
                   uint64_t options, uint64_t rusage_ptr);
uint64_t sys_kill(uint64_t pid, uint64_t sig);
uint64_t sys_tgkill(uint64_t tgid, uint64_t tid, uint64_t sig);

uint64_t sys_uname(uint64_t buf_ptr);
uint64_t sys_getcwd(uint64_t buf_ptr, uint64_t size);
uint64_t sys_chdir(uint64_t path_ptr);

uint64_t sys_set_tid_address(uint64_t tidptr);
uint64_t sys_exit_group(uint64_t code);
uint64_t sys_arch_prctl(uint64_t code, uint64_t addr);

uint64_t sys_stat(uint64_t path_ptr, uint64_t statbuf_ptr);
uint64_t sys_lstat(uint64_t path_ptr, uint64_t statbuf_ptr);
uint64_t sys_fstat(uint64_t fd, uint64_t statbuf_ptr);
uint64_t sys_lseek(uint64_t fd, uint64_t offset, uint64_t whence);
uint64_t sys_dup(uint64_t fd);
uint64_t sys_dup2(uint64_t oldfd, uint64_t newfd);
uint64_t sys_access(uint64_t path_ptr, uint64_t mode);
uint64_t sys_mkdir(uint64_t path_ptr, uint64_t mode);
uint64_t sys_rmdir(uint64_t path_ptr);
uint64_t sys_unlink(uint64_t path_ptr);
uint64_t sys_chmod(uint64_t path_ptr, uint64_t mode);
uint64_t sys_fchmod(uint64_t fd, uint64_t mode);
uint64_t sys_chown(uint64_t path_ptr, uint64_t uid, uint64_t gid);
uint64_t sys_fchown(uint64_t fd, uint64_t uid, uint64_t gid);
uint64_t sys_umask(uint64_t mask);
uint64_t sys_readv(uint64_t fd, uint64_t iov_ptr, uint64_t iovcnt);
uint64_t sys_writev(uint64_t fd, uint64_t iov_ptr, uint64_t iovcnt);
uint64_t sys_fcntl(uint64_t fd, uint64_t cmd, uint64_t arg);
uint64_t sys_mmap(uint64_t addr, uint64_t length, uint64_t prot,
                  uint64_t flags, uint64_t fd, uint64_t offset);
uint64_t sys_munmap(uint64_t addr, uint64_t length);
uint64_t sys_mprotect(uint64_t addr, uint64_t length, uint64_t prot);

uint64_t sys_authu(uint64_t username, uint64_t password);
uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count,
                   uint64_t unused1, uint64_t unused2, uint64_t unused3);

uint64_t sys_create(uint64_t path, uint64_t is_dir);
uint64_t sys_getdents64(uint64_t fd, uint64_t buf, uint64_t size);
uint64_t sys_ioctl(uint64_t fd, uint64_t req, uint64_t arg);
uint64_t sys_close(uint64_t fd);
uint64_t sys_open(uint64_t path, uint64_t flags);
uint64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count, uint64_t unused1, uint64_t unused2, uint64_t unused3);

uint64_t sys_socket(uint64_t domain, uint64_t type, uint64_t protocol);
uint64_t sys_bind(uint64_t fd, uint64_t addr, uint64_t addrlen);
uint64_t sys_listen(uint64_t fd, uint64_t backlog);
uint64_t sys_accept(uint64_t fd, uint64_t addr, uint64_t addrlen);
uint64_t connect(uint64_t fd, uint64_t addr, uint64_t addrlen);
uint64_t sendto(uint64_t fd, uint64_t buf, uint64_t count, uint64_t dest);
uint64_t recvfrom(uint64_t fd, uint64_t buf, uint64_t count, uint64_t src_out);