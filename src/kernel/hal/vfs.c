#include "vfs.h"
#include <user/user.h>
#include <arch/x86_64/e9.h>
#include <fb/textrenderer.h>
#include <block/block.h>
#include <stdbool.h>
#include <drivers/fs/ext/ext2.h>
#include <drivers/disk/ata.h>
#include <block/block_image.h>
#include <config/config.h>
#include <heap.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <debug.h>
#include <panic/panic.h>
#include <proc/proc.h>
#include <console/console.h>
#include <device/device.h>
#include <device/stdin/device_stdin.h>
#include <device/stdout/device_stdout.h>
#include <block/block_mbr.h>

block_device_t* rootdrive;
ext2_fs_t*      rootfs;

bool mounted = false;

const char* special_paths[] = {"/dev/stdin", "/dev/stdout", "/dev/stderr", "/dev/stddbg"};
const int sp_len = sizeof(special_paths) / sizeof(special_paths[0]);

VFS_File_t* open_files;

int get_value_by_key(const char* input, int key, char* out, size_t out_size)
{
    if (!input || !out || out_size == 0)
        return -1;

    int current_key = 0;
    size_t out_index = 0;

    while (*input)
    {
        if (current_key == key)
        {
            if (*input == '/')
                break;

            if (out_index + 1 < out_size)
                out[out_index++] = *input;
        }

        if (*input == '/')
            current_key++;

        input++;
    }

    if (current_key != key)
        return -1;

    out[out_index] = '\0';
    return 0;
}

static int check_path(const char* path)
{
    for (int i = 0; i < sp_len; i++) {
        if (strcmp(path, special_paths[i]) == 0)
            return i;
    }
    return -1;
}

static uid_t vfs_caller_uid(void)
{
    int pid = proc_get_current_pid();
    if (pid < 0)
        return UID_ROOT;
    uid_t uid = proc_get_owner(pid);
    if (!user_exists(uid))
        return UID_ROOT;
    return uid;
}

static bool vfs_access_ok(const char* path, int mask, bool privileged)
{
    if (privileged)
        return true;
    uid_t uid = vfs_caller_uid();
    if (user_is_root(uid))
        return true;
    return ext2_access(rootfs, path, uid, mask) == EXT2_SUCCESS;
}

int VFS_Create(const char* path, bool isDir)
{
    if (!vfs_access_ok(path, ACCESS_WRITE, false)) {
        log_err("VFS", "VFS_Create: permission denied for %s", path);
        return -1;
    }

    if (isDir) {
        int result = ext2_mkdir(rootfs, path);
        if (result == EXT2_SUCCESS)
            return 0;
        return -1;
    }

    int result = ext2_create(rootfs, path, 0644);
    if (result == EXT2_SUCCESS)
        return 0;
    return -1;
}

static int setflags(file_flags_t flags, int fd)
{
    if (!open_files[fd].exists)
        return -1;

    open_files[fd].flags = flags;
    return 0;
}

int VFS_Open(const char* path, bool privileged)
{
    int response = check_path(path);
    if (response != -1 && !privileged)
        return response;

    if (!vfs_access_ok(path, ACCESS_READ, privileged)) {
        log_err("VFS", "VFS_Open: permission denied for %s", path);
        return -1;
    }

    uid_t caller_uid = privileged ? UID_ROOT : vfs_caller_uid();

    ext2_inode_t inode;
    if (ext2_stat(rootfs, path, &inode) == EXT2_SUCCESS &&
        (inode.i_mode & 0xF000) == EXT2_S_IFDIR)
    {
        ext2_dir_iter_t* iter = ext2_opendir(rootfs, path);
        if (!iter)
            return -1;

        for (int i = 0; i < MAX_OPEN_FILES; i++) {
            if (!open_files[i].exists) {
                open_files[i].path     = path;
                open_files[i].file     = NULL;
                open_files[i].dir_iter = iter;
                open_files[i].is_dir   = true;
                open_files[i].exists   = true;
                open_files[i].is_dev   = false;
                open_files[i].owner    = caller_uid;
                open_files[i].pid      = privileged ? -1 : proc_get_current_pid();
                if (privileged)
                    open_files[i].flags = KERNEL;
                return i;
            }
        }

        ext2_closedir(iter);
        return -1;
    }

    ext2_file_t* ext2_file = ext2_open(rootfs, path);
    if (!ext2_file)
        return -1;

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i].exists) {
            open_files[i].path     = path;
            open_files[i].file     = ext2_file;
            open_files[i].dir_iter = NULL;
            open_files[i].is_dir   = false;
            open_files[i].exists   = true;
            open_files[i].owner    = caller_uid;
            open_files[i].pid      = proc_get_current_pid();

            if (privileged) {
                open_files[i].pid   = -1;
                open_files[i].flags = KERNEL;
            }

            device_t* dev = device_get(path);
            if (dev != NULL) {
                open_files[i].is_dev = true;
                open_files[i].dev    = dev;
                log_info("VFS", "is dev");
            }

            return i;
        }
    }

    ext2_close(ext2_file);
    return -1;
}

