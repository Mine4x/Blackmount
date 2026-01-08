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

#define MAX_NAME 256
#define MAX_CHILDREN 64
#define MAX_DATA 4096

typedef enum {
    EXECUTABLE = 0,
    LINKED_TO_CALLBACK = 1,
} FileFlags;

typedef struct FSNode {
    char name[MAX_NAME];
    int is_dir;
    FileFlags flags;
    FSNode_t* parent;
    
    FSNode_t* children[MAX_CHILDREN];
    int child_count;
    
    char data[MAX_DATA];
    int data_size;
    void (*callback)(void);
} FSNode_t;

#endif