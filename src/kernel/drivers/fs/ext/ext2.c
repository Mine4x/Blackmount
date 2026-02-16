#include "ext2.h"
#include <block/block.h>
#include <heap.h>
#include <memory.h>
#include <string.h>

// Cache configuration
#define EXT2_CACHE_SIZE 64

// Helper macros
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))

// forward declarations
static int ext2_read_block(ext2_fs_t* fs, uint32_t block_num, void* buffer);
static int ext2_write_block(ext2_fs_t* fs, uint32_t block_num, const void* buffer);
static uint8_t* ext2_get_block(ext2_fs_t* fs, uint32_t block_num);
static void ext2_put_block(ext2_fs_t* fs, uint8_t* block, bool dirty);
static int ext2_read_inode(ext2_fs_t* fs, uint32_t inode_num, ext2_inode_t* inode);
static int ext2_write_inode(ext2_fs_t* fs, uint32_t inode_num, const ext2_inode_t* inode);
static uint32_t ext2_get_block_num(ext2_fs_t* fs, ext2_inode_t* inode, uint32_t block_index, bool allocate);
static int ext2_alloc_block(ext2_fs_t* fs, uint32_t* block_num);
static int ext2_free_block(ext2_fs_t* fs, uint32_t block_num);
static int ext2_alloc_inode(ext2_fs_t* fs, uint32_t parent_inode, bool is_dir, uint32_t* inode_num);
static int ext2_free_inode(ext2_fs_t* fs, uint32_t inode_num);
static int ext2_lookup(ext2_fs_t* fs, uint32_t dir_inode, const char* name, uint32_t* result_inode);
static int ext2_add_dir_entry(ext2_fs_t* fs, uint32_t dir_inode, const char* name, uint32_t inode, uint8_t file_type);
static int ext2_remove_dir_entry(ext2_fs_t* fs, uint32_t dir_inode, const char* name);
static int ext2_resolve_path(ext2_fs_t* fs, const char* path, uint32_t* inode_num, char* last_component);

