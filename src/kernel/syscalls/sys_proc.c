#include "sys_proc.h"
#include <proc/proc.h>
#include <hal/vfs.h>
#include <drivers/fs/ext/ext2.h>
#include <user/user.h>
#include <string.h>
#include <debug.h>

#define EFAULT  14
#define EPERM    1
#define ESRCH    3
#define EINVAL  22
#define ERANGE  34
#define ENOMEM  12

#define SYSCALL_ERR(e) ((uint64_t)(-(int64_t)(e)))

extern ext2_fs_t *rootfs;

struct kernel_stat {
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

struct kernel_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

struct iovec {
    void   *iov_base;
    size_t  iov_len;
};

static void fill_stat_from_inode(struct kernel_stat *ks, const ext2_inode_t *inode)
{
    ks->st_ino     = 0;
    ks->st_dev     = 0;
    ks->st_nlink   = inode->i_links_count;
    ks->st_mode    = inode->i_mode;
    ks->st_uid     = inode->i_uid;
    ks->st_gid     = inode->i_gid;
    ks->__pad0     = 0;
    ks->st_rdev    = 0;
    ks->st_size    = (int64_t)inode->i_size;
    ks->st_blksize = 4096;
    ks->st_blocks  = (int64_t)inode->i_blocks;
    ks->st_atime   = inode->i_atime;
    ks->st_atime_ns = 0;
    ks->st_mtime   = inode->i_mtime;
    ks->st_mtime_ns = 0;
    ks->st_ctime   = inode->i_ctime;
    ks->st_ctime_ns = 0;
}

uint64_t sys_getpid(void)
{
    int pid = proc_get_current_pid();
    return pid < 0 ? SYSCALL_ERR(ESRCH) : (uint64_t)pid;
}

uint64_t sys_getppid(void)
{
    int ppid = proc_getppid();
    return ppid < 0 ? SYSCALL_ERR(ESRCH) : (uint64_t)ppid;
}

uint64_t sys_getuid(void)  { return (uint64_t)(int64_t)proc_getuid(); }
uint64_t sys_geteuid(void) { return (uint64_t)(int64_t)proc_geteuid(); }
uint64_t sys_getgid(void)  { return (uint64_t)(int64_t)proc_getgid(); }
uint64_t sys_getegid(void) { return (uint64_t)(int64_t)proc_getegid(); }

uint64_t sys_setuid(uint64_t uid)
{
    return proc_setuid((uid_t)uid) == 0 ? 0 : SYSCALL_ERR(EPERM);
}

uint64_t sys_seteuid(uint64_t uid)
{
    return proc_seteuid((uid_t)uid) == 0 ? 0 : SYSCALL_ERR(EPERM);
}

uint64_t sys_setgid(uint64_t gid)
{
    return proc_setgid((gid_t)gid) == 0 ? 0 : SYSCALL_ERR(EPERM);
}

uint64_t sys_setreuid(uint64_t ruid, uint64_t euid)
{
    return proc_setreuid((uid_t)ruid, (uid_t)euid) == 0 ? 0 : SYSCALL_ERR(EPERM);
}

uint64_t sys_setregid(uint64_t rgid, uint64_t egid)
{
    return proc_setregid((gid_t)rgid, (gid_t)egid) == 0 ? 0 : SYSCALL_ERR(EPERM);
}

uint64_t sys_setresuid(uint64_t ruid, uint64_t euid, uint64_t suid)
{
    int pid = proc_get_current_pid();
    if (pid < 0)
        return SYSCALL_ERR(ESRCH);

    uid_t cur_euid = proc_geteuid();
    if (!user_is_root(cur_euid)) {
        uid_t r = proc_getuid();
        uid_t e = cur_euid;
        uint64_t allowed[3] = { (uint64_t)r, (uint64_t)e, (uint64_t)-1 };
        (void)allowed;
        if (proc_setreuid((uid_t)ruid, (uid_t)euid) < 0)
            return SYSCALL_ERR(EPERM);
        return 0;
    }

    proc_setreuid((uid_t)ruid, (uid_t)euid);
    if (suid != (uint64_t)-1)
        proc_seteuid((uid_t)suid);
    return 0;
}

uint64_t sys_getresuid(uint64_t ruid_ptr, uint64_t euid_ptr, uint64_t suid_ptr)
{
    uid_t r = proc_getuid();
    uid_t e = proc_geteuid();

    if (ruid_ptr) *(uid_t *)ruid_ptr = r;
    if (euid_ptr) *(uid_t *)euid_ptr = e;
    if (suid_ptr) *(uid_t *)suid_ptr = e;
    return 0;
}

uint64_t sys_setresgid(uint64_t rgid, uint64_t egid, uint64_t sgid)
{
    return proc_setresgid((gid_t)rgid, (gid_t)egid, (gid_t)sgid) == 0
           ? 0 : SYSCALL_ERR(EPERM);
}

uint64_t sys_getresgid(uint64_t rgid_ptr, uint64_t egid_ptr, uint64_t sgid_ptr)
{
    gid_t r, e, s;
    if (proc_getresgid(&r, &e, &s) < 0)
        return SYSCALL_ERR(ESRCH);

    if (rgid_ptr) *(gid_t *)rgid_ptr = r;
    if (egid_ptr) *(gid_t *)egid_ptr = e;
    if (sgid_ptr) *(gid_t *)sgid_ptr = s;
    return 0;
}

uint64_t sys_fork(void)
{
    Registers *frame = proc_get_syscall_frame();
    if (!frame)
        return SYSCALL_ERR(EINVAL);

    int child = proc_fork(frame);
    return child < 0 ? SYSCALL_ERR(ENOMEM) : (uint64_t)child;
}

uint64_t sys_vfork(void)
{
    Registers *frame = proc_get_syscall_frame();
    if (!frame)
        return SYSCALL_ERR(EINVAL);

    int child = proc_vfork(frame);
    return child < 0 ? SYSCALL_ERR(ENOMEM) : (uint64_t)child;
}

uint64_t sys_clone(uint64_t flags, uint64_t child_stack,
                   uint64_t ptid, uint64_t ctid, uint64_t tls)
{
    (void)ptid; (void)ctid; (void)tls;

    Registers *frame = proc_get_syscall_frame();
    if (!frame)
        return SYSCALL_ERR(EINVAL);

    int child = proc_clone(frame, flags, child_stack);
    return child < 0 ? SYSCALL_ERR(ENOMEM) : (uint64_t)child;
}

uint64_t sys_wait4(uint64_t pid, uint64_t wstatus_ptr,
                   uint64_t options, uint64_t rusage_ptr)
{
    (void)options; (void)rusage_ptr;

    uid_t actor = proc_getuid();
    uint64_t exit_code = proc_wait_pid_as(actor, pid);

    if (exit_code == (uint64_t)-1)
        return SYSCALL_ERR(ESRCH);

    if (wstatus_ptr)
        *(int *)wstatus_ptr = (int)((exit_code & 0xFF) << 8);

    return pid;
}

uint64_t sys_kill(uint64_t pid, uint64_t sig)
{
    (void)sig;

    uid_t actor = proc_getuid();

    if (sig == 9 || sig == 15 || sig == 0) {
        int r = proc_kill_as(actor, (int)pid);
        return r == 0 ? 0 : SYSCALL_ERR(EPERM);
    }

    return 0;
}

uint64_t sys_tgkill(uint64_t tgid, uint64_t tid, uint64_t sig)
{
    (void)tgid;
    return sys_kill(tid, sig);
}

uint64_t sys_uname(uint64_t buf_ptr)
{
    if (!buf_ptr)
        return SYSCALL_ERR(EFAULT);

    struct kernel_utsname *u = (struct kernel_utsname *)buf_ptr;
    memset(u, 0, sizeof(*u));

    const char sysname[]    = "Blackmount";
    const char nodename[]   = "blackmount";
    const char release[]    = "1.0.0";
    const char version[]    = "#1";
    const char machine[]    = "x86_64";
    const char domainname[] = "(none)";

    memcpy(u->sysname,    sysname,    sizeof(sysname));
    memcpy(u->nodename,   nodename,   sizeof(nodename));
    memcpy(u->release,    release,    sizeof(release));
    memcpy(u->version,    version,    sizeof(version));
    memcpy(u->machine,    machine,    sizeof(machine));
    memcpy(u->domainname, domainname, sizeof(domainname));

    return 0;
}

uint64_t sys_authu(uint64_t username, uint64_t password)
{
    if (!username || !password)
        return SYSCALL_ERR(EFAULT);
    
    user_authenticate((const char*)username, (const char*)password);
}

uint64_t sys_getcwd(uint64_t buf_ptr, uint64_t size)
{
    if (!buf_ptr || size == 0)
        return SYSCALL_ERR(EFAULT);

    char *buf = (char *)buf_ptr;

    if (proc_getcwd(buf, (size_t)size) < 0)
        return SYSCALL_ERR(ERANGE);

    return buf_ptr;
}

uint64_t sys_chdir(uint64_t path_ptr)
{
    if (!path_ptr)
        return SYSCALL_ERR(EFAULT);

    const char *path = (const char *)path_ptr;

    ext2_inode_t inode;
    if (!rootfs || ext2_stat(rootfs, path, &inode) != EXT2_SUCCESS)
        return SYSCALL_ERR(EINVAL);

    if ((inode.i_mode & 0xF000) != EXT2_S_IFDIR)
        return SYSCALL_ERR(EINVAL);

    uid_t uid = proc_geteuid();
    if (ext2_access(rootfs, path, uid, ACCESS_EXEC) != EXT2_SUCCESS)
        return SYSCALL_ERR(EPERM);

    return proc_chdir(path) == 0 ? 0 : SYSCALL_ERR(EINVAL);
}

uint64_t sys_set_tid_address(uint64_t tidptr)
{
    (void)tidptr;
    return (uint64_t)proc_get_current_pid();
}

uint64_t sys_exit_group(uint64_t code)
{
    proc_exit(code);
    __builtin_unreachable();
}

uint64_t sys_stat(uint64_t path_ptr, uint64_t statbuf_ptr)
{
    if (!path_ptr || !statbuf_ptr)
        return SYSCALL_ERR(EFAULT);

    ext2_inode_t inode;
    if (!rootfs || ext2_stat(rootfs, (const char *)path_ptr, &inode) != EXT2_SUCCESS)
        return SYSCALL_ERR(EINVAL);

    fill_stat_from_inode((struct kernel_stat *)statbuf_ptr, &inode);
    return 0;
}

uint64_t sys_lstat(uint64_t path_ptr, uint64_t statbuf_ptr)
{
    return sys_stat(path_ptr, statbuf_ptr);
}

uint64_t sys_fstat(uint64_t fd, uint64_t statbuf_ptr)
{
    if (!statbuf_ptr)
        return SYSCALL_ERR(EFAULT);

    if (fd >= MAX_OPEN_FILES)
        return SYSCALL_ERR(EINVAL);

    ext2_inode_t inode;
    extern VFS_File_t *open_files;
    if (!open_files[fd].exists || !open_files[fd].path)
        return SYSCALL_ERR(EINVAL);

    if (!rootfs || ext2_stat(rootfs, open_files[fd].path, &inode) != EXT2_SUCCESS)
        return SYSCALL_ERR(EINVAL);

    fill_stat_from_inode((struct kernel_stat *)statbuf_ptr, &inode);
    return 0;
}

uint64_t sys_lseek(uint64_t fd, uint64_t offset, uint64_t whence)
{
    if (fd >= MAX_OPEN_FILES)
        return SYSCALL_ERR(EINVAL);

    int r = VFS_Set_Pos((int)fd, (uint32_t)offset, false);
    return r == 0 ? offset : SYSCALL_ERR(EINVAL);
}

uint64_t sys_dup(uint64_t fd)
{
    if (fd >= MAX_OPEN_FILES)
        return SYSCALL_ERR(EINVAL);

    extern VFS_File_t *open_files;
    if (!open_files[fd].exists)
        return SYSCALL_ERR(EINVAL);

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i].exists) {
            open_files[i] = open_files[fd];
            open_files[i].file = NULL;
            if (open_files[fd].path) {
                open_files[i].file = ext2_open(rootfs, open_files[fd].path);
            }
            open_files[i].pid = proc_get_current_pid();
            return (uint64_t)i;
        }
    }

    return SYSCALL_ERR(ENOMEM);
}

