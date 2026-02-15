#include "vfs.h"
#include <arch/x86_64/e9.h>
#include <fb/textrenderer.h>
#include <block/block.h>
#include <stdbool.h>
#include <drivers/fs/fat/fat.h>
#include <drivers/fs/ramdisk.h>
#include <config/config.h>
#include <heap.h>
#include <string.h>
#include <stdio.h>

block_device_t* rootdrive;
fat_fs_t* rootfs;
disk_type_t rootDriveType;

VFS_File_t *open_files;

int VFS_Create(const char* path, bool isDir)
{
    if (rootDriveType == RAMDISK) {
        if (!isDir)
            return ramdisk_create_file(path);
        
        return ramdisk_create_dir(path);
    }
}

int VFS_Open(const char* path)
{
    if (rootDriveType == RAMDISK) {
        int exists = ramdisk_fs_exists(path);
        int is_file = ramdisk_fs_is_file(path);

        if (!exists || !is_file)
            return -1;
        
        
        for (int i = 0; i < MAX_OPEN_FILES; i++) {
            if (!open_files[i].exists)
            {
                open_files[i].path = path;
                open_files[i].exists = true;

                return i;
            }
        }
    }

    return -1;
}

int VFS_Write(int fd, size_t count, void *buf)
{
    if (fd < 0 || fd > MAX_OPEN_FILES)
        return -1;
    
    if (!open_files[fd].exists)
        return -1;
    
    if (open_files[fd].disk_type == RAMDISK)
    {
        return ramdisk_write_file(open_files[fd].path, buf, count);
    }
}

int VFS_Read(int fd, size_t count, void *buf)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return -1;

    if (!open_files[fd].exists)
        return -1;
    
    if (open_files[fd].disk_type == RAMDISK)
    {
        return ramdisk_read_file(open_files[fd].path, buf, count);
    }

    return -1;
}

int VFS_Close(int fd)
{
    if (fd < 0 || fd > MAX_OPEN_FILES)
        return -1;
    
    if (open_files[fd].exists) {
        open_files[fd].file = NULL;
        open_files[fd].path = NULL;
        open_files[fd].exists = false;

        return 0;
    }

    return -1;
}

void VFS_Init(void)
{
    open_files = kmalloc(MAX_OPEN_FILES * sizeof(VFS_File_t));

    for (int i = 0; i < MAX_OPEN_FILES; i++)
        open_files[i].exists = false;

    const char* rd = config_get("bootdisk", "iso");
    if(strcmp(rd, "iso")) {
        ramdisk_init_fs();
        rootDriveType = RAMDISK;

        return;
    }
    // TODO: Implement actuall drives
}

// For compatiblitiy with stdio
int VFS_Write_old(fd_t file, uint8_t* data, size_t size)
{
    switch (file)
    {
    case VFS_FD_STDIN:
        return 0;
    case VFS_FD_STDOUT:
    case VFS_FD_STDERR:
        for (size_t i = 0; i < size; i++)
            tr_putc(data[i]);
        return size;

    case VFS_FD_DEBUG:
        for (size_t i = 0; i < size; i++)
            e9_putc(data[i]);
        return size;

    default:
        return -1;
    }
}