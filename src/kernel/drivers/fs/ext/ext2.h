#ifndef EXT2_H
#define EXT2_H

#include <stdint.h>
#include <stdbool.h>
#include <user/user.h>

// Forward declarations
typedef struct block_device block_device_t;
typedef struct ext2_filesystem ext2_fs_t;
typedef struct ext2_file ext2_file_t;

// Error codes
#define EXT2_SUCCESS           0
#define EXT2_ERROR            -1
#define EXT2_ERROR_NO_MEM     -2
#define EXT2_ERROR_IO         -3
#define EXT2_ERROR_NOT_FOUND  -4
#define EXT2_ERROR_EXISTS     -5
#define EXT2_ERROR_NO_SPACE   -6
#define EXT2_ERROR_INVALID    -7
#define EXT2_ERROR_IS_DIR     -8
#define EXT2_ERROR_NOT_DIR    -9
#define EXT2_ERROR_NOT_EMPTY  -10
#define EXT2_ERROR_PERM       -11  // Operation not permitted

// EXT2 magic number
#define EXT2_SUPER_MAGIC      0xEF53

// File types
#define EXT2_FT_UNKNOWN       0
#define EXT2_FT_REG_FILE      1
#define EXT2_FT_DIR           2
#define EXT2_FT_CHRDEV        3
#define EXT2_FT_BLKDEV        4
#define EXT2_FT_FIFO          5
#define EXT2_FT_SOCK          6
#define EXT2_FT_SYMLINK       7

// Inode file modes
#define EXT2_S_IFSOCK         0xC000
#define EXT2_S_IFLNK          0xA000
#define EXT2_S_IFREG          0x8000
#define EXT2_S_IFBLK          0x6000
#define EXT2_S_IFDIR          0x4000
#define EXT2_S_IFCHR          0x2000
#define EXT2_S_IFIFO          0x1000

// Permission bits (lower 12 bits of i_mode)
#define EXT2_S_ISUID          0x0800   // Set-user-ID on execute
#define EXT2_S_ISGID          0x0400   // Set-group-ID on execute
#define EXT2_S_ISVTX          0x0200   // Sticky bit

#define EXT2_S_IRUSR          0x0100   // Owner: read
#define EXT2_S_IWUSR          0x0080   // Owner: write
#define EXT2_S_IXUSR          0x0040   // Owner: execute
#define EXT2_S_IRGRP          0x0020   // Group: read
#define EXT2_S_IWGRP          0x0010   // Group: write
#define EXT2_S_IXGRP          0x0008   // Group: execute
#define EXT2_S_IROTH          0x0004   // Other: read
#define EXT2_S_IWOTH          0x0002   // Other: write
#define EXT2_S_IXOTH          0x0001   // Other: execute

// Mask covering all permission + special bits (lower 12 bits)
#define EXT2_S_PERM_MASK      0x0FFF
// Mask covering only the rwxrwxrwx bits (lower 9 bits)
#define EXT2_S_RWXMASK        0x01FF

// Special inodes
#define EXT2_BAD_INO          1
#define EXT2_ROOT_INO         2

// Block pointers in inode
#define EXT2_NDIR_BLOCKS      12
#define EXT2_IND_BLOCK        12
#define EXT2_DIND_BLOCK       13
#define EXT2_TIND_BLOCK       14
#define EXT2_N_BLOCKS         15

// Seek modes
#define EXT2_SEEK_SET         0
#define EXT2_SEEK_CUR         1
#define EXT2_SEEK_END         2

// Directory entry header
typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];  // Variable length
} __attribute__((packed)) ext2_dir_entry_t;

// Superblock structure (starts at byte 1024)
typedef struct {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    // Extended fields
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algo_bitmap;
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_padding1;
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_reserved_char_pad;
    uint16_t s_reserved_word_pad;
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg;
    uint8_t  s_reserved[760];
} __attribute__((packed)) ext2_superblock_t;

// Block group descriptor
typedef struct {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} __attribute__((packed)) ext2_group_desc_t;

// Inode structure
typedef struct {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[EXT2_N_BLOCKS];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} __attribute__((packed)) ext2_inode_t;

// Block cache entry
typedef struct ext2_cache_entry {
    uint32_t block_num;
    uint8_t* data;
    bool dirty;
    uint32_t ref_count;
    struct ext2_cache_entry* next;
    struct ext2_cache_entry* prev;
} ext2_cache_entry_t;

