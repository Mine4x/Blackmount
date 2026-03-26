#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <drivers/fs/ext/ext2.h>
#include <device/device.h>
#include <user/user.h>

typedef int fd_t;

typedef enum {
    RAMDISK = 0,
    DISK    = 1,
    IMAGE   = 2,
} disk_type_t;

typedef enum {
    USER_WRITE = 0,
    KERNEL     = 1,
} file_flags_t;

typedef struct {
    char     name[256];
    uint32_t inode;
    uint8_t  type;
} vfs_dirent_t;

struct linux_dirent64 {
    uint64_t       d_ino;
    int64_t        d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[];
} __attribute__((packed));

typedef struct {
    char            mountpoint[256];
    ext2_fs_t*      fs;
    block_device_t* dev;
    bool            active;
} vfs_mount_t;

typedef struct {
    const char*      path;
    ext2_file_t*     file;
    ext2_dir_iter_t* dir_iter;
    ext2_fs_t*       fs;
    disk_type_t      disk_type;
    file_flags_t     flags;
    int              pid;
    uid_t            owner;
    bool             exists;
    bool             is_dir;
    device_t*        dev;
    bool             is_dev;
    bool             write_all;
} VFS_File_t;

#define VFS_FD_STDIN    0
#define VFS_FD_STDOUT   1
#define VFS_FD_STDERR   2
#define VFS_FD_DEBUG    3
#define MAX_OPEN_FILES  100
#define MAX_MOUNTS      16

int  VFS_Create(const char* path, bool isDir);
int  VFS_Open(const char* path, bool privileged);
int  VFS_Read(int fd, size_t count, void* buf);
int  VFS_Write(int fd, size_t count, void* buf, bool privileged);
int  VFS_Close(int fd, bool privileged);
int  VFS_Set_Pos(int fd, uint32_t pos, bool privileged);
int  VFS_GetDents64(int fd, struct linux_dirent64* buf, size_t count);
int  VFS_ioctl(int fd, uint64_t req, void* arg);
// DEPERECATED
int  VFS_Write_old(fd_t file, uint8_t* data, size_t size);
int  VFS_Mount(const char* source, const char* target);
int  VFS_Unmount_Path(const char* target);
void VFS_Init(void);
void VFS_Unmount(void);

uint64_t sys_execve(uint64_t path, uint64_t argv, uint64_t envp);