uint64_t sys_dup2(uint64_t oldfd, uint64_t newfd)
{
    if (oldfd >= MAX_OPEN_FILES || newfd >= MAX_OPEN_FILES)
        return SYSCALL_ERR(EINVAL);

    extern VFS_File_t *open_files;
    if (!open_files[oldfd].exists)
        return SYSCALL_ERR(EINVAL);

    if (oldfd == newfd)
        return newfd;

    if (open_files[newfd].exists)
        VFS_Close((int)newfd, false);

    open_files[newfd] = open_files[oldfd];
    open_files[newfd].file = NULL;
    if (open_files[oldfd].path)
        open_files[newfd].file = ext2_open(rootfs, open_files[oldfd].path);
    open_files[newfd].pid = proc_get_current_pid();

    return newfd;
}

uint64_t sys_access(uint64_t path_ptr, uint64_t mode)
{
    if (!path_ptr)
        return SYSCALL_ERR(EFAULT);

    const char *path = (const char *)path_ptr;

    if (mode == 0) {
        return ext2_exists(rootfs, path) ? 0 : SYSCALL_ERR(EINVAL);
    }

    int mask = 0;
    if (mode & 4) mask |= ACCESS_READ;
    if (mode & 2) mask |= ACCESS_WRITE;
    if (mode & 1) mask |= ACCESS_EXEC;

    uid_t uid = proc_getuid();
    int r = ext2_access(rootfs, path, uid, mask);
    return r == EXT2_SUCCESS ? 0 : SYSCALL_ERR(EPERM);
}