// Filesystem structure
struct ext2_filesystem {
    block_device_t* device;
    ext2_superblock_t superblock;
    ext2_group_desc_t* group_desc;
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t num_groups;
    uint32_t first_data_block;
    
    // Block cache
    ext2_cache_entry_t* cache_head;
    ext2_cache_entry_t* cache_tail;
    uint32_t cache_size;
    uint32_t max_cache_entries;
};

// File handle structure
struct ext2_file {
    ext2_fs_t* fs;
    uint32_t inode_num;
    ext2_inode_t inode;
    uint64_t position;
    bool is_directory;
};

// Directory entry iterator
typedef struct {
    ext2_file_t* dir;
    uint64_t offset;
    ext2_dir_entry_t current;
    char name_buffer[256];
} ext2_dir_iter_t;

// API Functions

// Mount/unmount
ext2_fs_t* ext2_mount(block_device_t* device);
int ext2_unmount(ext2_fs_t* fs);

// File operations
ext2_file_t* ext2_open(ext2_fs_t* fs, const char* path);
int ext2_close(ext2_file_t* file);
int ext2_read(ext2_file_t* file, void* buffer, uint32_t size);
int ext2_write(ext2_file_t* file, const void* buffer, uint32_t size);
int ext2_seek(ext2_file_t* file, int64_t offset, int whence);
uint64_t ext2_tell(ext2_file_t* file);
uint64_t ext2_size(ext2_file_t* file);

// File management
int ext2_create(ext2_fs_t* fs, const char* path, uint16_t mode);
int ext2_delete(ext2_fs_t* fs, const char* path);
int ext2_mkdir(ext2_fs_t* fs, const char* path);
int ext2_rmdir(ext2_fs_t* fs, const char* path);

// Directory operations
ext2_dir_iter_t* ext2_opendir(ext2_fs_t* fs, const char* path);
int ext2_readdir(ext2_dir_iter_t* iter, char* name, uint32_t* inode, uint8_t* type);
int ext2_closedir(ext2_dir_iter_t* iter);

// Utility functions
int ext2_stat(ext2_fs_t* fs, const char* path, ext2_inode_t* inode);
bool ext2_exists(ext2_fs_t* fs, const char* path);

// Permission operations
//   ext2_get_mode  - returns the full i_mode field (file-type bits + permission bits),
//                    or 0 on error.  Use EXT2_S_PERM_MASK to isolate permission bits.
//   ext2_chmod     - replaces the permission bits (EXT2_S_PERM_MASK) of the inode while
//                    leaving the file-type bits untouched.  `mode` should contain only
//                    the lower 12 bits (e.g. 0755, EXT2_S_IRUSR|EXT2_S_IWUSR, …).
uint16_t ext2_get_mode(ext2_fs_t* fs, const char* path);
int      ext2_chmod(ext2_fs_t* fs, const char* path, uint16_t mode);

// Ownership operations
//   ext2_get_owner - fills *uid and *gid from the inode; either pointer may be NULL.
//   ext2_chown     - updates uid and/or gid; pass (uint16_t)-1 to leave a field unchanged.
int ext2_get_owner(ext2_fs_t* fs, const char* path, uint16_t* uid, uint16_t* gid);
int ext2_chown(ext2_fs_t* fs, const char* path, uint16_t uid, uint16_t gid);

// User-system-aware permission layer
//
//   ext2_access   - checks whether `uid` may access the file with the requested
//                   operations.  `mask` is a bitwise-OR of ACCESS_READ, ACCESS_WRITE,
//                   and/or ACCESS_EXEC.  Root bypasses all checks.
//                   Returns EXT2_SUCCESS or EXT2_ERROR_PERM.
//
//   ext2_chmod_as - like ext2_chmod but enforces that `requesting_uid` is either
//                   the file owner or root.  Returns EXT2_ERROR_PERM otherwise.
//
//   ext2_chown_as - like ext2_chown but enforces that `requesting_uid` is root.
//                   Returns EXT2_ERROR_PERM otherwise.
#define ACCESS_READ  4
#define ACCESS_WRITE 2
#define ACCESS_EXEC  1

int ext2_access(ext2_fs_t* fs, const char* path, uid_t requesting_uid, int mask);
int ext2_chmod_as(ext2_fs_t* fs, const char* path, uid_t requesting_uid, uint16_t mode);
int ext2_chown_as(ext2_fs_t* fs, const char* path, uid_t requesting_uid, uint16_t new_uid, uint16_t new_gid);

#endif // EXT2_H