int VFS_ioctl(int fd, uint64_t req, void* arg)
{
    int pid = proc_get_current_pid();

    if (!open_files[fd].exists || !open_files[fd].is_dev || open_files[fd].pid != pid)
        return -1;

    return open_files[fd].dev->dispatch(pid, req, arg);
}

int VFS_Write(int fd, size_t count, void* buf, bool privileged)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return -1;

    if (!open_files[fd].exists)
        return -1;

    if (!privileged
        && (open_files[fd].flags & KERNEL)
        && !(open_files[fd].flags & USER_WRITE))
        return -1;

    if (!privileged && open_files[fd].pid != proc_get_current_pid())
        return -1;

    if (!open_files[fd].is_dev
        && !vfs_access_ok(open_files[fd].path, ACCESS_WRITE, privileged))
        return -1;

    if (!open_files[fd].file)
        return -1;

    int result = ext2_write(open_files[fd].file, buf, (uint32_t)count);
    if (result < 0)
        return -1;

    return result;
}

int VFS_Read(int fd, size_t count, void* buf)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return -1;

    if (!open_files[fd].exists)
        return -1;

    if (open_files[fd].pid != -1 && open_files[fd].pid != proc_get_current_pid())
        return -1;

    if (!open_files[fd].file)
        return -1;

    int result = ext2_read(open_files[fd].file, buf, (uint32_t)count);
    if (result < 0)
        return -1;

    return result;
}

int VFS_Close(int fd, bool privileged)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return -1;

    if (!open_files[fd].exists)
        return -1;

    if (!privileged && (open_files[fd].flags & KERNEL))
        return -1;

    if (!privileged && open_files[fd].pid != proc_get_current_pid())
        return -1;

    if (open_files[fd].is_dir && open_files[fd].dir_iter) {
        ext2_closedir(open_files[fd].dir_iter);
        open_files[fd].dir_iter = NULL;
    } else if (open_files[fd].file) {
        ext2_close(open_files[fd].file);
        open_files[fd].file = NULL;
    }

    open_files[fd].exists = false;
    open_files[fd].is_dir = false;
    return 0;
}

int VFS_GetDents64(int fd, struct linux_dirent64* buf, size_t count)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return -1;

    if (!open_files[fd].exists || !open_files[fd].is_dir || !open_files[fd].dir_iter)
        return -1;

    if (open_files[fd].pid != proc_get_current_pid())
        return -1;

    size_t written = 0;
    uint8_t* ptr = (uint8_t*)buf;

    while (written < count) {
        vfs_dirent_t entry;

        int result = ext2_readdir(open_files[fd].dir_iter, entry.name, &entry.inode, &entry.type);
        if (result < 0)
            break;

        size_t namelen = strlen(entry.name);
        size_t reclen  = sizeof(struct linux_dirent64) + namelen + 1;
        reclen = (reclen + 7) & ~7;

        if (written + reclen > count)
            break;

        struct linux_dirent64* d = (struct linux_dirent64*)ptr;
        d->d_ino    = entry.inode;
        d->d_off    = (int64_t)(written + reclen);
        d->d_reclen = (unsigned short)reclen;
        d->d_type   = entry.type;
        memcpy(d->d_name, entry.name, namelen + 1);

        ptr     += reclen;
        written += reclen;
    }

    return (int)written;
}

int VFS_Set_Pos(int fd, uint32_t pos, bool privileged)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return -1;

    if (!open_files[fd].exists || !open_files[fd].file)
        return -1;

    if (!privileged && (open_files[fd].flags & KERNEL) && !(open_files[fd].flags & USER_WRITE))
        return -1;

    if (!privileged && open_files[fd].pid != proc_get_current_pid())
        return -1;

    int result = ext2_seek(open_files[fd].file, pos, EXT2_SEEK_SET);
    if (result == EXT2_SUCCESS)
        return 0;

    return -1;
}