uint64_t sys_mkdir(uint64_t path_ptr, uint64_t mode)
{
    (void)mode;
    if (!path_ptr)
        return SYSCALL_ERR(EFAULT);

    int r = VFS_Create((const char *)path_ptr, true);
    return r == 0 ? 0 : SYSCALL_ERR(EINVAL);
}

uint64_t sys_rmdir(uint64_t path_ptr)
{
    if (!path_ptr)
        return SYSCALL_ERR(EFAULT);

    if (!rootfs)
        return SYSCALL_ERR(EINVAL);

    uid_t uid = proc_geteuid();
    int r = ext2_chown_as(rootfs, (const char *)path_ptr, (uint16_t)uid,
                          (uint16_t)-1, (uint16_t)-1);
    (void)r;

    return ext2_rmdir(rootfs, (const char *)path_ptr) == EXT2_SUCCESS
           ? 0 : SYSCALL_ERR(EINVAL);
}

uint64_t sys_unlink(uint64_t path_ptr)
{
    if (!path_ptr)
        return SYSCALL_ERR(EFAULT);

    if (!rootfs)
        return SYSCALL_ERR(EINVAL);

    uid_t uid = proc_geteuid();
    if (ext2_access(rootfs, (const char *)path_ptr, uid, ACCESS_WRITE) != EXT2_SUCCESS)
        return SYSCALL_ERR(EPERM);

    return ext2_delete(rootfs, (const char *)path_ptr) == EXT2_SUCCESS
           ? 0 : SYSCALL_ERR(EINVAL);
}

