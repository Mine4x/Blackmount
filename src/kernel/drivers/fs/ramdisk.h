#ifndef RAMDISK_H
#define RAMDISK_H
#include <drivers/fs/fs.h>
#include <debug.h>
#include <heap.h>
#include <string.h>
#include <memory.h>

int ramdisk_init();
static FSNode_t* ramdisk_find_node(const char* path, FSNode_t** parent_out);
static void ramdisk_get_basename(const char* path, char* out);
int ramdisk_create_dir(const char* path);
int ramdisk_get_dir_cont(const char* path, char* buffer, int max_size);
int ramdisk_delete_dir(const char* path);
int ramdisk_create_file(const char* path);
int ramdisk_execute_file(const char* path);
int ramdisk_delete_file(const char* path);
int ramdisk_write_file(const char* path, const char* data, int size);
int ramdisk_set_file_callback(const char* path, void (*callback)(void));
int ramdisk_read_file(const char* path, char* buffer, int max_size);
int ramdisk_exists(const char* path);
int ramdisk_is_dir(const char* path);
int ramdisk_is_file(const char* path);
int ramdisk_is_exec(const char* path);

#endif