#include "vfs.h"
#include <arch/x86_64/e9.h>
#include <fb/textrenderer.h>
#include <block/block.h>
#include <stdbool.h>
#include <drivers/fs/ext/ext2.h>
#include <drivers/disk/ata.h>
#include <drivers/fs/ramdisk.h>
#include <config/config.h>
#include <heap.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <debug.h>
#include <panic/panic.h>

block_device_t* rootdrive;
ext2_fs_t* rootfs;
disk_type_t rootDriveType;

bool mounted = false;

VFS_File_t *open_files;

int get_value_by_key(const char *input, int key, char *out, size_t out_size)
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

int VFS_Create(const char* path, bool isDir)
{
    if (rootDriveType == RAMDISK) {
        if (!isDir)
            return ramdisk_create_file(path);
        
        return ramdisk_create_dir(path);
    }

    if (rootDriveType == DISK) {
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
    
    return -1;
}

int VFS_Open(const char* path)
{
    if (rootDriveType == RAMDISK) {
        int exists = ramdisk_fs_exists(path);
        int is_file = ramdisk_fs_is_file(path);

        if (!exists || !is_file)
            return -10;
        
        
        for (int i = 0; i < MAX_OPEN_FILES; i++) {
            if (!open_files[i].exists)
            {
                open_files[i].path = path;
                open_files[i].exists = true;
                open_files[i].disk_type = RAMDISK;
                open_files[i].file = NULL;

                return i;
            }
        }
    }

    if (rootDriveType == DISK) {
        ext2_file_t* ext2_file = ext2_open(rootfs, path);
        if (!ext2_file)
            return -1;
        
        for (int i = 0; i < MAX_OPEN_FILES; i++) {
            if (!open_files[i].exists)
            {
                open_files[i].path = path;
                open_files[i].file = ext2_file;
                open_files[i].exists = true;
                open_files[i].disk_type = DISK;

                return i;
            }
        }
        
        ext2_close(ext2_file);
    }

    return -1;
}

int VFS_Write(int fd, size_t count, void *buf)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return -1;
    
    if (!open_files[fd].exists)
        return -1;
    
    if (open_files[fd].disk_type == RAMDISK)
    {
        return ramdisk_write_file(open_files[fd].path, buf, count);
    }
    
    if (open_files[fd].disk_type == DISK)
    {
        if (!open_files[fd].file)
            return -1;
        
        int result = ext2_write(open_files[fd].file, buf, (uint32_t)count);
        if (result < 0)
            return -1;
        
        return result;
    }

    return -1;
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
    
    if (open_files[fd].disk_type == DISK)
    {
        if (!open_files[fd].file)
            return -1;
        
        int result = ext2_read(open_files[fd].file, buf, (uint32_t)count);
        if (result < 0)
            return -1;
        
        return result;
    }

    return -1;
}

int VFS_Close(int fd)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return -1;
    
    if (open_files[fd].exists) {
        if (open_files[fd].disk_type == DISK && open_files[fd].file) {
            ext2_close(open_files[fd].file);
            open_files[fd].file = NULL;
        }
        
        open_files[fd].exists = false;
        return 0;
    }

    return -1;
}

static void create_special_files()
{
    if (VFS_Create("/dev", true) < 0)
        panic("VFS", "Failed to create special files 1");
    if (VFS_Create("/dev/stdin", false) < 0)
        panic("VFS", "Failed to create special files 2");
    if (VFS_Create("/dev/stdout", false) < 0)
        panic("VFS", "Failed to create special files 3");
    if (VFS_Create("/dev/stderr", false) < 0)
        panic("VFS", "Failed to create special files 4");
    if (VFS_Create("/dev/stddbg", false) < 0)
        panic("VFS", "Failed to create special files 5");
    
    if (VFS_Open("/dev/stdin") < 0)
        panic("VFS", "Failed to open special files 1");
    if (VFS_Open("/dev/stdout") < 0)
        panic("VFS", "Failed to open special files 2");
    if (VFS_Open("/dev/stderr") < 0)
        panic("VFS", "Failed to open special files 3");
    if (VFS_Open("/dev/stddbg") < 0)
        panic("VFS", "Failed to open special files 4");
}

void VFS_Init(void)
{
    open_files = kmalloc(MAX_OPEN_FILES * sizeof(VFS_File_t));

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        open_files[i].exists = false;
        open_files[i].file = NULL;
    }

    const char* rd = config_get("bootdisk", "iso");
    if(strcmp(rd, "iso") == 0) {
        log_info("VFS", "Using ramdisk");
        ramdisk_init_fs();
        rootDriveType = RAMDISK;

        create_special_files();

        return;
    }

    char type[32];
    get_value_by_key(rd, 0, type, sizeof(type));
    if (strcmp(type, "ata") == 0) {
        log_info("VFS", "Using ATA with EXT2");
        block_device_t* bd = ata_create_primary_blockdev("root");
        if (bd == NULL) {
            panic("VFS", "Failed to mount root drive\nHELP: Try to mount the drive from another OS and make sure that:\nThe drive is correctly configured in /etc/kernel.conf");
        }

        log_info("VFS", "Sector size: %d", bd->sector_size);
        
        ext2_fs_t* efs = ext2_mount(bd);
        if (efs == NULL) {
            panic("VFS", "Mounted root drive but failed to mount EXT2 fs");
        }

        rootdrive = bd;
        rootfs = efs;
        rootDriveType = DISK;
        mounted = true;

        create_special_files();
    }
}

void VFS_Unmount(void)
{
    if (mounted)
    {
        ext2_unmount(rootfs);
    }
}

int VFS_Set_Pos(int fd, uint32_t pos)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return -1;
    
    if (open_files[fd].exists && open_files[fd].disk_type == DISK) {
        if (!open_files[fd].file)
            return -1;
        
        int result = ext2_seek(open_files[fd].file, pos, EXT2_SEEK_SET);
        if (result == EXT2_SUCCESS)
            return 0;
        
        return -1;
    }

    return -1;
}

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