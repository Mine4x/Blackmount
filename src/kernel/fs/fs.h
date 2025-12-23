#ifndef FS_H
#define FS_H

#define FS_SUCCESS 0
#define FS_ERROR -1
#define FS_EXISTS -2
#define FS_NOT_FOUND -3
#define FS_INVALID_PARAM -4
#define FS_NOT_DIR -5
#define FS_NOT_FILE -6
#define FS_DIR_FULL -7
#define FS_DIR_NOT_EMPTY -8
#define FS_NO_EXEC -9

typedef enum {
    EXECUTABLE = 0,
    LINKED_TO_CALLBACK = 1,
} FileFlags;

void init_fs();
int create_dir(const char* path);
int get_dir_cont(const char* path, char* buffer, int max_size);
int delete_dir(const char* path);
int create_file(const char* path);
int execute_file(const char* path);
int delete_file(const char* path);
int write_file(const char* path, const char* data, int size);
int read_file(const char* path, char* buffer, int max_size);
int set_file_callback(const char* path, void (*callback)(void));
int fs_exists(const char* path);
int fs_is_dir(const char* path);
int fs_is_file(const char* path);

#endif