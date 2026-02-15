#include "vfs.h"
#include <arch/x86_64/e9.h>
#include <fb/textrenderer.h>
#include <block/block.h>
#include <stdbool.h>
#include <drivers/fs/fat/fat.h>
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
fat_fs_t* rootfs;
disk_type_t rootDriveType;

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
        return -1; // key not found

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
        if (isDir)
            return -1; // UNIMPLEMENTED
        
        fat_file_t temp_file;
        bool response = fat_create(rootfs, path, &temp_file);
        if (response == false)
            return -1;
        if (response == true)
            return 0;
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
                open_files[i].disk_type = RAMDISK;

                return i;
            }
        }
    }

    if (rootDriveType == DISK) {
        fat_file_t fat_file;
        if (!fat_open(rootfs, path, &fat_file))
            return -1;
        
        for (int i = 0; i < MAX_OPEN_FILES; i++) {
            if (!open_files[i].exists)
            {
                open_files[i].path = path;
                open_files[i].file = fat_file;
                open_files[i].exists = true;
                open_files[i].disk_type = DISK;

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
    if (open_files[fd].disk_type == DISK)
    {
        uint32_t response = fat_write(&open_files[fd].file, buf, (uint32_t)count);
        return response;
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
        return fat_read(&open_files[fd].file, buf, (uint32_t)count);
    }

    return -1;
}

int VFS_Close(int fd)
{
    if (fd < 0 || fd > MAX_OPEN_FILES)
        return -1;
    
    if (open_files[fd].exists) {
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
    if(strcmp(rd, "iso") == 0) {
        log_info("VFS", "Using ramdisk");
        ramdisk_init_fs();
        rootDriveType = RAMDISK;

        return;
    }

    char type[32];
    get_value_by_key(rd, 0, type, sizeof(type));
    if (strcmp(type, "ata") == 0) {
        log_info("VFS", "Using ATA");
        block_device_t* bd = ata_create_primary_blockdev("root");
        if (bd == NULL) {
            panic("VFS", "Couldn't mount root drive\nHELP: Try to mount the drive from another OS and make sure that:\nThe drive is correctly configured in /etc/kernel.conf");
        }
        fat_fs_t* ffs = fat_mount(bd);
        if (ffs == NULL) {
            panic("VFS", "Mounted root drive but couldn't mount FAT fs");
        }

        rootdrive = bd;
        rootfs = ffs;
        rootDriveType = DISK;
    }
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