#include "scman.h"
#include "sys.h"
#include <arch/x86_64/syscalls.h>
#include <proc/proc.h>
#include <debug.h>
#include <hal/vfs.h>
#include <shutdown/shutdown.h>
#include <user/user.h>
#include <loaders/bin_loader.h>

uint64_t load_bin(uint64_t path, uint64_t priority)
{
    x86_64_DisableInterrupts();
    int pid = bin_load_elf((const char *)path, (uint32_t)priority,
                           proc_get_current_pid());
    x86_64_EnableInterrupts();
    return (uint64_t)pid;
}

void register_syscalls(void)
{
    x86_64_Syscall_RegisterHandler(0,   (SyscallHandler)sys_read);
    x86_64_Syscall_RegisterHandler(1,   (SyscallHandler)sys_write);
    x86_64_Syscall_RegisterHandler(2,   (SyscallHandler)sys_open);
    x86_64_Syscall_RegisterHandler(3,   (SyscallHandler)sys_close);
    x86_64_Syscall_RegisterHandler(4,   (SyscallHandler)sys_stat);
    x86_64_Syscall_RegisterHandler(5,   (SyscallHandler)sys_fstat);
    x86_64_Syscall_RegisterHandler(6,   (SyscallHandler)sys_lstat);
    x86_64_Syscall_RegisterHandler(8,   (SyscallHandler)sys_lseek);
    x86_64_Syscall_RegisterHandler(9,   (SyscallHandler)sys_mmap);
    x86_64_Syscall_RegisterHandler(10,  (SyscallHandler)sys_mprotect);
    x86_64_Syscall_RegisterHandler(11,  (SyscallHandler)sys_munmap);
    x86_64_Syscall_RegisterHandler(12,  (SyscallHandler)proc_brk);
    x86_64_Syscall_RegisterHandler(16,  (SyscallHandler)sys_ioctl);
    x86_64_Syscall_RegisterHandler(19,  (SyscallHandler)sys_readv);
    x86_64_Syscall_RegisterHandler(20,  (SyscallHandler)sys_writev);
    x86_64_Syscall_RegisterHandler(21,  (SyscallHandler)sys_access);
    x86_64_Syscall_RegisterHandler(32,  (SyscallHandler)sys_dup);
    x86_64_Syscall_RegisterHandler(33,  (SyscallHandler)sys_dup2);
    x86_64_Syscall_RegisterHandler(72,  (SyscallHandler)sys_fcntl);
    x86_64_Syscall_RegisterHandler(83,  (SyscallHandler)sys_mkdir);
    x86_64_Syscall_RegisterHandler(84,  (SyscallHandler)sys_rmdir);
    x86_64_Syscall_RegisterHandler(87,  (SyscallHandler)sys_unlink);
    x86_64_Syscall_RegisterHandler(90,  (SyscallHandler)sys_chmod);
    x86_64_Syscall_RegisterHandler(91,  (SyscallHandler)sys_fchmod);
    x86_64_Syscall_RegisterHandler(92,  (SyscallHandler)sys_chown);
    x86_64_Syscall_RegisterHandler(93,  (SyscallHandler)sys_fchown);
    x86_64_Syscall_RegisterHandler(95,  (SyscallHandler)sys_umask);
    x86_64_Syscall_RegisterHandler(217, (SyscallHandler)sys_getdents64);

    x86_64_Syscall_RegisterHandler(24,  (SyscallHandler)proc_yield);
    x86_64_Syscall_RegisterHandler(35,  (SyscallHandler)proc_brk);

    x86_64_Syscall_RegisterHandler(41,  (SyscallHandler)sys_socket);
    x86_64_Syscall_RegisterHandler(42,  (SyscallHandler)connect);
    x86_64_Syscall_RegisterHandler(43,  (SyscallHandler)sys_accept);
    x86_64_Syscall_RegisterHandler(44,  (SyscallHandler)sendto);
    x86_64_Syscall_RegisterHandler(45,  (SyscallHandler)recvfrom);
    x86_64_Syscall_RegisterHandler(49,  (SyscallHandler)sys_bind);
    x86_64_Syscall_RegisterHandler(50,  (SyscallHandler)sys_listen);

    x86_64_Syscall_RegisterHandler(56,  (SyscallHandler)sys_clone);
    x86_64_Syscall_RegisterHandler(57,  (SyscallHandler)sys_fork);
    x86_64_Syscall_RegisterHandler(58,  (SyscallHandler)sys_vfork);
    x86_64_Syscall_RegisterHandler(59,  (SyscallHandler)sys_execve);
    x86_64_Syscall_RegisterHandler(60,  (SyscallHandler)proc_exit);
    x86_64_Syscall_RegisterHandler(61,  (SyscallHandler)sys_wait4);
    x86_64_Syscall_RegisterHandler(62,  (SyscallHandler)sys_kill);
    x86_64_Syscall_RegisterHandler(231, (SyscallHandler)sys_exit_group);
    x86_64_Syscall_RegisterHandler(234, (SyscallHandler)sys_tgkill);

    x86_64_Syscall_RegisterHandler(39,  (SyscallHandler)sys_getpid);
    x86_64_Syscall_RegisterHandler(110, (SyscallHandler)sys_getppid);
    x86_64_Syscall_RegisterHandler(63,  (SyscallHandler)sys_uname);
    x86_64_Syscall_RegisterHandler(79,  (SyscallHandler)sys_getcwd);
    x86_64_Syscall_RegisterHandler(80,  (SyscallHandler)sys_chdir);
    x86_64_Syscall_RegisterHandler(158, (SyscallHandler)sys_arch_prctl);
    x86_64_Syscall_RegisterHandler(218, (SyscallHandler)sys_set_tid_address);

    x86_64_Syscall_RegisterHandler(102, (SyscallHandler)sys_getuid);
    x86_64_Syscall_RegisterHandler(104, (SyscallHandler)sys_getgid);
    x86_64_Syscall_RegisterHandler(105, (SyscallHandler)sys_setuid);
    x86_64_Syscall_RegisterHandler(106, (SyscallHandler)sys_setgid);
    x86_64_Syscall_RegisterHandler(107, (SyscallHandler)sys_geteuid);
    x86_64_Syscall_RegisterHandler(108, (SyscallHandler)sys_getegid);
    x86_64_Syscall_RegisterHandler(113, (SyscallHandler)sys_setreuid);
    x86_64_Syscall_RegisterHandler(114, (SyscallHandler)sys_setregid);
    x86_64_Syscall_RegisterHandler(117, (SyscallHandler)sys_setresuid);
    x86_64_Syscall_RegisterHandler(118, (SyscallHandler)sys_getresuid);
    x86_64_Syscall_RegisterHandler(119, (SyscallHandler)sys_setresgid);
    x86_64_Syscall_RegisterHandler(120, (SyscallHandler)sys_getresgid);

    x86_64_Syscall_RegisterHandler(48,  (SyscallHandler)shutdown);

    x86_64_Syscall_RegisterHandler(301, (SyscallHandler)load_bin);
    x86_64_Syscall_RegisterHandler(302, (SyscallHandler)proc_wait_pid);
    x86_64_Syscall_RegisterHandler(303, (SyscallHandler)sys_create);
    x86_64_Syscall_RegisterHandler(304, (SyscallHandler)sys_authu);
}