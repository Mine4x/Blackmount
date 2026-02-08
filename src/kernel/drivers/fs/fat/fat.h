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
    uint32_t dir_sector;
    uint32_t dir_offset;
} fat_file_t;

typedef struct __attribute__((packed)) {
    char     name[11];
    uint8_t  attr;
    uint8_t  nt_reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t cluster_low;
    uint32_t size;
} fat_dir_entry_t;

fat_fs_t* fat_mount(block_device_t* dev);
void fat_unmount(fat_fs_t* fs);

bool fat_open(fat_fs_t* fs, const char* path, fat_file_t* out);
uint32_t fat_read(fat_file_t* file, void* buffer, uint32_t bytes);
uint32_t fat_write(fat_file_t* file, const void* buffer, uint32_t bytes);
bool fat_create(fat_fs_t* fs, const char* name, fat_file_t* out);