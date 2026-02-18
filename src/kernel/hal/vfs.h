#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <drivers/fs/ext/ext2.h>

typedef int fd_t;

typedef enum {
    RAMDISK = 0,
    DISK = 1,
    IMAGE = 2,
} disk_type_t;

typedef enum {
    USER_WRITE = 0,
    KERNEL = 1,
} file_flags_t;

typedef struct {
    const char* path;
    ext2_file_t* file; // For disks ONLY (EXT2 file handle)
    disk_type_t disk_type;
    file_flags_t flags;
    int pid;
    bool exists;
} VFS_File_t;


#define VFS_FD_STDIN    0
#define VFS_FD_STDOUT   1
#define VFS_FD_STDERR   2
#define VFS_FD_DEBUG    3

#define MAX_OPEN_FILES 50

int VFS_Create(const char* path, bool isDir);
int VFS_Open(const char* path, bool privileged);
int VFS_Read(int fd, size_t count, void *buf);
int VFS_Close(int fd, bool privileged);
void VFS_Init(void);
int VFS_Write(int fd, size_t count, void *buf, bool privileged);
int VFS_Set_Pos(int fd, uint32_t pos, bool privileged);
void VFS_Unmount(void);

int VFS_Write_old(fd_t file, uint8_t* data, size_t size);