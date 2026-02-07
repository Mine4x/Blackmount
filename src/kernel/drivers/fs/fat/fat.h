#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <block/block.h>

typedef enum {
    FAT_NONE,
    FAT12,
    FAT16,
    FAT32
} fat_type_t;

typedef struct fat_fs fat_fs_t;

typedef struct {
    fat_fs_t* fs;
    uint32_t cluster;
    uint32_t size;
    uint32_t pos;
} fat_file_t;

fat_fs_t* fat_mount(block_device_t* dev);
void fat_unmount(fat_fs_t* fs);

bool fat_open(fat_fs_t* fs, const char* path, fat_file_t* out);
uint32_t fat_read(fat_file_t* file, void* buffer, uint32_t bytes);
