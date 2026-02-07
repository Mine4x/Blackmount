#include "fat.h"
#include <heap.h>
#include <memory.h>
#include <string.h>
#include <debug.h>

typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fats;
    uint16_t root_entries;
    uint16_t total16;
    uint8_t  media;
    uint16_t fat16_size;
    uint16_t sptrack;
    uint16_t heads;
    uint32_t hidden;
    uint32_t total32;
} fat_bpb_t;

struct fat_fs {
    block_device_t* dev;
    fat_type_t type;

    uint32_t fat_start;
    uint32_t data_start;
    uint32_t root_dir;

    uint32_t root_cluster;
    uint32_t spc;
    uint32_t bps;
    uint32_t root_entries;
};

fat_fs_t* fat_mount(block_device_t* dev) {
    uint8_t sector[512];
    if (!dev->read(dev, 0, 1, sector)) {
        log_err("FAT", "Failed to read boot sector");
        return NULL;
    }

    fat_bpb_t* bpb = (fat_bpb_t*)sector;
    fat_fs_t* fs = kmalloc(sizeof(fat_fs_t));
    memset(fs, 0, sizeof(*fs));

    fs->dev = dev;
    fs->bps = bpb->bytes_per_sector;
    fs->spc = bpb->sectors_per_cluster;
    fs->root_entries = bpb->root_entries;

    uint32_t total_sectors = bpb->total16 ?
        bpb->total16 : bpb->total32;

    uint32_t fat_size = bpb->fat16_size;
    if (fat_size == 0) {
        uint32_t* fat32 = (uint32_t*)(sector + 36);
        fat_size = fat32[0];
        fs->type = FAT32;
        fs->root_cluster = fat32[2];
    }

    uint32_t root_dir_sectors =
        ((bpb->root_entries * 32) + (fs->bps - 1)) / fs->bps;

    fs->fat_start = bpb->reserved_sectors;
    fs->root_dir = fs->fat_start + bpb->fats * fat_size;
    fs->data_start = fs->root_dir + root_dir_sectors;

    uint32_t data_sectors =
        total_sectors - fs->data_start;

    uint32_t clusters = data_sectors / fs->spc;

    if (fs->type != FAT32) {
        if (clusters < 4085)
            fs->type = FAT12;
        else
            fs->type = FAT16;
    }

    log_ok("FAT", "Mounted FAT%d filesystem", fs->type == FAT12 ? 12 :
                                        fs->type == FAT16 ? 16 : 32);
    return fs;
}

void fat_unmount(fat_fs_t* fs) {
    if (!fs) {
        return;
    }
    
    kfree(fs);
    log_info("FAT", "Filesystem unmounted");
}

static uint32_t fat_get_next_cluster(fat_fs_t* fs, uint32_t cluster) {
    uint8_t sector[512];
    uint32_t fat_offset;
    uint32_t fat_sector;
    uint32_t next_cluster;
    
    if (fs->type == FAT12) {
        fat_offset = cluster + (cluster / 2);
        fat_sector = fs->fat_start + (fat_offset / fs->bps);
        uint32_t entry_offset = fat_offset % fs->bps;
        
        if (entry_offset == (fs->bps - 1)) {
            uint8_t sector2[512];
            if (!fs->dev->read(fs->dev, fat_sector, 1, sector)) {
                return 0;
            }
            if (!fs->dev->read(fs->dev, fat_sector + 1, 1, sector2)) {
                return 0;
            }
            next_cluster = sector[entry_offset] | (sector2[0] << 8);
        } else {
            if (!fs->dev->read(fs->dev, fat_sector, 1, sector)) {
                return 0;
            }
            next_cluster = *(uint16_t*)&sector[entry_offset];
        }
        
        if (cluster & 1) {
            next_cluster >>= 4;
        } else {
            next_cluster &= 0x0FFF;
        }
        
        if (next_cluster >= 0x0FF8) {
            return 0xFFFFFFFF;
        }
    } else if (fs->type == FAT16) {
        fat_offset = cluster * 2;
        fat_sector = fs->fat_start + (fat_offset / fs->bps);
        uint32_t entry_offset = fat_offset % fs->bps;
        
        if (!fs->dev->read(fs->dev, fat_sector, 1, sector)) {
            return 0;
        }
        
        next_cluster = *(uint16_t*)&sector[entry_offset];
        
        if (next_cluster >= 0xFFF8) {
            return 0xFFFFFFFF;
        }
    } else {
        fat_offset = cluster * 4;
        fat_sector = fs->fat_start + (fat_offset / fs->bps);
        uint32_t entry_offset = fat_offset % fs->bps;
        
        if (!fs->dev->read(fs->dev, fat_sector, 1, sector)) {
            return 0;
        }
        
        next_cluster = *(uint32_t*)&sector[entry_offset] & 0x0FFFFFFF;
        
        if (next_cluster >= 0x0FFFFFF8) {
            return 0xFFFFFFFF;
        }
    }
    
    return next_cluster;
}

