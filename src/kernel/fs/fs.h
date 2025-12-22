#pragma once

typedef enum {
    EXECUTABLE = 0,
    LINKED_TO_CALLBACK = 1,
} FileFlags;

void init_fs();
void create_dir(const char* path);
void get_dir_cont(const char*);
void delete_dir(const char* path);
void create_file(const char* path); //TODO
void function_to_bin(const char* path, void(*callback)()); //For functions that the user can call and are in the Kernel only
void execute_file(const char* path);
void delete_file(const char* path);