uint64_t sys_chmod(uint64_t path_ptr, uint64_t mode)
{
    if (!path_ptr)
        return SYSCALL_ERR(EFAULT);

    uid_t uid = proc_geteuid();
    int r = ext2_chmod_as(rootfs, (const char *)path_ptr, uid, (uint16_t)mode);
    return r == EXT2_SUCCESS ? 0 : SYSCALL_ERR(EPERM);
}

uint64_t sys_fchmod(uint64_t fd, uint64_t mode)
{
    if (fd >= MAX_OPEN_FILES)
        return SYSCALL_ERR(EINVAL);

    extern VFS_File_t *open_files;
    if (!open_files[fd].exists || !open_files[fd].path)
        return SYSCALL_ERR(EINVAL);

    return sys_chmod((uint64_t)open_files[fd].path, mode);
}

uint64_t sys_chown(uint64_t path_ptr, uint64_t uid, uint64_t gid)
{
    if (!path_ptr)
        return SYSCALL_ERR(EFAULT);

    uid_t actor = proc_geteuid();
    int r = ext2_chown_as(rootfs, (const char *)path_ptr,
                          (uint16_t)actor,
                          (uint16_t)uid, (uint16_t)gid);
    return r == EXT2_SUCCESS ? 0 : SYSCALL_ERR(EPERM);
}

