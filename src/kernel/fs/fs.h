#ifndef FS_H
#define FS_H

typedef enum {
    EXECUTABLE = 0,
    LINKED_TO_CALLBACK = 1,
} FileFlags;

void init_fs();

void create_dir(const char* path);

void get_dir_cont(const char* path);

void delete_dir(const char* path);

void create_file(const char* path);

void execute_file(const char* path);

void delete_file(const char* path);

void write_file(const char* path, const char* data, int size);

int read_file(const char* path, char* buffer, int max_size);

void set_file_callback(const char* path, void (*callback)(void));

#endif