static void create_special_files(void)
{
    VFS_Create("/dev", true);

    stdin_device_init("/dev/stdin");
    stdout_device_init("/dev/stdout");
    if (VFS_Create("/dev/stderr", false) < 0)
        log_err("VFS", "Failed to create special file 4\n This could just be becuase it already exists");
    if (VFS_Create("/dev/stddbg", false) < 0)
        log_err("VFS", "Failed to create special file 5\n This could just be becuase it already exists");

    if (VFS_Open("/dev/stdin", true) < 0)
        log_err("VFS", "Failed to open special file 1");
    if (VFS_Open("/dev/stdout", true) < 0)
        log_err("VFS", "Failed to open special file 2");
    if (VFS_Open("/dev/stderr", true) < 0)
        log_err("VFS", "Failed to open special file 3");
    if (VFS_Open("/dev/stddbg", true) < 0)
        log_err("VFS", "Failed to open special file 4");

    if (setflags((KERNEL | USER_WRITE), VFS_FD_STDOUT) < 0)
        log_err("VFS", "Failed to set flags special file 1\n This could just be becuase it already exists");
}

void VFS_Init(void)
{
    open_files = kmalloc(MAX_OPEN_FILES * sizeof(VFS_File_t));

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        open_files[i].exists   = false;
        open_files[i].file     = NULL;
        open_files[i].dir_iter = NULL;
        open_files[i].is_dir   = false;
    }

    const char* rd = config_get("bootdisk", "iso");
    if (strcmp(rd, "iso") == 0) {
        log_info("VFS", "Using hdd.img as root");
        
        block_device_t* imgdev = image_create_blockdev("hdaimg", "hdd.img");
        if (!imgdev)
            panic("VFS", "Failed to create block device from hdd.img");
        block_register(imgdev);
        if (!gpt_register_partitions(imgdev))
            panic("VFS", "Failed to parse GPT on hdd.img");
        block_device_t* part = block_get("hdaimgp1");
        if (!part)
            panic("VFS", "Failed to find partition hdaimgp1");
        rootfs = ext2_mount(part);
        if (!rootfs)
            panic("VFS", "Failed to mount ext2 on hdaimgp1");
        rootdrive = part;
        mounted   = true;

        create_special_files();
        return;
    }

    char type[32];
    get_value_by_key(rd, 0, type, sizeof(type));
    if (strcmp(type, "ata") == 0) {
        log_info("VFS", "Using ATA with EXT2");
        log_warn("VFS", "ATA wont work on most systems!");

        ata_init();
        log_ok("VFS", "Started ATA driver");

        block_device_t* bd = ata_create_primary_blockdev("root");
        if (bd == NULL)
            panic("VFS", "Failed to mount root drive\nHELP: Try to mount the drive from another OS and make sure that:\nThe drive is correctly configured in /etc/kernel.conf");

        log_info("VFS", "Sector size: %d", bd->sector_size);

        ext2_fs_t* efs = ext2_mount(bd);
        if (efs == NULL)
            panic("VFS", "Mounted root drive but failed to mount EXT2 fs");

        rootdrive = bd;
        rootfs    = efs;
        mounted   = true;

        create_special_files();
    }
}

void VFS_Unmount(void)
{
    if (mounted)
        ext2_unmount(rootfs);
}

int VFS_Write_old(fd_t file, uint8_t* data, size_t size)
{
    switch (file)
    {
    case VFS_FD_STDIN:
        return 0;
    case VFS_FD_STDOUT:
        for (size_t i = 0; i < size; i++)
        {
            //log_info("CON", "byte 0x%02x", (unsigned)data[i]);
            console_putc(data[i]);
        }
        return size;
    case VFS_FD_STDERR:
        for (size_t i = 0; i < size; i++)
        {
            log_info("CON", "byte 0x%02x", (unsigned)data[i]);
            console_putc(data[i]);
        }
        return size;

    case VFS_FD_DEBUG:
        for (size_t i = 0; i < size; i++)
            e9_putc(data[i]);
        return size;

    default:
        return -1;
    }
}

uint64_t sys_execve(uint64_t path, uint64_t argv, uint64_t envp)
{
    const char  *prog = (const char *)path;
    const char **av   = (const char **)argv;
    const char **ev   = (const char **)envp;

    uid_t caller_uid = proc_get_owner(proc_get_current_pid());
    if (!user_exists(caller_uid))
        return (uint64_t)-1;

    if (!user_is_root(caller_uid)) {
        if (ext2_access(rootfs, prog, caller_uid, ACCESS_EXEC) != EXT2_SUCCESS)
            return (uint64_t)-1;
    }

    int argc = 0;
    int envc = 0;
    if (av) while (av[argc]) argc++;
    if (ev) while (ev[envc]) envc++;

    x86_64_DisableInterrupts();
    int pid = bin_load_elf_argv(prog, 1, proc_get_current_pid(),
                                argc, av, envc, ev);
    if (pid > 0)
        proc_set_owner(pid, caller_uid);
    x86_64_EnableInterrupts();

    return (uint64_t)pid;
}