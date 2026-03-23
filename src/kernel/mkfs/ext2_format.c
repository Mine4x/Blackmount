#include "ext2_format.h"
#include <block/block.h>
#include <drivers/fs/ext/ext2.h>
#include <heap.h>
#include <string.h>
#include <debug.h>

#define FMT_BLOCK_SIZE          1024
#define FMT_SECTORS_PER_BLOCK   2
#define FMT_INODE_SIZE          128
#define FMT_BLOCKS_PER_GROUP    8192
#define FMT_FIRST_DATA_BLOCK    1
#define FMT_INODES_RATIO        4
#define FMT_FIRST_INO           11
#define FMT_RESERVED_INODES     10

static bool fmt_write_block(block_device_t* dev, uint32_t blk, const void* data)
{
    return dev->write(dev, (uint64_t)blk * FMT_SECTORS_PER_BLOCK, FMT_SECTORS_PER_BLOCK, data);
}

static void bitmap_set(uint8_t* bm, uint32_t bit)
{
    bm[bit / 8] |= (uint8_t)(1u << (bit % 8));
}

int ext2_format(block_device_t* dev)
{
    uint64_t total_bytes  = (uint64_t)dev->sector_count * dev->sector_size;
    uint32_t total_blocks = (uint32_t)(total_bytes / FMT_BLOCK_SIZE);

    if (total_blocks < 16) {
        log_err("EXT2FMT", "Device %s too small (%u blocks)", dev->name, total_blocks);
        return -1;
    }

    uint32_t blocks_in_group = total_blocks - FMT_FIRST_DATA_BLOCK;
    if (blocks_in_group > FMT_BLOCKS_PER_GROUP)
        blocks_in_group = FMT_BLOCKS_PER_GROUP;

    uint32_t inodes_per_group = (blocks_in_group / FMT_INODES_RATIO + 7) & ~7u;
    if (inodes_per_group < 16)   inodes_per_group = 16;
    if (inodes_per_group > 8192) inodes_per_group = 8192;

    uint32_t inode_table_blocks =
        (inodes_per_group * FMT_INODE_SIZE + FMT_BLOCK_SIZE - 1) / FMT_BLOCK_SIZE;

    /*
     * Group 0 layout (absolute block numbers, all relative to s_first_data_block=1):
     *   blk 1  : superblock
     *   blk 2  : group descriptor table
     *   blk 3  : block bitmap
     *   blk 4  : inode bitmap
     *   blk 5  : inode table  (inode_table_blocks blocks)
     *   blk 5+N: root directory data block
     */
    const uint32_t blk_sb        = 1;
    const uint32_t blk_gdt       = 2;
    const uint32_t blk_bb        = 3;
    const uint32_t blk_ib        = 4;
    const uint32_t blk_inode_tbl = 5;
    const uint32_t blk_root_dir  = 5 + inode_table_blocks;

    uint32_t overhead    = 4 + inode_table_blocks + 1;
    uint32_t free_blocks = blocks_in_group - overhead;

    if (blocks_in_group < overhead) {
        log_err("EXT2FMT", "Device %s too small for metadata", dev->name);
        return -1;
    }

    uint32_t reserved_blocks = total_blocks / 20;
    uint32_t free_inodes     = inodes_per_group - FMT_RESERVED_INODES;

    ext2_superblock_t* sb = kmalloc(FMT_BLOCK_SIZE);
    if (!sb) return -1;
    memset(sb, 0, FMT_BLOCK_SIZE);

    sb->s_inodes_count      = inodes_per_group;
    sb->s_blocks_count      = total_blocks;
    sb->s_r_blocks_count    = reserved_blocks;
    sb->s_free_blocks_count = free_blocks;
    sb->s_free_inodes_count = free_inodes;
    sb->s_first_data_block  = FMT_FIRST_DATA_BLOCK;
    sb->s_log_block_size    = 0;
    sb->s_log_frag_size     = 0;
    sb->s_blocks_per_group  = FMT_BLOCKS_PER_GROUP;
    sb->s_frags_per_group   = FMT_BLOCKS_PER_GROUP;
    sb->s_inodes_per_group  = inodes_per_group;
    sb->s_magic             = EXT2_SUPER_MAGIC;
    sb->s_state             = 1;
    sb->s_rev_level         = 1;
    sb->s_first_ino         = FMT_FIRST_INO;
    sb->s_inode_size        = FMT_INODE_SIZE;
    sb->s_creator_os        = 0;

    if (!fmt_write_block(dev, blk_sb, sb)) {
        log_err("EXT2FMT", "Failed to write superblock");
        kfree(sb);
        return -1;
    }
    kfree(sb);

    uint8_t* gdt_buf = kmalloc(FMT_BLOCK_SIZE);
    if (!gdt_buf) return -1;
    memset(gdt_buf, 0, FMT_BLOCK_SIZE);

    ext2_group_desc_t* gd    = (ext2_group_desc_t*)gdt_buf;
    gd->bg_block_bitmap      = blk_bb;
    gd->bg_inode_bitmap      = blk_ib;
    gd->bg_inode_table       = blk_inode_tbl;
    gd->bg_free_blocks_count = (uint16_t)free_blocks;
    gd->bg_free_inodes_count = (uint16_t)free_inodes;
    gd->bg_used_dirs_count   = 1;

    if (!fmt_write_block(dev, blk_gdt, gdt_buf)) {
        log_err("EXT2FMT", "Failed to write group descriptor");
        kfree(gdt_buf);
        return -1;
    }
    kfree(gdt_buf);

    uint8_t* bb = kmalloc(FMT_BLOCK_SIZE);
    if (!bb) return -1;
    memset(bb, 0, FMT_BLOCK_SIZE);

    for (uint32_t i = 0; i < overhead; i++)
        bitmap_set(bb, i);
    for (uint32_t i = blocks_in_group; i < FMT_BLOCKS_PER_GROUP; i++)
        bitmap_set(bb, i);

    if (!fmt_write_block(dev, blk_bb, bb)) {
        log_err("EXT2FMT", "Failed to write block bitmap");
        kfree(bb);
        return -1;
    }
    kfree(bb);

    uint8_t* ib = kmalloc(FMT_BLOCK_SIZE);
    if (!ib) return -1;
    memset(ib, 0, FMT_BLOCK_SIZE);

    for (uint32_t i = 0; i < FMT_RESERVED_INODES; i++)
        bitmap_set(ib, i);
    for (uint32_t i = inodes_per_group; i < FMT_BLOCKS_PER_GROUP; i++)
        bitmap_set(ib, i);

    if (!fmt_write_block(dev, blk_ib, ib)) {
        log_err("EXT2FMT", "Failed to write inode bitmap");
        kfree(ib);
        return -1;
    }
    kfree(ib);

    uint32_t inode_tbl_bytes = inode_table_blocks * FMT_BLOCK_SIZE;
    uint8_t* inode_tbl = kmalloc(inode_tbl_bytes);
    if (!inode_tbl) return -1;
    memset(inode_tbl, 0, inode_tbl_bytes);

    /*
     * Inode N lives at byte offset (N-1) * inode_size within the table.
     * Inode 2 (root directory) is therefore at offset 128.
     */
    ext2_inode_t* root = (ext2_inode_t*)(inode_tbl + FMT_INODE_SIZE);
    root->i_mode        = EXT2_S_IFDIR | 0755;
    root->i_uid         = 0;
    root->i_size        = FMT_BLOCK_SIZE;
    root->i_links_count = 2;
    root->i_blocks      = FMT_SECTORS_PER_BLOCK;
    root->i_block[0]    = blk_root_dir;

    for (uint32_t i = 0; i < inode_table_blocks; i++) {
        if (!fmt_write_block(dev, blk_inode_tbl + i, inode_tbl + i * FMT_BLOCK_SIZE)) {
            log_err("EXT2FMT", "Failed to write inode table block %u", i);
            kfree(inode_tbl);
            return -1;
        }
    }
    kfree(inode_tbl);

    uint8_t* dir_buf = kmalloc(FMT_BLOCK_SIZE);
    if (!dir_buf) return -1;
    memset(dir_buf, 0, FMT_BLOCK_SIZE);

    
    ext2_dir_entry_t* dot = (ext2_dir_entry_t*)dir_buf;
    dot->inode     = 2;
    dot->rec_len   = 12;
    dot->name_len  = 1;
    dot->file_type = EXT2_FT_DIR;
    dot->name[0]   = '.';

    ext2_dir_entry_t* dotdot = (ext2_dir_entry_t*)(dir_buf + 12);
    dotdot->inode     = 2;
    dotdot->rec_len   = FMT_BLOCK_SIZE - 12;
    dotdot->name_len  = 2;
    dotdot->file_type = EXT2_FT_DIR;
    dotdot->name[0]   = '.';
    dotdot->name[1]   = '.';

    if (!fmt_write_block(dev, blk_root_dir, dir_buf)) {
        log_err("EXT2FMT", "Failed to write root directory block");
        kfree(dir_buf);
        return -1;
    }
    kfree(dir_buf);

    log_ok("EXT2FMT", "Formatted %s: %u blocks total, %u inodes, %u free blocks",
           dev->name, total_blocks, inodes_per_group, free_blocks);
    return 0;
}