uint64_t sys_fchown(uint64_t fd, uint64_t uid, uint64_t gid)
{
    if (fd >= MAX_OPEN_FILES)
        return SYSCALL_ERR(EINVAL);

    extern VFS_File_t *open_files;
    if (!open_files[fd].exists || !open_files[fd].path)
        return SYSCALL_ERR(EINVAL);

    return sys_chown((uint64_t)open_files[fd].path, uid, gid);
}

uint64_t sys_umask(uint64_t mask)
{
    (void)mask;
    return 0022;
}

uint64_t sys_readv(uint64_t fd, uint64_t iov_ptr, uint64_t iovcnt)
{
    if (!iov_ptr)
        return SYSCALL_ERR(EFAULT);

    struct iovec *iov = (struct iovec *)iov_ptr;
    uint64_t total = 0;

    for (uint64_t i = 0; i < iovcnt; i++) {
        if (!iov[i].iov_base || iov[i].iov_len == 0)
            continue;
        int r = VFS_Read((int)fd, iov[i].iov_len, iov[i].iov_base);
        if (r < 0)
            return total > 0 ? total : SYSCALL_ERR(EINVAL);
        total += (uint64_t)r;
        if ((size_t)r < iov[i].iov_len)
            break;
    }

    return total;
}

uint64_t sys_writev(uint64_t fd, uint64_t iov_ptr, uint64_t iovcnt)
{
    if (!iov_ptr)
        return SYSCALL_ERR(EFAULT);

    struct iovec *iov = (struct iovec *)iov_ptr;
    uint64_t total = 0;

    for (uint64_t i = 0; i < iovcnt; i++) {
        if (!iov[i].iov_base || iov[i].iov_len == 0)
            continue;
        int r = VFS_Write((int)fd, iov[i].iov_len, iov[i].iov_base, false);
        if (r < 0)
            return total > 0 ? total : SYSCALL_ERR(EINVAL);
        total += (uint64_t)r;
    }

    return total;
}

uint64_t sys_fcntl(uint64_t fd, uint64_t cmd, uint64_t arg)
{
    (void)arg;

    if (fd >= MAX_OPEN_FILES)
        return SYSCALL_ERR(EINVAL);

    switch (cmd) {
    case 1: return fd;
    case 2: return sys_dup(fd);
    default: return 0;
    }
}

uint64_t sys_mmap(uint64_t addr, uint64_t length, uint64_t prot,
                  uint64_t flags, uint64_t fd, uint64_t offset)
{
    (void)addr; (void)prot; (void)flags; (void)fd; (void)offset;

    if (length == 0)
        return SYSCALL_ERR(EINVAL);

    uint64_t result = proc_brk(0);
    if (result == (uint64_t)-1)
        return SYSCALL_ERR(ENOMEM);

    uint64_t aligned = (length + 4095) & ~(uint64_t)4095;
    uint64_t new_brk = proc_sbrk((int64_t)aligned);
    if (new_brk == (uint64_t)-1)
        return SYSCALL_ERR(ENOMEM);

    return new_brk;
}

uint64_t sys_munmap(uint64_t addr, uint64_t length)
{
    (void)addr; (void)length;
    return 0;
}

uint64_t sys_mprotect(uint64_t addr, uint64_t length, uint64_t prot)
{
    (void)addr; (void)length; (void)prot;
    return 0;
}