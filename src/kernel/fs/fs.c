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
        log_debug(FS_MODULE, "Found root node");
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
            log_debug(FS_MODULE, "Path not found: %s", path);
            if (parent_out) *parent_out = cur;
            return 0;
        }
        
        if (path[end] == 0) break;
        start = end + 1;
    }
    
    log_debug(FS_MODULE, "Found node: %s", path);
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

void create_dir(const char* path) {
    log_info(FS_MODULE, "Creating directory: %s", path);
    
    if (!root || !path) {
        log_err(FS_MODULE, "Invalid parameters for create_dir");
        return;
    }
    
    FSNode* parent = 0;
    FSNode* existing = find_node(path, &parent);
    
    if (existing) {
        log_warn(FS_MODULE, "Directory already exists: %s", path);
        return;
    }
    
    if (!parent) {
        log_err(FS_MODULE, "Parent directory not found for: %s", path);
        return;
    }
    
    if (parent->child_count >= MAX_CHILDREN) {
        log_err(FS_MODULE, "Parent directory full (max %d children)", MAX_CHILDREN);
        return;
    }
    
    FSNode* new_dir = (FSNode*)kmalloc(sizeof(FSNode));
    if (!new_dir) {
        log_crit(FS_MODULE, "Failed to allocate memory for directory");
        return;
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
    log_ok(FS_MODULE, "Directory created: %s", path);
}

void get_dir_cont(const char* path) {
    log_debug(FS_MODULE, "Listing directory: %s", path);
    
    FSNode* node = find_node(path, 0);
    if (!node) {
        log_err(FS_MODULE, "Directory not found: %s", path);
        return;
    }
    
    if (!node->is_dir) {
        log_err(FS_MODULE, "Not a directory: %s", path);
        return;
    }
    
    log_info(FS_MODULE, "Contents of %s (%d items):", path, node->child_count);
    for (int i = 0; i < node->child_count; i++) {
        FSNode* child = node->children[i];
        printf("%s", child->name);
        if (child->is_dir) printf("/");
        printf("\n");
    }
}

void delete_dir(const char* path) {
    log_info(FS_MODULE, "Deleting directory: %s", path);
    
    FSNode* parent = 0;
    FSNode* node = find_node(path, &parent);
    
    if (!node) {
        log_err(FS_MODULE, "Directory not found: %s", path);
        return;
    }
    
    if (!node->is_dir) {
        log_err(FS_MODULE, "Not a directory: %s", path);
        return;
    }
    
    if (node == root) {
        log_err(FS_MODULE, "Cannot delete root directory");
        return;
    }
    
    if (node->child_count > 0) {
        log_err(FS_MODULE, "Directory not empty: %s (%d items)", path, node->child_count);
        return;
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
    log_ok(FS_MODULE, "Directory deleted: %s", path);
}

void create_file(const char* path) {
    log_info(FS_MODULE, "Creating file: %s", path);
    
    if (!root || !path) {
        log_err(FS_MODULE, "Invalid parameters for create_file");
        return;
    }
    
    FSNode* parent = 0;
    FSNode* existing = find_node(path, &parent);
    
    if (existing) {
        log_warn(FS_MODULE, "File already exists: %s", path);
        return;
    }
    
    if (!parent || !parent->is_dir) {
        log_err(FS_MODULE, "Parent directory not found for: %s", path);
        return;
    }
    
    if (parent->child_count >= MAX_CHILDREN) {
        log_err(FS_MODULE, "Parent directory full (max %d children)", MAX_CHILDREN);
        return;
    }
    
    FSNode* new_file = (FSNode*)kmalloc(sizeof(FSNode));
    if (!new_file) {
        log_crit(FS_MODULE, "Failed to allocate memory for file");
        return;
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
    log_ok(FS_MODULE, "File created: %s", path);
}

void execute_file(const char* path) {
    log_info(FS_MODULE, "Executing file: %s", path);
    
    FSNode* node = find_node(path, 0);
    if (!node) {
        log_err(FS_MODULE, "File not found: %s", path);
        return;
    }
    
    if (node->is_dir) {
        log_err(FS_MODULE, "Cannot execute directory: %s", path);
        return;
    }
    
    if (node->flags == LINKED_TO_CALLBACK && node->callback) {
        log_debug(FS_MODULE, "Executing callback for: %s", path);
        node->callback();
        log_ok(FS_MODULE, "Callback executed: %s", path);
    } else if (node->flags == EXECUTABLE && node->data_size > 0) {
        log_debug(FS_MODULE, "Executing binary for: %s", path);
        void (*exec)(void) = (void(*)(void))node->data;
        exec();
        log_ok(FS_MODULE, "Binary executed: %s", path);
    } else {
        log_warn(FS_MODULE, "File has no executable content: %s", path);
    }
}

void delete_file(const char* path) {
    log_info(FS_MODULE, "Deleting file: %s", path);
    
    FSNode* parent = 0;
    FSNode* node = find_node(path, &parent);
    
    if (!node) {
        log_err(FS_MODULE, "File not found: %s", path);
        return;
    }
    
    if (node->is_dir) {
        log_err(FS_MODULE, "Cannot delete directory as file: %s", path);
        return;
    }
    
    if (!parent) {
        log_err(FS_MODULE, "Cannot delete file without parent");
        return;
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
    log_ok(FS_MODULE, "File deleted: %s", path);
}

void write_file(const char* path, const char* data, int size) {
    log_info(FS_MODULE, "Writing %d bytes to: %s", size, path);
    
    FSNode* node = find_node(path, 0);
    if (!node) {
        log_err(FS_MODULE, "File not found: %s", path);
        return;
    }
    
    if (node->is_dir) {
        log_err(FS_MODULE, "Cannot write to directory: %s", path);
        return;
    }
    
    int copy_size = (size > MAX_DATA) ? MAX_DATA : size;
    if (size > MAX_DATA) {
        log_warn(FS_MODULE, "Data truncated from %d to %d bytes", size, MAX_DATA);
    }
    
    memcpy(node->data, data, copy_size);
    node->data_size = copy_size;
    log_ok(FS_MODULE, "Wrote %d bytes to: %s", copy_size, path);
}

void set_file_callback(const char* path, void (*callback)(void)) {
    log_info(FS_MODULE, "Setting callback for: %s", path);
    
    FSNode* node = find_node(path, 0);
    if (!node) {
        log_err(FS_MODULE, "File not found: %s", path);
        return;
    }
    
    if (node->is_dir) {
        log_err(FS_MODULE, "Cannot set callback on directory: %s", path);
        return;
    }
    
    node->flags = LINKED_TO_CALLBACK;
    node->callback = callback;
    log_ok(FS_MODULE, "Callback set for: %s", path);
}

int read_file(const char* path, char* buffer, int max_size) {
    log_debug(FS_MODULE, "Reading file: %s (max %d bytes)", path, max_size);
    
    FSNode* node = find_node(path, 0);
    if (!node) {
        log_err(FS_MODULE, "File not found: %s", path);
        return 0;
    }
    
    if (node->is_dir) {
        log_err(FS_MODULE, "Cannot read directory: %s", path);
        return 0;
    }
    
    int copy_size = (node->data_size > max_size) ? max_size : node->data_size;
    memcpy(buffer, node->data, copy_size);
    log_ok(FS_MODULE, "Read %d bytes from: %s", copy_size, path);
    return copy_size;
}