#include "ramdisk.h"
#include <drivers/fs/fs.h>
#include <debug.h>
#include <heap.h>
#include <string.h>
#include <memory.h>

#define MODULE "RamDisk"

static FSNode_t* root = 0;

int ramdisk_init() {
    root = (FSNode_t*)kmalloc(sizeof(FSNode_t));
    if (!root) {
        log_err(MODULE, "Failed to allocate root node");
        return -1;
    }

    str_cpy(root->name, "/");
    root->is_dir = 1;
    root->flags = 0;
    root->parent = 0;
    root->child_count = 0;
    root->data_size = 0;
    root->callback = 0;
    memset(root->children, 0, sizeof(root->children));

    return -2;
}

static FSNode_t* ramdisk_find_node(const char* path, FSNode_t** parent_out) {
    if (!root) {
        log_err(MODULE, "Filesystem not initialized");
        return 0;
    }
    
    if (!path || path[0] != '/') {
        log_err(MODULE, "Invalid path: %s", path ? path : "(null)");
        return 0;
    }
    
    if (path[1] == 0) {
        if (parent_out) *parent_out = 0;
        return root;
    }
    
    FSNode_t* cur = root;
    FSNode_t* parent = 0;
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
            FSNode_t* child = cur->children[i];
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

static void ramdisk_get_basename(const char* path, char* out) {
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

int ramdisk_create_dir(const char* path) {
    if (!root || !path) {
        log_err(MODULE, "Invalid parameters for ramdisk_create_dir");
        return FS_INVALID_PARAM;
    }
    
    FSNode_t* parent = 0;
    FSNode_t* existing = ramdisk_find_node(path, &parent);
    
    if (existing) {
        return ramdisk_exists;
    }
    
    if (!parent) {
        log_err(MODULE, "Parent directory not found for: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (parent->child_count >= MAX_CHILDREN) {
        log_err(MODULE, "Parent directory full (max %d children)", MAX_CHILDREN);
        return FS_DIR_FULL;
    }
    
    FSNode_t* new_dir = (FSNode_t*)kmalloc(sizeof(FSNode_t));
    if (!new_dir) {
        log_crit(MODULE, "Failed to allocate memory for directory");
        return FS_ERROR;
    }
    
    ramdisk_get_basename(path, new_dir->name);
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

int ramdisk_get_dir_cont(const char* path, char* buffer, int max_size) {
    FSNode_t* node = ramdisk_find_node(path, 0);
    if (!node) {
        log_err(MODULE, "Directory not found: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (!node->is_dir) {
        log_err(MODULE, "Not a directory: %s", path);
        return FS_NOT_DIR;
    }
    
    int offset = 0;
    for (int i = 0; i < node->child_count; i++) {
        FSNode_t* child = node->children[i];
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

int ramdisk_delete_dir(const char* path) {
    FSNode_t* parent = 0;
    FSNode_t* node = ramdisk_find_node(path, &parent);
    
    if (!node) {
        log_err(MODULE, "Directory not found: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (!node->is_dir) {
        log_err(MODULE, "Not a directory: %s", path);
        return FS_NOT_DIR;
    }
    
    if (node == root) {
        log_err(MODULE, "Cannot delete root directory");
        return FS_ERROR;
    }
    
    if (node->child_count > 0) {
        log_err(MODULE, "Directory not empty: %s (%d items)", path, node->child_count);
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

int ramdisk_create_file(const char* path) {
    if (!root || !path) {
        log_err(MODULE, "Invalid parameters for ramdisk_create_file");
        return FS_INVALID_PARAM;
    }
    
    FSNode_t* parent = 0;
    FSNode_t* existing = ramdisk_find_node(path, &parent);
    
    if (existing) {
        return ramdisk_exists;
    }
    
    if (!parent || !parent->is_dir) {
        log_err(MODULE, "Parent directory not found for: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (parent->child_count >= MAX_CHILDREN) {
        log_err(MODULE, "Parent directory full (max %d children)", MAX_CHILDREN);
        return FS_DIR_FULL;
    }
    
    FSNode_t* new_file = (FSNode_t*)kmalloc(sizeof(FSNode_t));
    if (!new_file) {
        log_crit(MODULE, "Failed to allocate memory for file");
        return FS_ERROR;
    }
    
    ramdisk_get_basename(path, new_file->name);
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

int ramdisk_execute_file(const char* path) {
    FSNode_t* node = ramdisk_find_node(path, 0);
    if (!node) {
        log_err(MODULE, "File not found: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (node->is_dir) {
        log_err(MODULE, "Cannot execute directory: %s", path);
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
        log_err(MODULE, "File has no executable content: %s", path);
        return FS_NO_EXEC;
    }
}

int ramdisk_delete_file(const char* path) {
    FSNode_t* parent = 0;
    FSNode_t* node = ramdisk_find_node(path, &parent);
    
    if (!node) {
        log_err(MODULE, "File not found: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (node->is_dir) {
        log_err(MODULE, "Cannot delete directory as file: %s", path);
        return FS_NOT_DIR;
    }
    
    if (!parent) {
        log_err(MODULE, "Cannot delete file without parent");
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

int ramdisk_write_file(const char* path, const char* data, int size) {
    FSNode_t* node = ramdisk_find_node(path, 0);
    if (!node) {
        log_err(MODULE, "File not found: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (node->is_dir) {
        log_err(MODULE, "Cannot write to directory: %s", path);
        return FS_NOT_FILE;
    }
    
    int copy_size = (size > MAX_DATA) ? MAX_DATA : size;
    
    memcpy(node->data, data, copy_size);
    node->data_size = copy_size;
    return copy_size;
}

int ramdisk_set_file_callback(const char* path, void (*callback)(void)) {
    FSNode_t* node = ramdisk_find_node(path, 0);
    if (!node) {
        log_err(MODULE, "File not found: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (node->is_dir) {
        log_err(MODULE, "Cannot set callback on directory: %s", path);
        return FS_NOT_DIR;
    }
    
    node->flags = LINKED_TO_CALLBACK;
    node->callback = callback;
    return FS_SUCCESS;
}

int ramdisk_read_file(const char* path, char* buffer, int max_size) {
    FSNode_t* node = ramdisk_find_node(path, 0);
    if (!node) {
        log_err(MODULE, "File not found: %s", path);
        return FS_NOT_FOUND;
    }
    
    if (node->is_dir) {
        log_err(MODULE, "Cannot read directory: %s", path);
        return FS_NOT_FILE;
    }
    
    int copy_size = (node->data_size > max_size) ? max_size : node->data_size;
    memcpy(buffer, node->data, copy_size);
    return copy_size;
}

// Returns 1 if the path exists, 0 otherwise
int ramdisk_exists(const char* path) {
    FSNode_t* node = ramdisk_find_node(path, 0);
    return node ? 1 : 0;
}

// Returns 1 if the path exists and is a directory, 0 otherwise
int ramdisk_is_dir(const char* path) {
    FSNode_t* node = ramdisk_find_node(path, 0);
    return (node && node->is_dir) ? 1 : 0;
}

// Returns 1 if the path exists and is a file, 0 otherwise
int ramdisk_is_file(const char* path) {
    FSNode_t* node = ramdisk_find_node(path, 0);
    return (node && !node->is_dir) ? 1 : 0;
}

int ramdisk_is_exec(const char* path) {
    return 1; /*TODO*/
}