static uint32_t fat_cluster_to_sector(fat_fs_t* fs, uint32_t cluster) {
    if (cluster < 2) {
        return 0;
    }
    return fs->data_start + (cluster - 2) * fs->spc;
}

// TODO: subdir support
// FIXME: not working on FAT 32
bool fat_open(fat_fs_t* fs, const char* path, fat_file_t* out) {
    if (!fs || !path || !out) {
        return false;
    }
    
    if (path[0] == '/') {
        path++;
    }
    
    typedef struct __attribute__((packed)) {
        char name[11];
        uint8_t attr;
        uint8_t reserved;
        uint8_t create_time_tenth;
        uint16_t create_time;
        uint16_t create_date;
        uint16_t access_date;
        uint16_t cluster_high;
        uint16_t modify_time;
        uint16_t modify_date;
        uint16_t cluster_low;
        uint32_t size;
    } fat_dir_entry_t;
    
    char fat_name[11];
    memset(fat_name, ' ', 11);
    
    int i = 0, j = 0;
    while (path[i] && path[i] != '.' && j < 8) {
        fat_name[j++] = (path[i] >= 'a' && path[i] <= 'z') ? 
                        path[i] - 32 : path[i];
        i++;
    }
    
    if (path[i] == '.') {
        i++;
        j = 8;
        while (path[i] && j < 11) {
            fat_name[j++] = (path[i] >= 'a' && path[i] <= 'z') ? 
                            path[i] - 32 : path[i];
            i++;
        }
    }
    
    uint32_t root_sectors = ((fs->root_entries * 32) + (fs->bps - 1)) / fs->bps;
    uint8_t sector[512];
    uint32_t root_sector = fs->root_dir;
    
    for (uint32_t sector_num = 0; sector_num < root_sectors; sector_num++) {
        if (!fs->dev->read(fs->dev, root_sector + sector_num, 1, sector)) {
            log_err("FAT", "Failed to read root directory sector");
            return false;
        }
        
        fat_dir_entry_t* entries = (fat_dir_entry_t*)sector;
        uint32_t entries_per_sector = fs->bps / 32;
        
        for (uint32_t entry = 0; entry < entries_per_sector; entry++) {
            if (entries[entry].name[0] == 0x00) {
                log_err("FAT", "File not found: %s", path);
                return false;
            }
            
            if (entries[entry].name[0] == 0xE5) {
                continue;
            }
            
            if (entries[entry].attr & 0x08) {
                continue;
            }
            
            if (memcmp(entries[entry].name, fat_name, 11) == 0) {
                out->fs = fs;
                out->size = entries[entry].size;
                out->pos = 0;
                out->cluster = entries[entry].cluster_low;
                if (fs->type == FAT32) {
                    out->cluster |= ((uint32_t)entries[entry].cluster_high << 16);
                }
                
                log_ok("FAT", "Opened file: %s (size: %u bytes, cluster: %u)", 
                       path, out->size, out->cluster);
                return true;
            }
        }
    }
    
    log_err("FAT", "File not found: %s", path);
    return false;
}

uint32_t fat_read(fat_file_t* file, void* buffer, uint32_t bytes) {
    if (!file || !buffer || bytes == 0) {
        return 0;
    }
    
    fat_fs_t* fs = file->fs;
    uint8_t* buf = (uint8_t*)buffer;
    uint32_t total_read = 0;
    
    if (file->pos + bytes > file->size) {
        bytes = file->size - file->pos;
    }
    
    if (bytes == 0) {
        return 0;
    }
    
    uint32_t current_cluster = file->cluster;
    uint32_t clusters_to_skip = file->pos / (fs->spc * fs->bps);
    uint32_t offset_in_cluster = file->pos % (fs->spc * fs->bps);
    
    // Skip to the cluster containing our current position
    for (uint32_t i = 0; i < clusters_to_skip; i++) {
        current_cluster = fat_get_next_cluster(fs, current_cluster);
        if (current_cluster == 0xFFFFFFFF) {
            return 0;
        }
    }
    
    uint8_t sector[512];
    
    while (bytes > 0 && current_cluster != 0xFFFFFFFF) {
        uint32_t sector_num = fat_cluster_to_sector(fs, current_cluster);
        uint32_t sector_in_cluster = offset_in_cluster / fs->bps;
        uint32_t offset_in_sector = offset_in_cluster % fs->bps;
        
        if (sector_in_cluster >= fs->spc) {
            current_cluster = fat_get_next_cluster(fs, current_cluster);
            offset_in_cluster = 0;
            
            if (current_cluster == 0xFFFFFFFF) {
                break;
            }
            continue;
        }
        
        if (!fs->dev->read(fs->dev, sector_num + sector_in_cluster, 1, sector)) {
            log_err("FAT", "Failed to read sector");
            break;
        }
        
        uint32_t to_copy = fs->bps - offset_in_sector;
        if (to_copy > bytes) {
            to_copy = bytes;
        }
        
        memcpy(buf, sector + offset_in_sector, to_copy);
        
        buf += to_copy;
        bytes -= to_copy;
        total_read += to_copy;
        file->pos += to_copy;
        offset_in_cluster += to_copy;
    }
    
    return total_read;
}