static ext2_cache_entry_t* ext2_cache_find(ext2_fs_t* fs, uint32_t block_num) {
    ext2_cache_entry_t* entry = fs->cache_head;
    while (entry) {
        if (entry->block_num == block_num) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

static void ext2_cache_remove(ext2_fs_t* fs, ext2_cache_entry_t* entry) {
    if (entry->prev) {
        entry->prev->next = entry->next;
    } else {
        fs->cache_head = entry->next;
    }
    
    if (entry->next) {
        entry->next->prev = entry->prev;
    } else {
        fs->cache_tail = entry->prev;
    }
    
    fs->cache_size--;
}

static void ext2_cache_add_front(ext2_fs_t* fs, ext2_cache_entry_t* entry) {
    entry->next = fs->cache_head;
    entry->prev = NULL;
    
    if (fs->cache_head) {
        fs->cache_head->prev = entry;
    } else {
        fs->cache_tail = entry;
    }
    
    fs->cache_head = entry;
    fs->cache_size++;
}

static void ext2_cache_move_front(ext2_fs_t* fs, ext2_cache_entry_t* entry) {
    if (entry == fs->cache_head) {
        return;
    }
    
    ext2_cache_remove(fs, entry);
    ext2_cache_add_front(fs, entry);
}

static int ext2_cache_flush_entry(ext2_fs_t* fs, ext2_cache_entry_t* entry) {
    if (!entry->dirty) {
        return EXT2_SUCCESS;
    }
    
    int result = ext2_write_block(fs, entry->block_num, entry->data);
    if (result == EXT2_SUCCESS) {
        entry->dirty = false;
    }
    return result;
}

static int ext2_cache_evict(ext2_fs_t* fs) {
    // Evict the least recently used block (tail)
    ext2_cache_entry_t* entry = fs->cache_tail;
    
    while (entry) {
        if (entry->ref_count == 0) {
            int result = ext2_cache_flush_entry(fs, entry);
            if (result != EXT2_SUCCESS) {
                return result;
            }
            
            ext2_cache_remove(fs, entry);
            kfree(entry->data);
            kfree(entry);
            return EXT2_SUCCESS;
        }
        entry = entry->prev;
    }
    
    return EXT2_ERROR_NO_MEM;
}

static int ext2_read_block(ext2_fs_t* fs, uint32_t block_num, void* buffer) {
    uint32_t sectors_per_block = fs->block_size / fs->device->sector_size;
    uint64_t lba = block_num * sectors_per_block;
    
    if (!fs->device->read(fs->device, lba, sectors_per_block, buffer)) {
        return EXT2_ERROR_IO;
    }
    
    return EXT2_SUCCESS;
}

static int ext2_write_block(ext2_fs_t* fs, uint32_t block_num, const void* buffer) {
    uint32_t sectors_per_block = fs->block_size / fs->device->sector_size;
    uint64_t lba = block_num * sectors_per_block;
    
    if (!fs->device->write(fs->device, lba, sectors_per_block, buffer)) {
        return EXT2_ERROR_IO;
    }
    
    return EXT2_SUCCESS;
}

static uint8_t* ext2_get_block(ext2_fs_t* fs, uint32_t block_num) {
    // Check cache
    ext2_cache_entry_t* entry = ext2_cache_find(fs, block_num);
    if (entry) {
        entry->ref_count++;
        ext2_cache_move_front(fs, entry);
        return entry->data;
    }
    
    // Need to allocate new cache entry
    if (fs->cache_size >= fs->max_cache_entries) {
        if (ext2_cache_evict(fs) != EXT2_SUCCESS) {
            return NULL;
        }
    }
    
    entry = (ext2_cache_entry_t*)kmalloc(sizeof(ext2_cache_entry_t));
    if (!entry) {
        return NULL;
    }
    
    entry->data = (uint8_t*)kmalloc(fs->block_size);
    if (!entry->data) {
        kfree(entry);
        return NULL;
    }
    
    if (ext2_read_block(fs, block_num, entry->data) != EXT2_SUCCESS) {
        kfree(entry->data);
        kfree(entry);
        return NULL;
    }
    
    entry->block_num = block_num;
    entry->dirty = false;
    entry->ref_count = 1;
    
    ext2_cache_add_front(fs, entry);
    
    return entry->data;
}

static void ext2_put_block(ext2_fs_t* fs, uint8_t* block, bool dirty) {
    // Find the cache entry
    ext2_cache_entry_t* entry = fs->cache_head;
    while (entry) {
        if (entry->data == block) {
            entry->ref_count--;
            if (dirty) {
                entry->dirty = true;
            }
            return;
        }
        entry = entry->next;
    }
}

static int ext2_flush_cache(ext2_fs_t* fs) {
    ext2_cache_entry_t* entry = fs->cache_head;
    while (entry) {
        if (entry->dirty) {
            int result = ext2_cache_flush_entry(fs, entry);
            if (result != EXT2_SUCCESS) {
                return result;
            }
        }
        entry = entry->next;
    }
    return EXT2_SUCCESS;
}

static bool ext2_test_bit(const uint8_t* bitmap, uint32_t bit) {
    return (bitmap[bit / 8] & (1 << (bit % 8))) != 0;
}

static void ext2_set_bit(uint8_t* bitmap, uint32_t bit) {
    bitmap[bit / 8] |= (1 << (bit % 8));
}

static void ext2_clear_bit(uint8_t* bitmap, uint32_t bit) {
    bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static int ext2_read_inode(ext2_fs_t* fs, uint32_t inode_num, ext2_inode_t* inode) {
    if (inode_num == 0 || inode_num > fs->superblock.s_inodes_count) {
        return EXT2_ERROR_INVALID;
    }
    
    // Inode numbers start at 1
    inode_num--;
    
    uint32_t group = inode_num / fs->superblock.s_inodes_per_group;
    uint32_t index = inode_num % fs->superblock.s_inodes_per_group;
    
    uint32_t inode_table = fs->group_desc[group].bg_inode_table;
    uint32_t block = inode_table + (index * fs->inode_size) / fs->block_size;
    uint32_t offset = (index * fs->inode_size) % fs->block_size;
    
    uint8_t* block_data = ext2_get_block(fs, block);
    if (!block_data) {
        return EXT2_ERROR_IO;
    }
    
    memcpy(inode, block_data + offset, sizeof(ext2_inode_t));
    ext2_put_block(fs, block_data, false);
    
    return EXT2_SUCCESS;
}

static int ext2_write_inode(ext2_fs_t* fs, uint32_t inode_num, const ext2_inode_t* inode) {
    if (inode_num == 0 || inode_num > fs->superblock.s_inodes_count) {
        return EXT2_ERROR_INVALID;
    }
    
    // Inode numbers start at 1
    inode_num--;
    
    uint32_t group = inode_num / fs->superblock.s_inodes_per_group;
    uint32_t index = inode_num % fs->superblock.s_inodes_per_group;
    
    uint32_t inode_table = fs->group_desc[group].bg_inode_table;
    uint32_t block = inode_table + (index * fs->inode_size) / fs->block_size;
    uint32_t offset = (index * fs->inode_size) % fs->block_size;
    
    uint8_t* block_data = ext2_get_block(fs, block);
    if (!block_data) {
        return EXT2_ERROR_IO;
    }
    
    memcpy(block_data + offset, inode, sizeof(ext2_inode_t));
    ext2_put_block(fs, block_data, true);
    
    return EXT2_SUCCESS;
}

static int ext2_alloc_block(ext2_fs_t* fs, uint32_t* block_num) {
    for (uint32_t group = 0; group < fs->num_groups; group++) {
        if (fs->group_desc[group].bg_free_blocks_count == 0) {
            continue;
        }
        
        uint32_t bitmap_block = fs->group_desc[group].bg_block_bitmap;
        uint8_t* bitmap = ext2_get_block(fs, bitmap_block);
        if (!bitmap) {
            return EXT2_ERROR_IO;
        }
        
        uint32_t blocks_in_group = fs->superblock.s_blocks_per_group;
        if (group == fs->num_groups - 1) {
            blocks_in_group = fs->superblock.s_blocks_count - 
                            (group * fs->superblock.s_blocks_per_group);
        }
        
        for (uint32_t i = 0; i < blocks_in_group; i++) {
            if (!ext2_test_bit(bitmap, i)) {
                ext2_set_bit(bitmap, i);
                ext2_put_block(fs, bitmap, true);
                
                *block_num = group * fs->superblock.s_blocks_per_group + 
                           fs->first_data_block + i;
                
                fs->group_desc[group].bg_free_blocks_count--;
                fs->superblock.s_free_blocks_count--;
                
                // Write updated group descriptor
                uint32_t gd_block = fs->first_data_block + 1;
                uint8_t* gd_data = ext2_get_block(fs, gd_block);
                if (gd_data) {
                    memcpy(gd_data + group * sizeof(ext2_group_desc_t),
                          &fs->group_desc[group], sizeof(ext2_group_desc_t));
                    ext2_put_block(fs, gd_data, true);
                }
                
                return EXT2_SUCCESS;
            }
        }
        
        ext2_put_block(fs, bitmap, false);
    }
    
    return EXT2_ERROR_NO_SPACE;
}

static int ext2_free_block(ext2_fs_t* fs, uint32_t block_num) {
    if (block_num < fs->first_data_block || block_num >= fs->superblock.s_blocks_count) {
        return EXT2_ERROR_INVALID;
    }
    
    block_num -= fs->first_data_block;
    uint32_t group = block_num / fs->superblock.s_blocks_per_group;
    uint32_t index = block_num % fs->superblock.s_blocks_per_group;
    
    uint32_t bitmap_block = fs->group_desc[group].bg_block_bitmap;
    uint8_t* bitmap = ext2_get_block(fs, bitmap_block);
    if (!bitmap) {
        return EXT2_ERROR_IO;
    }
    
    ext2_clear_bit(bitmap, index);
    ext2_put_block(fs, bitmap, true);
    
    fs->group_desc[group].bg_free_blocks_count++;
    fs->superblock.s_free_blocks_count++;
    
    // Write updated group descriptor
    uint32_t gd_block = fs->first_data_block + 1;
    uint8_t* gd_data = ext2_get_block(fs, gd_block);
    if (gd_data) {
        memcpy(gd_data + group * sizeof(ext2_group_desc_t),
              &fs->group_desc[group], sizeof(ext2_group_desc_t));
        ext2_put_block(fs, gd_data, true);
    }
    
    return EXT2_SUCCESS;
}

static int ext2_alloc_inode(ext2_fs_t* fs, uint32_t parent_inode, bool is_dir, uint32_t* inode_num) {
    uint32_t preferred_group = 0;
    if (parent_inode > 0) {
        preferred_group = (parent_inode - 1) / fs->superblock.s_inodes_per_group;
    }
    
    // Try preferred group first
    for (uint32_t attempt = 0; attempt < fs->num_groups; attempt++) {
        uint32_t group = (preferred_group + attempt) % fs->num_groups;
        
        if (fs->group_desc[group].bg_free_inodes_count == 0) {
            continue;
        }
        
        uint32_t bitmap_block = fs->group_desc[group].bg_inode_bitmap;
        uint8_t* bitmap = ext2_get_block(fs, bitmap_block);
        if (!bitmap) {
            return EXT2_ERROR_IO;
        }
        
        for (uint32_t i = 0; i < fs->superblock.s_inodes_per_group; i++) {
            if (!ext2_test_bit(bitmap, i)) {
                ext2_set_bit(bitmap, i);
                ext2_put_block(fs, bitmap, true);
                
                *inode_num = group * fs->superblock.s_inodes_per_group + i + 1;
                
                fs->group_desc[group].bg_free_inodes_count--;
                fs->superblock.s_free_inodes_count--;
                if (is_dir) {
                    fs->group_desc[group].bg_used_dirs_count++;
                }
                
                // Write updated group descriptor
                uint32_t gd_block = fs->first_data_block + 1;
                uint8_t* gd_data = ext2_get_block(fs, gd_block);
                if (gd_data) {
                    memcpy(gd_data + group * sizeof(ext2_group_desc_t),
                          &fs->group_desc[group], sizeof(ext2_group_desc_t));
                    ext2_put_block(fs, gd_data, true);
                }
                
                return EXT2_SUCCESS;
            }
        }
        
        ext2_put_block(fs, bitmap, false);
    }
    
    return EXT2_ERROR_NO_SPACE;
}

static int ext2_free_inode(ext2_fs_t* fs, uint32_t inode_num) {
    if (inode_num == 0 || inode_num > fs->superblock.s_inodes_count) {
        return EXT2_ERROR_INVALID;
    }
    
    ext2_inode_t inode;
    if (ext2_read_inode(fs, inode_num, &inode) != EXT2_SUCCESS) {
        return EXT2_ERROR_IO;
    }
    
    bool is_dir = (inode.i_mode & EXT2_S_IFDIR) != 0;
    
    inode_num--;
    uint32_t group = inode_num / fs->superblock.s_inodes_per_group;
    uint32_t index = inode_num % fs->superblock.s_inodes_per_group;
    
    uint32_t bitmap_block = fs->group_desc[group].bg_inode_bitmap;
    uint8_t* bitmap = ext2_get_block(fs, bitmap_block);
    if (!bitmap) {
        return EXT2_ERROR_IO;
    }
    
    ext2_clear_bit(bitmap, index);
    ext2_put_block(fs, bitmap, true);
    
    fs->group_desc[group].bg_free_inodes_count++;
    fs->superblock.s_free_inodes_count++;
    if (is_dir) {
        fs->group_desc[group].bg_used_dirs_count--;
    }
    
    // Write updated group descriptor
    uint32_t gd_block = fs->first_data_block + 1;
    uint8_t* gd_data = ext2_get_block(fs, gd_block);
    if (gd_data) {
        memcpy(gd_data + group * sizeof(ext2_group_desc_t),
              &fs->group_desc[group], sizeof(ext2_group_desc_t));
        ext2_put_block(fs, gd_data, true);
    }
    
    return EXT2_SUCCESS;
}

static uint32_t ext2_get_block_num(ext2_fs_t* fs, ext2_inode_t* inode, 
                                   uint32_t block_index, bool allocate) {
    uint32_t ptrs_per_block = fs->block_size / 4;
    
    // Direct blocks
    if (block_index < EXT2_NDIR_BLOCKS) {
        if (inode->i_block[block_index] == 0 && allocate) {
            uint32_t new_block;
            if (ext2_alloc_block(fs, &new_block) == EXT2_SUCCESS) {
                inode->i_block[block_index] = new_block;
                
                // Zero out the new block
                uint8_t* block_data = ext2_get_block(fs, new_block);
                if (block_data) {
                    memset(block_data, 0, fs->block_size);
                    ext2_put_block(fs, block_data, true);
                }
            }
        }
        return inode->i_block[block_index];
    }
    
    block_index -= EXT2_NDIR_BLOCKS;
    
    // Single indirect
    if (block_index < ptrs_per_block) {
        if (inode->i_block[EXT2_IND_BLOCK] == 0) {
            if (!allocate) return 0;
            uint32_t new_block;
            if (ext2_alloc_block(fs, &new_block) != EXT2_SUCCESS) return 0;
            inode->i_block[EXT2_IND_BLOCK] = new_block;
            
            uint8_t* block_data = ext2_get_block(fs, new_block);
            if (block_data) {
                memset(block_data, 0, fs->block_size);
                ext2_put_block(fs, block_data, true);
            }
        }
        
        uint8_t* ind_block = ext2_get_block(fs, inode->i_block[EXT2_IND_BLOCK]);
        if (!ind_block) return 0;
        
        uint32_t* ptrs = (uint32_t*)ind_block;
        uint32_t block_num = ptrs[block_index];
        
        if (block_num == 0 && allocate) {
            uint32_t new_block;
            if (ext2_alloc_block(fs, &new_block) == EXT2_SUCCESS) {
                ptrs[block_index] = new_block;
                ext2_put_block(fs, ind_block, true);
                
                uint8_t* block_data = ext2_get_block(fs, new_block);
                if (block_data) {
                    memset(block_data, 0, fs->block_size);
                    ext2_put_block(fs, block_data, true);
                }
                
                return new_block;
            }
        } else {
            ext2_put_block(fs, ind_block, false);
        }
        
        return block_num;
    }
    
    block_index -= ptrs_per_block;
    
    // Double indirect
    if (block_index < ptrs_per_block * ptrs_per_block) {
        if (inode->i_block[EXT2_DIND_BLOCK] == 0) {
            if (!allocate) return 0;
            uint32_t new_block;
            if (ext2_alloc_block(fs, &new_block) != EXT2_SUCCESS) return 0;
            inode->i_block[EXT2_DIND_BLOCK] = new_block;
            
            uint8_t* block_data = ext2_get_block(fs, new_block);
            if (block_data) {
                memset(block_data, 0, fs->block_size);
                ext2_put_block(fs, block_data, true);
            }
        }
        
        uint32_t ind1_idx = block_index / ptrs_per_block;
        uint32_t ind2_idx = block_index % ptrs_per_block;
        
        uint8_t* dind_block = ext2_get_block(fs, inode->i_block[EXT2_DIND_BLOCK]);
        if (!dind_block) return 0;
        
        uint32_t* dptrs = (uint32_t*)dind_block;
        uint32_t ind_block_num = dptrs[ind1_idx];
        
        if (ind_block_num == 0 && allocate) {
            if (ext2_alloc_block(fs, &ind_block_num) == EXT2_SUCCESS) {
                dptrs[ind1_idx] = ind_block_num;
                ext2_put_block(fs, dind_block, true);
                
                uint8_t* block_data = ext2_get_block(fs, ind_block_num);
                if (block_data) {
                    memset(block_data, 0, fs->block_size);
                    ext2_put_block(fs, block_data, true);
                }
            }
        } else {
            ext2_put_block(fs, dind_block, false);
        }
        
        if (ind_block_num == 0) return 0;
        
        uint8_t* ind_block = ext2_get_block(fs, ind_block_num);
        if (!ind_block) return 0;
        
        uint32_t* ptrs = (uint32_t*)ind_block;
        uint32_t block_num = ptrs[ind2_idx];
        
        if (block_num == 0 && allocate) {
            uint32_t new_block;
            if (ext2_alloc_block(fs, &new_block) == EXT2_SUCCESS) {
                ptrs[ind2_idx] = new_block;
                ext2_put_block(fs, ind_block, true);
                
                uint8_t* block_data = ext2_get_block(fs, new_block);
                if (block_data) {
                    memset(block_data, 0, fs->block_size);
                    ext2_put_block(fs, block_data, true);
                }
                
                return new_block;
            }
        } else {
            ext2_put_block(fs, ind_block, false);
        }
        
        return block_num;
    }
    
    block_index -= ptrs_per_block * ptrs_per_block;
    
    // Triple indirect
    if (block_index < ptrs_per_block * ptrs_per_block * ptrs_per_block) {
        if (inode->i_block[EXT2_TIND_BLOCK] == 0) {
            if (!allocate) return 0;
            uint32_t new_block;
            if (ext2_alloc_block(fs, &new_block) != EXT2_SUCCESS) return 0;
            inode->i_block[EXT2_TIND_BLOCK] = new_block;
            
            uint8_t* block_data = ext2_get_block(fs, new_block);
            if (block_data) {
                memset(block_data, 0, fs->block_size);
                ext2_put_block(fs, block_data, true);
            }
        }
        
        uint32_t ind1_idx = block_index / (ptrs_per_block * ptrs_per_block);
        uint32_t ind2_idx = (block_index / ptrs_per_block) % ptrs_per_block;
        uint32_t ind3_idx = block_index % ptrs_per_block;
        
        // Get first level indirect block
        uint8_t* tind_block = ext2_get_block(fs, inode->i_block[EXT2_TIND_BLOCK]);
        if (!tind_block) return 0;
        
        uint32_t* tptrs = (uint32_t*)tind_block;
        uint32_t dind_block_num = tptrs[ind1_idx];
        
        if (dind_block_num == 0 && allocate) {
            if (ext2_alloc_block(fs, &dind_block_num) == EXT2_SUCCESS) {
                tptrs[ind1_idx] = dind_block_num;
                ext2_put_block(fs, tind_block, true);
                
                uint8_t* block_data = ext2_get_block(fs, dind_block_num);
                if (block_data) {
                    memset(block_data, 0, fs->block_size);
                    ext2_put_block(fs, block_data, true);
                }
            }
        } else {
            ext2_put_block(fs, tind_block, false);
        }
        
        if (dind_block_num == 0) return 0;
        
        // Get second level indirect block
        uint8_t* dind_block = ext2_get_block(fs, dind_block_num);
        if (!dind_block) return 0;
        
        uint32_t* dptrs = (uint32_t*)dind_block;
        uint32_t ind_block_num = dptrs[ind2_idx];
        
        if (ind_block_num == 0 && allocate) {
            if (ext2_alloc_block(fs, &ind_block_num) == EXT2_SUCCESS) {
                dptrs[ind2_idx] = ind_block_num;
                ext2_put_block(fs, dind_block, true);
                
                uint8_t* block_data = ext2_get_block(fs, ind_block_num);
                if (block_data) {
                    memset(block_data, 0, fs->block_size);
                    ext2_put_block(fs, block_data, true);
                }
            }
        } else {
            ext2_put_block(fs, dind_block, false);
        }
        
        if (ind_block_num == 0) return 0;
        
        // Get data block
        uint8_t* ind_block = ext2_get_block(fs, ind_block_num);
        if (!ind_block) return 0;
        
        uint32_t* ptrs = (uint32_t*)ind_block;
        uint32_t block_num = ptrs[ind3_idx];
        
        if (block_num == 0 && allocate) {
            uint32_t new_block;
            if (ext2_alloc_block(fs, &new_block) == EXT2_SUCCESS) {
                ptrs[ind3_idx] = new_block;
                ext2_put_block(fs, ind_block, true);
                
                uint8_t* block_data = ext2_get_block(fs, new_block);
                if (block_data) {
                    memset(block_data, 0, fs->block_size);
                    ext2_put_block(fs, block_data, true);
                }
                
                return new_block;
            }
        } else {
            ext2_put_block(fs, ind_block, false);
        }
        
        return block_num;
    }
    
    return 0;
}

static int ext2_lookup(ext2_fs_t* fs, uint32_t dir_inode_num, 
                      const char* name, uint32_t* result_inode) {
    ext2_inode_t dir_inode;
    if (ext2_read_inode(fs, dir_inode_num, &dir_inode) != EXT2_SUCCESS) {
        return EXT2_ERROR_IO;
    }
    
    if ((dir_inode.i_mode & EXT2_S_IFDIR) == 0) {
        return EXT2_ERROR_NOT_DIR;
    }
    
    uint32_t dir_size = dir_inode.i_size;
    uint32_t offset = 0;
    
    while (offset < dir_size) {
        uint32_t block_index = offset / fs->block_size;
        uint32_t block_offset = offset % fs->block_size;
        
        uint32_t block_num = ext2_get_block_num(fs, &dir_inode, block_index, false);
        if (block_num == 0) {
            break;
        }
        
        uint8_t* block_data = ext2_get_block(fs, block_num);
        if (!block_data) {
            return EXT2_ERROR_IO;
        }
        
        ext2_dir_entry_t* entry = (ext2_dir_entry_t*)(block_data + block_offset);
        
        if (entry->inode != 0 && entry->name_len == strlen(name)) {
            if (memcmp(entry->name, name, entry->name_len) == 0) {
                *result_inode = entry->inode;
                ext2_put_block(fs, block_data, false);
                return EXT2_SUCCESS;
            }
        }
        
        offset += entry->rec_len;
        ext2_put_block(fs, block_data, false);
    }
    
    return EXT2_ERROR_NOT_FOUND;
}

static int ext2_add_dir_entry(ext2_fs_t* fs, uint32_t dir_inode_num, 
                             const char* name, uint32_t inode, uint8_t file_type) {
    ext2_inode_t dir_inode;
    if (ext2_read_inode(fs, dir_inode_num, &dir_inode) != EXT2_SUCCESS) {
        return EXT2_ERROR_IO;
    }
    
    if ((dir_inode.i_mode & EXT2_S_IFDIR) == 0) {
        return EXT2_ERROR_NOT_DIR;
    }
    
    uint32_t name_len = strlen(name);
    uint32_t required_len = ALIGN_UP(8 + name_len, 4);
    
    uint32_t dir_size = dir_inode.i_size;
    uint32_t offset = 0;
    
    // Try to find space in existing blocks
    while (offset < dir_size) {
        uint32_t block_index = offset / fs->block_size;
        uint32_t block_offset = offset % fs->block_size;
        
        uint32_t block_num = ext2_get_block_num(fs, &dir_inode, block_index, false);
        if (block_num == 0) {
            break;
        }
        
        uint8_t* block_data = ext2_get_block(fs, block_num);
        if (!block_data) {
            return EXT2_ERROR_IO;
        }
        
        ext2_dir_entry_t* entry = (ext2_dir_entry_t*)(block_data + block_offset);
        uint32_t real_len = ALIGN_UP(8 + entry->name_len, 4);
        uint32_t extra_space = entry->rec_len - real_len;
        
        if (extra_space >= required_len) {
            // Found space, split the entry
            entry->rec_len = real_len;
            
            ext2_dir_entry_t* new_entry = (ext2_dir_entry_t*)(block_data + block_offset + real_len);
            new_entry->inode = inode;
            new_entry->rec_len = extra_space;
            new_entry->name_len = name_len;
            new_entry->file_type = file_type;
            memcpy(new_entry->name, name, name_len);
            
            ext2_put_block(fs, block_data, true);
            return EXT2_SUCCESS;
        }
        
        offset += entry->rec_len;
        ext2_put_block(fs, block_data, false);
    }
    
    // Need to allocate a new block
    uint32_t block_index = dir_size / fs->block_size;
    uint32_t block_num = ext2_get_block_num(fs, &dir_inode, block_index, true);
    if (block_num == 0) {
        return EXT2_ERROR_NO_SPACE;
    }
    
    uint8_t* block_data = ext2_get_block(fs, block_num);
    if (!block_data) {
        return EXT2_ERROR_IO;
    }
    
    ext2_dir_entry_t* entry = (ext2_dir_entry_t*)block_data;
    entry->inode = inode;
    entry->rec_len = fs->block_size;
    entry->name_len = name_len;
    entry->file_type = file_type;
    memcpy(entry->name, name, name_len);
    
    ext2_put_block(fs, block_data, true);
    
    dir_inode.i_size += fs->block_size;
    ext2_write_inode(fs, dir_inode_num, &dir_inode);
    
    return EXT2_SUCCESS;
}

static int ext2_remove_dir_entry(ext2_fs_t* fs, uint32_t dir_inode_num, const char* name) {
    ext2_inode_t dir_inode;
    if (ext2_read_inode(fs, dir_inode_num, &dir_inode) != EXT2_SUCCESS) {
        return EXT2_ERROR_IO;
    }
    
    if ((dir_inode.i_mode & EXT2_S_IFDIR) == 0) {
        return EXT2_ERROR_NOT_DIR;
    }
    
    uint32_t dir_size = dir_inode.i_size;
    uint32_t offset = 0;
    ext2_dir_entry_t* prev_entry = NULL;
    uint8_t* prev_block = NULL;
    uint32_t prev_block_num = 0;
    
    while (offset < dir_size) {
        uint32_t block_index = offset / fs->block_size;
        uint32_t block_offset = offset % fs->block_size;
        
        uint32_t block_num = ext2_get_block_num(fs, &dir_inode, block_index, false);
        if (block_num == 0) {
            break;
        }
        
        uint8_t* block_data = ext2_get_block(fs, block_num);
        if (!block_data) {
            return EXT2_ERROR_IO;
        }
        
        ext2_dir_entry_t* entry = (ext2_dir_entry_t*)(block_data + block_offset);
        
        if (entry->inode != 0 && entry->name_len == strlen(name)) {
            if (memcmp(entry->name, name, entry->name_len) == 0) {
                // Found the entry to remove
                if (prev_entry) {
                    // Merge with previous entry
                    prev_entry->rec_len += entry->rec_len;
                    if (prev_block) {
                        ext2_put_block(fs, prev_block, true);
                    }
                    ext2_put_block(fs, block_data, false);
                } else {
                    // First entry in block, just mark as unused
                    entry->inode = 0;
                    ext2_put_block(fs, block_data, true);
                }
                return EXT2_SUCCESS;
            }
        }
        
        if (prev_block && prev_block != block_data) {
            ext2_put_block(fs, prev_block, false);
        }
        
        prev_entry = entry;
        prev_block = block_data;
        prev_block_num = block_num;
        offset += entry->rec_len;
    }
    
    if (prev_block) {
        ext2_put_block(fs, prev_block, false);
    }
    
    return EXT2_ERROR_NOT_FOUND;
}

static int ext2_resolve_path(ext2_fs_t* fs, const char* path, 
                            uint32_t* inode_num, char* last_component) {
    if (path[0] != '/') {
        return EXT2_ERROR_INVALID;
    }
    
    *inode_num = EXT2_ROOT_INO;
    
    if (path[1] == '\0') {
        if (last_component) {
            last_component[0] = '\0';
        }
        return EXT2_SUCCESS;
    }
    
    char path_copy[256];
    strncpy(path_copy, path + 1, 255);
    path_copy[255] = '\0';
    
    char* token = path_copy;
    char* next_slash;
    
    while (token && *token) {
        next_slash = strchr(token, '/');
        if (next_slash) {
            *next_slash = '\0';
        }
        
        if (next_slash == NULL && last_component) {
            // This is the last component
            strcpy(last_component, token);
            return EXT2_SUCCESS;
        }
        
        uint32_t next_inode;
        int result = ext2_lookup(fs, *inode_num, token, &next_inode);
        if (result != EXT2_SUCCESS) {
            return result;
        }
        
        *inode_num = next_inode;
        
        if (next_slash) {
            token = next_slash + 1;
        } else {
            break;
        }
    }
    
    if (last_component) {
        last_component[0] = '\0';
    }
    
    return EXT2_SUCCESS;
}

ext2_fs_t* ext2_mount(block_device_t* device) {
    if (!device) {
        return NULL;
    }
    
    ext2_fs_t* fs = (ext2_fs_t*)kmalloc(sizeof(ext2_fs_t));
    if (!fs) {
        return NULL;
    }
    
    memset(fs, 0, sizeof(ext2_fs_t));
    fs->device = device;
    fs->max_cache_entries = EXT2_CACHE_SIZE;
    
    // Read superblock (at byte 1024, which is LBA 2 for 512-byte sectors)
    uint8_t* sb_buffer = (uint8_t*)kmalloc(1024);
    if (!sb_buffer) {
        kfree(fs);
        return NULL;
    }
    
    uint64_t sb_lba = 1024 / device->sector_size;
    uint32_t sb_sectors = 1024 / device->sector_size;
    
    if (!device->read(device, sb_lba, sb_sectors, sb_buffer)) {
        kfree(sb_buffer);
        kfree(fs);
        return NULL;
    }
    
    memcpy(&fs->superblock, sb_buffer, sizeof(ext2_superblock_t));
    kfree(sb_buffer);
    
    // Verify magic number
    if (fs->superblock.s_magic != EXT2_SUPER_MAGIC) {
        kfree(fs);
        return NULL;
    }
    
    // Calculate filesystem parameters
    fs->block_size = 1024 << fs->superblock.s_log_block_size;
    fs->inode_size = fs->superblock.s_inode_size ? 
                     fs->superblock.s_inode_size : 128;
    fs->num_groups = (fs->superblock.s_blocks_count + 
                     fs->superblock.s_blocks_per_group - 1) / 
                     fs->superblock.s_blocks_per_group;
    fs->first_data_block = fs->superblock.s_first_data_block;
    
    // Read group descriptors
    uint32_t gd_size = fs->num_groups * sizeof(ext2_group_desc_t);
    fs->group_desc = (ext2_group_desc_t*)kmalloc(gd_size);
    if (!fs->group_desc) {
        kfree(fs);
        return NULL;
    }
    
    uint32_t gd_block = fs->first_data_block + 1;
    uint8_t* gd_buffer = (uint8_t*)kmalloc(fs->block_size);
    if (!gd_buffer) {
        kfree(fs->group_desc);
        kfree(fs);
        return NULL;
    }
    
    if (ext2_read_block(fs, gd_block, gd_buffer) != EXT2_SUCCESS) {
        kfree(gd_buffer);
        kfree(fs->group_desc);
        kfree(fs);
        return NULL;
    }
    
    memcpy(fs->group_desc, gd_buffer, gd_size);
    kfree(gd_buffer);
    
    return fs;
}

int ext2_unmount(ext2_fs_t* fs) {
    if (!fs) {
        return EXT2_ERROR_INVALID;
    }
    
    // Flush all cached blocks
    ext2_flush_cache(fs);
    
    // Free cache entries
    ext2_cache_entry_t* entry = fs->cache_head;
    while (entry) {
        ext2_cache_entry_t* next = entry->next;
        kfree(entry->data);
        kfree(entry);
        entry = next;
    }
    
    // Free group descriptors
    kfree(fs->group_desc);
    
    // Free filesystem structure
    kfree(fs);
    
    return EXT2_SUCCESS;
}

ext2_file_t* ext2_open(ext2_fs_t* fs, const char* path) {
    if (!fs || !path) {
        return NULL;
    }
    
    uint32_t inode_num;
    if (ext2_resolve_path(fs, path, &inode_num, NULL) != EXT2_SUCCESS) {
        return NULL;
    }
    
    ext2_file_t* file = (ext2_file_t*)kmalloc(sizeof(ext2_file_t));
    if (!file) {
        return NULL;
    }
    
    if (ext2_read_inode(fs, inode_num, &file->inode) != EXT2_SUCCESS) {
        kfree(file);
        return NULL;
    }
    
    file->fs = fs;
    file->inode_num = inode_num;
    file->position = 0;
    file->is_directory = (file->inode.i_mode & EXT2_S_IFDIR) != 0;
    
    return file;
}

int ext2_close(ext2_file_t* file) {
    if (!file) {
        return EXT2_ERROR_INVALID;
    }
    
    // Write back inode if modified
    ext2_write_inode(file->fs, file->inode_num, &file->inode);
    
    kfree(file);
    return EXT2_SUCCESS;
}

int ext2_read(ext2_file_t* file, void* buffer, uint32_t size) {
    if (!file || !buffer) {
        return EXT2_ERROR_INVALID;
    }
    
    if (file->is_directory) {
        return EXT2_ERROR_IS_DIR;
    }
    
    uint32_t file_size = file->inode.i_size;
    if (file->position >= file_size) {
        return 0;
    }
    
    uint32_t to_read = MIN(size, file_size - file->position);
    uint32_t bytes_read = 0;
    
    while (bytes_read < to_read) {
        uint32_t block_index = file->position / file->fs->block_size;
        uint32_t block_offset = file->position % file->fs->block_size;
        uint32_t chunk_size = MIN(to_read - bytes_read, 
                                 file->fs->block_size - block_offset);
        
        uint32_t block_num = ext2_get_block_num(file->fs, &file->inode, block_index, false);
        if (block_num == 0) {
            // Sparse file, return zeros
            memset((uint8_t*)buffer + bytes_read, 0, chunk_size);
        } else {
            uint8_t* block_data = ext2_get_block(file->fs, block_num);
            if (!block_data) {
                return EXT2_ERROR_IO;
            }
            
            memcpy((uint8_t*)buffer + bytes_read, block_data + block_offset, chunk_size);
            ext2_put_block(file->fs, block_data, false);
        }
        
        file->position += chunk_size;
        bytes_read += chunk_size;
    }
    
    return bytes_read;
}

int ext2_write(ext2_file_t* file, const void* buffer, uint32_t size) {
    if (!file || !buffer) {
        return EXT2_ERROR_INVALID;
    }
    
    if (file->is_directory) {
        return EXT2_ERROR_IS_DIR;
    }
    
    uint32_t bytes_written = 0;
    
    while (bytes_written < size) {
        uint32_t block_index = file->position / file->fs->block_size;
        uint32_t block_offset = file->position % file->fs->block_size;
        uint32_t chunk_size = MIN(size - bytes_written, 
                                 file->fs->block_size - block_offset);
        
        uint32_t block_num = ext2_get_block_num(file->fs, &file->inode, block_index, true);
        if (block_num == 0) {
            return EXT2_ERROR_NO_SPACE;
        }
        
        uint8_t* block_data = ext2_get_block(file->fs, block_num);
        if (!block_data) {
            return EXT2_ERROR_IO;
        }
        
        memcpy(block_data + block_offset, (const uint8_t*)buffer + bytes_written, chunk_size);
        ext2_put_block(file->fs, block_data, true);
        
        file->position += chunk_size;
        bytes_written += chunk_size;
        
        if (file->position > file->inode.i_size) {
            file->inode.i_size = file->position;
        }
    }
    
    // Update inode
    ext2_write_inode(file->fs, file->inode_num, &file->inode);
    
    return bytes_written;
}

int ext2_seek(ext2_file_t* file, int64_t offset, int whence) {
    if (!file) {
        return EXT2_ERROR_INVALID;
    }
    
    int64_t new_pos;
    
    switch (whence) {
        case EXT2_SEEK_SET:
            new_pos = offset;
            break;
        case EXT2_SEEK_CUR:
            new_pos = file->position + offset;
            break;
        case EXT2_SEEK_END:
            new_pos = file->inode.i_size + offset;
            break;
        default:
            return EXT2_ERROR_INVALID;
    }
    
    if (new_pos < 0) {
        return EXT2_ERROR_INVALID;
    }
    
    file->position = new_pos;
    return EXT2_SUCCESS;
}

uint64_t ext2_tell(ext2_file_t* file) {
    if (!file) {
        return 0;
    }
    return file->position;
}

uint64_t ext2_size(ext2_file_t* file) {
    if (!file) {
        return 0;
    }
    return file->inode.i_size;
}

int ext2_create(ext2_fs_t* fs, const char* path, uint16_t mode) {
    if (!fs || !path) {
        return EXT2_ERROR_INVALID;
    }
    
    char last_component[256];
    uint32_t parent_inode;
    
    int result = ext2_resolve_path(fs, path, &parent_inode, last_component);
    if (result != EXT2_SUCCESS && result != EXT2_ERROR_NOT_FOUND) {
        return result;
    }
    
    if (last_component[0] == '\0') {
        return EXT2_ERROR_INVALID;
    }
    
    // Check if already exists
    uint32_t existing_inode;
    if (ext2_lookup(fs, parent_inode, last_component, &existing_inode) == EXT2_SUCCESS) {
        return EXT2_ERROR_EXISTS;
    }
    
    // Allocate new inode
    uint32_t new_inode;
    result = ext2_alloc_inode(fs, parent_inode, false, &new_inode);
    if (result != EXT2_SUCCESS) {
        return result;
    }
    
    // Initialize inode
    ext2_inode_t inode;
    memset(&inode, 0, sizeof(ext2_inode_t));
    inode.i_mode = mode | EXT2_S_IFREG;
    inode.i_links_count = 1;
    inode.i_size = 0;
    
    result = ext2_write_inode(fs, new_inode, &inode);
    if (result != EXT2_SUCCESS) {
        ext2_free_inode(fs, new_inode);
        return result;
    }
    
    // Add directory entry
    result = ext2_add_dir_entry(fs, parent_inode, last_component, 
                               new_inode, EXT2_FT_REG_FILE);
    if (result != EXT2_SUCCESS) {
        ext2_free_inode(fs, new_inode);
        return result;
    }
    
    return EXT2_SUCCESS;
}

int ext2_delete(ext2_fs_t* fs, const char* path) {
    if (!fs || !path) {
        return EXT2_ERROR_INVALID;
    }
    
    char last_component[256];
    uint32_t parent_inode;
    
    int result = ext2_resolve_path(fs, path, &parent_inode, last_component);
    if (result != EXT2_SUCCESS) {
        return result;
    }
    
    if (last_component[0] == '\0') {
        return EXT2_ERROR_INVALID;
    }
    
    // Find the inode
    uint32_t inode_num;
    result = ext2_lookup(fs, parent_inode, last_component, &inode_num);
    if (result != EXT2_SUCCESS) {
        return result;
    }
    
    ext2_inode_t inode;
    result = ext2_read_inode(fs, inode_num, &inode);
    if (result != EXT2_SUCCESS) {
        return result;
    }
    
    // Check if it's a directory
    if ((inode.i_mode & EXT2_S_IFDIR) != 0) {
        return EXT2_ERROR_IS_DIR;
    }
    
    // Remove directory entry
    result = ext2_remove_dir_entry(fs, parent_inode, last_component);
    if (result != EXT2_SUCCESS) {
        return result;
    }
    
    // Decrease link count
    inode.i_links_count--;
    
    if (inode.i_links_count == 0) {
        // Free all blocks
        for (uint32_t i = 0; i < EXT2_NDIR_BLOCKS && inode.i_block[i]; i++) {
            ext2_free_block(fs, inode.i_block[i]);
        }
        // TODO: Free indirect blocks
        
        // Free inode
        ext2_free_inode(fs, inode_num);
    } else {
        ext2_write_inode(fs, inode_num, &inode);
    }
    
    return EXT2_SUCCESS;
}

int ext2_mkdir(ext2_fs_t* fs, const char* path) {
    if (!fs || !path) {
        return EXT2_ERROR_INVALID;
    }
    
    char last_component[256];
    uint32_t parent_inode;
    
    int result = ext2_resolve_path(fs, path, &parent_inode, last_component);
    if (result != EXT2_SUCCESS && result != EXT2_ERROR_NOT_FOUND) {
        return result;
    }
    
    if (last_component[0] == '\0') {
        return EXT2_ERROR_INVALID;
    }
    
    // Check if already exists
    uint32_t existing_inode;
    if (ext2_lookup(fs, parent_inode, last_component, &existing_inode) == EXT2_SUCCESS) {
        return EXT2_ERROR_EXISTS;
    }
    
    // Allocate new inode
    uint32_t new_inode;
    result = ext2_alloc_inode(fs, parent_inode, true, &new_inode);
    if (result != EXT2_SUCCESS) {
        return result;
    }
    
    // Initialize inode
    ext2_inode_t inode;
    memset(&inode, 0, sizeof(ext2_inode_t));
    inode.i_mode = 0755 | EXT2_S_IFDIR;
    inode.i_links_count = 2;  // . and ..
    inode.i_size = fs->block_size;
    
    // Allocate first block for directory
    uint32_t dir_block;
    result = ext2_alloc_block(fs, &dir_block);
    if (result != EXT2_SUCCESS) {
        ext2_free_inode(fs, new_inode);
        return result;
    }
    
    inode.i_block[0] = dir_block;
    
    result = ext2_write_inode(fs, new_inode, &inode);
    if (result != EXT2_SUCCESS) {
        ext2_free_block(fs, dir_block);
        ext2_free_inode(fs, new_inode);
        return result;
    }
    
    // Create . and .. entries
    uint8_t* block_data = ext2_get_block(fs, dir_block);
    if (!block_data) {
        ext2_free_block(fs, dir_block);
        ext2_free_inode(fs, new_inode);
        return EXT2_ERROR_IO;
    }
    
    memset(block_data, 0, fs->block_size);
    
    ext2_dir_entry_t* dot = (ext2_dir_entry_t*)block_data;
    dot->inode = new_inode;
    dot->rec_len = 12;
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    dot->name[0] = '.';
    
    ext2_dir_entry_t* dotdot = (ext2_dir_entry_t*)(block_data + 12);
    dotdot->inode = parent_inode;
    dotdot->rec_len = fs->block_size - 12;
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    
    ext2_put_block(fs, block_data, true);
    
    // Add directory entry in parent
    result = ext2_add_dir_entry(fs, parent_inode, last_component, 
                               new_inode, EXT2_FT_DIR);
    if (result != EXT2_SUCCESS) {
        ext2_free_block(fs, dir_block);
        ext2_free_inode(fs, new_inode);
        return result;
    }
    
    // Update parent's link count (for ..)
    ext2_inode_t parent;
    if (ext2_read_inode(fs, parent_inode, &parent) == EXT2_SUCCESS) {
        parent.i_links_count++;
        ext2_write_inode(fs, parent_inode, &parent);
    }
    
    return EXT2_SUCCESS;
}

int ext2_rmdir(ext2_fs_t* fs, const char* path) {
    if (!fs || !path) {
        return EXT2_ERROR_INVALID;
    }
    
    char last_component[256];
    uint32_t parent_inode;
    
    int result = ext2_resolve_path(fs, path, &parent_inode, last_component);
    if (result != EXT2_SUCCESS) {
        return result;
    }
    
    if (last_component[0] == '\0') {
        return EXT2_ERROR_INVALID;
    }
    
    // Find the inode
    uint32_t inode_num;
    result = ext2_lookup(fs, parent_inode, last_component, &inode_num);
    if (result != EXT2_SUCCESS) {
        return result;
    }
    
    ext2_inode_t inode;
    result = ext2_read_inode(fs, inode_num, &inode);
    if (result != EXT2_SUCCESS) {
        return result;
    }
    
    // Check if it's a directory
    if ((inode.i_mode & EXT2_S_IFDIR) == 0) {
        return EXT2_ERROR_NOT_DIR;
    }
    
    // Check if empty (should only have . and ..)
    if (inode.i_links_count > 2) {
        return EXT2_ERROR_NOT_EMPTY;
    }
    
    // Remove directory entry
    result = ext2_remove_dir_entry(fs, parent_inode, last_component);
    if (result != EXT2_SUCCESS) {
        return result;
    }
    
    // Free blocks
    for (uint32_t i = 0; i < EXT2_NDIR_BLOCKS && inode.i_block[i]; i++) {
        ext2_free_block(fs, inode.i_block[i]);
    }
    
    // Free inode
    ext2_free_inode(fs, inode_num);
    
    // Update parent's link count
    ext2_inode_t parent;
    if (ext2_read_inode(fs, parent_inode, &parent) == EXT2_SUCCESS) {
        parent.i_links_count--;
        ext2_write_inode(fs, parent_inode, &parent);
    }
    
    return EXT2_SUCCESS;
}

ext2_dir_iter_t* ext2_opendir(ext2_fs_t* fs, const char* path) {
    if (!fs || !path) {
        return NULL;
    }
    
    ext2_file_t* dir = ext2_open(fs, path);
    if (!dir) {
        return NULL;
    }
    
    if (!dir->is_directory) {
        ext2_close(dir);
        return NULL;
    }
    
    ext2_dir_iter_t* iter = (ext2_dir_iter_t*)kmalloc(sizeof(ext2_dir_iter_t));
    if (!iter) {
        ext2_close(dir);
        return NULL;
    }
    
    iter->dir = dir;
    iter->offset = 0;
    
    return iter;
}

int ext2_readdir(ext2_dir_iter_t* iter, char* name, uint32_t* inode, uint8_t* type) {
    if (!iter || !iter->dir) {
        return EXT2_ERROR_INVALID;
    }
    
    ext2_file_t* dir = iter->dir;
    
    while (iter->offset < dir->inode.i_size) {
        uint32_t block_index = iter->offset / dir->fs->block_size;
        uint32_t block_offset = iter->offset % dir->fs->block_size;
        
        uint32_t block_num = ext2_get_block_num(dir->fs, &dir->inode, block_index, false);
        if (block_num == 0) {
            return EXT2_ERROR_IO;
        }
        
        uint8_t* block_data = ext2_get_block(dir->fs, block_num);
        if (!block_data) {
            return EXT2_ERROR_IO;
        }
        
        ext2_dir_entry_t* entry = (ext2_dir_entry_t*)(block_data + block_offset);
        
        iter->offset += entry->rec_len;
        
        if (entry->inode != 0) {
            if (name) {
                memcpy(name, entry->name, entry->name_len);
                name[entry->name_len] = '\0';
            }
            if (inode) {
                *inode = entry->inode;
            }
            if (type) {
                *type = entry->file_type;
            }
            
            ext2_put_block(dir->fs, block_data, false);
            return EXT2_SUCCESS;
        }
        
        ext2_put_block(dir->fs, block_data, false);
    }
    
    return EXT2_ERROR_NOT_FOUND;
}

int ext2_closedir(ext2_dir_iter_t* iter) {
    if (!iter) {
        return EXT2_ERROR_INVALID;
    }
    
    if (iter->dir) {
        ext2_close(iter->dir);
    }
    
    kfree(iter);
    return EXT2_SUCCESS;
}

int ext2_stat(ext2_fs_t* fs, const char* path, ext2_inode_t* inode) {
    if (!fs || !path || !inode) {
        return EXT2_ERROR_INVALID;
    }
    
    uint32_t inode_num;
    int result = ext2_resolve_path(fs, path, &inode_num, NULL);
    if (result != EXT2_SUCCESS) {
        return result;
    }
    
    return ext2_read_inode(fs, inode_num, inode);
}

bool ext2_exists(ext2_fs_t* fs, const char* path) {
    if (!fs || !path) {
        return false;
    }
    
    uint32_t inode_num;
    return ext2_resolve_path(fs, path, &inode_num, NULL) == EXT2_SUCCESS;
}