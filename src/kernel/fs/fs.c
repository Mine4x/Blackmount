#include "fs.h"
#include "memory.h"
#include "stdio.h"
#include "debug.h"

#define FS_MODULE "FS"

extern void* kmalloc(unsigned int size);
extern void kfree(void* ptr);

static int str_len(const char* s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

static int str_cmp(const char* s1, const char* s2) {
    int i = 0;
    while (s1[i] && s2[i] && s1[i] == s2[i]) i++;
    return s1[i] - s2[i];
}

static void str_cpy(char* dst, const char* src) {
    int i = 0;
    while (src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

#define MAX_NAME 256
#define MAX_CHILDREN 64
#define MAX_DATA 4096

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

typedef struct FSNode FSNode;

struct FSNode {
    char name[MAX_NAME];
    int is_dir;
    FileFlags flags;
    FSNode* parent;
    
    FSNode* children[MAX_CHILDREN];
    int child_count;
    
    char data[MAX_DATA];
    int data_size;
    void (*callback)(void);
};

static FSNode* root = 0;

void init_fs() {
    log_info(FS_MODULE, "Initializing filesystem");
    
    root = (FSNode*)kmalloc(sizeof(FSNode));
    if (!root) {
        log_crit(FS_MODULE, "Failed to allocate root node");
        return;
    }
    
    str_cpy(root->name, "/");
    root->is_dir = 1;
    root->flags = 0;
    root->parent = 0;
    root->child_count = 0;
    root->data_size = 0;
    root->callback = 0;
    memset(root->children, 0, sizeof(root->children));
    
    log_ok(FS_MODULE, "Filesystem initialized successfully");
}

static FSNode* find_node(const char* path, FSNode** parent_out) {
    if (!root) {
        log_err(FS_MODULE, "Filesystem not initialized");
        return 0;
    }
    
    if (!path || path[0] != '/') {
        log_err(FS_MODULE, "Invalid path: %s", path ? path : "(null)");
        return 0;
    }
    
    if (path[1] == 0) {
        if (parent_out) *parent_out = 0;
        return root;
    }
    
    FSNode* cur = root;
    FSNode* parent = 0;
    int start = 1;
    
    while (path[start]) {
        int end = start;
        while (path[end] && path[end] != '/') end++;
        
        int len = end - start;
        if (len == 0) {
            start = end + 1;
            continue;
        }
        
        int found = 0;
        for (int i = 0; i < cur->child_count; i++) {
            FSNode* child = cur->children[i];
            int match = 1;
            for (int j = 0; j < len; j++) {
                if (child->name[j] != path[start + j]) {
                    match = 0;
                    break;
                }
            }
            if (match && child->name[len] == 0) {
                parent = cur;
                cur = child;
                found = 1;
                break;
            }
        }
        
        if (!found) {
            if (parent_out) *parent_out = cur;
            return 0;
        }
        
        if (path[end] == 0) break;
        start = end + 1;
    }
    
    if (parent_out) *parent_out = parent;
    return cur;
}

static void get_basename(const char* path, char* out) {
    int len = str_len(path);
    int i = len - 1;
    
    while (i > 0 && path[i] == '/') i--;
    
    int end = i + 1;
    while (i > 0 && path[i] != '/') i--;
    
    int start = (path[i] == '/') ? i + 1 : i;
    int j = 0;
    for (int k = start; k < end; k++) {
        out[j++] = path[k];
    }
    out[j] = 0;
}

int create_dir(const char* path) {
    if (!root || !path) {
        log_err(FS_MODULE, "Invalid parameters for create_dir");
        return FS_INVALID_PARAM;
    }
    
    FSNode* parent = 0;
    FSNode* existing = find_node(path, &parent);
    
    if (existing) {
        return FS_EXISTS;
    }
    
    if (!parent) {
        log_err(FS_MODULE, "Parent directory not found for: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (parent->child_count >= MAX_CHILDREN) {
        log_err(FS_MODULE, "Parent directory full (max %d children)", MAX_CHILDREN);
        return FS_DIR_FULL;
    }
    
    FSNode* new_dir = (FSNode*)kmalloc(sizeof(FSNode));
    if (!new_dir) {
        log_crit(FS_MODULE, "Failed to allocate memory for directory");
        return FS_ERROR;
    }
    
    get_basename(path, new_dir->name);
    new_dir->is_dir = 1;
    new_dir->flags = 0;
    new_dir->parent = parent;
    new_dir->child_count = 0;
    new_dir->data_size = 0;
    new_dir->callback = 0;
    memset(new_dir->children, 0, sizeof(new_dir->children));
    memset(new_dir->data, 0, sizeof(new_dir->data));
    
    parent->children[parent->child_count++] = new_dir;
    return FS_SUCCESS;
}

int get_dir_cont(const char* path, char* buffer, int max_size) {
    FSNode* node = find_node(path, 0);
    if (!node) {
        log_err(FS_MODULE, "Directory not found: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (!node->is_dir) {
        log_err(FS_MODULE, "Not a directory: %s", path);
        return FS_NOT_DIR;
    }
    
    int offset = 0;
    for (int i = 0; i < node->child_count; i++) {
        FSNode* child = node->children[i];
        int name_len = str_len(child->name);
        int suffix_len = child->is_dir ? 2 : 1;  // "/" or "\n"
        
        if (offset + name_len + suffix_len >= max_size) {
            break;
        }
        
        str_cpy(buffer + offset, child->name);
        offset += name_len;
        
        if (child->is_dir) {
            buffer[offset++] = '/';
        }
        buffer[offset++] = '\n';
    }
    
    if (offset > 0 && buffer[offset - 1] == '\n') {
        buffer[offset - 1] = 0;
        offset--;
    } else {
        buffer[offset] = 0;
    }
    
    return offset;
}

int delete_dir(const char* path) {
    FSNode* parent = 0;
    FSNode* node = find_node(path, &parent);
    
    if (!node) {
        log_err(FS_MODULE, "Directory not found: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (!node->is_dir) {
        log_err(FS_MODULE, "Not a directory: %s", path);
        return FS_NOT_DIR;
    }
    
    if (node == root) {
        log_err(FS_MODULE, "Cannot delete root directory");
        return FS_ERROR;
    }
    
    if (node->child_count > 0) {
        log_err(FS_MODULE, "Directory not empty: %s (%d items)", path, node->child_count);
        return FS_DIR_NOT_EMPTY;
    }
    
    for (int i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == node) {
            for (int j = i; j < parent->child_count - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->child_count--;
            break;
        }
    }
    
    kfree(node);
    return FS_SUCCESS;
}

int create_file(const char* path) {
    if (!root || !path) {
        log_err(FS_MODULE, "Invalid parameters for create_file");
        return FS_INVALID_PARAM;
    }
    
    FSNode* parent = 0;
    FSNode* existing = find_node(path, &parent);
    
    if (existing) {
        return FS_EXISTS;
    }
    
    if (!parent || !parent->is_dir) {
        log_err(FS_MODULE, "Parent directory not found for: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (parent->child_count >= MAX_CHILDREN) {
        log_err(FS_MODULE, "Parent directory full (max %d children)", MAX_CHILDREN);
        return FS_DIR_FULL;
    }
    
    FSNode* new_file = (FSNode*)kmalloc(sizeof(FSNode));
    if (!new_file) {
        log_crit(FS_MODULE, "Failed to allocate memory for file");
        return FS_ERROR;
    }
    
    get_basename(path, new_file->name);
    new_file->is_dir = 0;
    new_file->flags = EXECUTABLE;
    new_file->parent = parent;
    new_file->child_count = 0;
    new_file->data_size = 0;
    new_file->callback = 0;
    memset(new_file->children, 0, sizeof(new_file->children));
    memset(new_file->data, 0, sizeof(new_file->data));
    
    parent->children[parent->child_count++] = new_file;
    return FS_SUCCESS;
}

int execute_file(const char* path) {
    FSNode* node = find_node(path, 0);
    if (!node) {
        log_err(FS_MODULE, "File not found: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (node->is_dir) {
        log_err(FS_MODULE, "Cannot execute directory: %s", path);
        return FS_NOT_FILE;
    }
    
    if (node->flags == LINKED_TO_CALLBACK && node->callback) {
        node->callback();
        return FS_SUCCESS;
    } else if (node->flags == EXECUTABLE && node->data_size > 0) {
        void (*exec)(void) = (void(*)(void))node->data;
        exec();
        return FS_SUCCESS;
    } else {
        log_err(FS_MODULE, "File has no executable content: %s", path);
        return FS_NO_EXEC;
    }
}

int delete_file(const char* path) {
    FSNode* parent = 0;
    FSNode* node = find_node(path, &parent);
    
    if (!node) {
        log_err(FS_MODULE, "File not found: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (node->is_dir) {
        log_err(FS_MODULE, "Cannot delete directory as file: %s", path);
        return FS_NOT_DIR;
    }
    
    if (!parent) {
        log_err(FS_MODULE, "Cannot delete file without parent");
        return FS_ERROR;
    }
    
    for (int i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == node) {
            for (int j = i; j < parent->child_count - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->child_count--;
            break;
        }
    }
    
    kfree(node);
    return FS_SUCCESS;
}

int write_file(const char* path, const char* data, int size) {
    FSNode* node = find_node(path, 0);
    if (!node) {
        log_err(FS_MODULE, "File not found: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (node->is_dir) {
        log_err(FS_MODULE, "Cannot write to directory: %s", path);
        return FS_NOT_FILE;
    }
    
    int copy_size = (size > MAX_DATA) ? MAX_DATA : size;
    
    memcpy(node->data, data, copy_size);
    node->data_size = copy_size;
    return copy_size;
}

int set_file_callback(const char* path, void (*callback)(void)) {
    FSNode* node = find_node(path, 0);
    if (!node) {
        log_err(FS_MODULE, "File not found: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (node->is_dir) {
        log_err(FS_MODULE, "Cannot set callback on directory: %s", path);
        return FS_NOT_DIR;
    }
    
    node->flags = LINKED_TO_CALLBACK;
    node->callback = callback;
    return FS_SUCCESS;
}

int read_file(const char* path, char* buffer, int max_size) {
    FSNode* node = find_node(path, 0);
    if (!node) {
        log_err(FS_MODULE, "File not found: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (node->is_dir) {
        log_err(FS_MODULE, "Cannot read directory: %s", path);
        return FS_NOT_FILE;
    }
    
    int copy_size = (node->data_size > max_size) ? max_size : node->data_size;
    memcpy(buffer, node->data, copy_size);
    return copy_size;
}