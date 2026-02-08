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

static void fat_name_to_83(const char* name, char* fat_name) {
    memset(fat_name, ' ', 11);
    
    int i = 0, j = 0;
    while (name[i] && name[i] != '.' && name[i] != '/' && j < 8) {
        char c = name[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        fat_name[j++] = c;
    }
    
    if (name[i] == '.') {
        i++;
        j = 8;
        while (name[i] && name[i] != '/' && j < 11) {
            char c = name[i++];
            if (c >= 'a' && c <= 'z') c -= 32;
            fat_name[j++] = c;
        }
    }
}

static bool fat_search_dir_cluster(fat_fs_t* fs, uint32_t dir_cluster, 
                                   const char* fat_name, fat_dir_entry_t* out_entry,
                                   uint32_t* out_sector, uint32_t* out_offset) {
    uint8_t sector[512];
    uint32_t current_cluster = dir_cluster;
    
    while (current_cluster != 0xFFFFFFFF && current_cluster >= 2) {
        uint32_t sector_num = fat_cluster_to_sector(fs, current_cluster);
        
        for (uint32_t sec = 0; sec < fs->spc; sec++) {
            if (!fs->dev->read(fs->dev, sector_num + sec, 1, sector)) {
                return false;
            }
            
            fat_dir_entry_t* entries = (fat_dir_entry_t*)sector;
            uint32_t entries_per_sector = fs->bps / sizeof(fat_dir_entry_t);
            
            for (uint32_t entry = 0; entry < entries_per_sector; entry++) {
                if (entries[entry].name[0] == 0x00) {
                    return false;
                }
                
                if (entries[entry].name[0] == 0xE5) {
                    continue;
                }
                
                if (entries[entry].attr & 0x08) {
                    continue;
                }
                
                if (memcmp(entries[entry].name, fat_name, 11) == 0) {
                    if (out_entry) {
                        memcpy(out_entry, &entries[entry], sizeof(fat_dir_entry_t));
                    }
                    if (out_sector) {
                        *out_sector = sector_num + sec;
                    }
                    if (out_offset) {
                        *out_offset = entry * sizeof(fat_dir_entry_t);
                    }
                    return true;
                }
            }
        }
        
        current_cluster = fat_get_next_cluster(fs, current_cluster);
    }
    
    return false;
}

static bool fat_search_root_fixed(fat_fs_t* fs, const char* fat_name,
                                  fat_dir_entry_t* out_entry,
                                  uint32_t* out_sector, uint32_t* out_offset) {
    uint8_t sector[512];
    uint32_t root_sectors = ((fs->root_entries * 32) + (fs->bps - 1)) / fs->bps;
    uint32_t root_sector = fs->root_dir;
    
    for (uint32_t sector_num = 0; sector_num < root_sectors; sector_num++) {
        if (!fs->dev->read(fs->dev, root_sector + sector_num, 1, sector)) {
            return false;
        }
        
        fat_dir_entry_t* entries = (fat_dir_entry_t*)sector;
        uint32_t entries_per_sector = fs->bps / sizeof(fat_dir_entry_t);
        
        for (uint32_t entry = 0; entry < entries_per_sector; entry++) {
            if (entries[entry].name[0] == 0x00) {
                return false;
            }
            
            if (entries[entry].name[0] == 0xE5) {
                continue;
            }
            
            if (entries[entry].attr & 0x08) {
                continue;
            }
            
            if (memcmp(entries[entry].name, fat_name, 11) == 0) {
                if (out_entry) {
                    memcpy(out_entry, &entries[entry], sizeof(fat_dir_entry_t));
                }
                if (out_sector) {
                    *out_sector = root_sector + sector_num;
                }
                if (out_offset) {
                    *out_offset = entry * sizeof(fat_dir_entry_t);
                }
                return true;
            }
        }
    }
    
    return false;
}

bool fat_open(fat_fs_t* fs, const char* path, fat_file_t* out) {
    if (!fs || !path || !out) {
        return false;
    }

    // Skip leading slash
    if (path[0] == '/') {
        path++;
    }

    // Start at root directory
    uint32_t current_cluster = (fs->type == FAT32) ? fs->root_cluster : 0;
    bool is_root = true;
    
    // Parse path components
    char component[256];
    int comp_idx = 0;
    
    while (*path) {
        // Extract next path component
        comp_idx = 0;
        while (*path && *path != '/' && comp_idx < 255) {
            component[comp_idx++] = *path++;
        }
        component[comp_idx] = '\0';
        
        if (*path == '/') {
            path++;
        }
        
        // Skip empty components
        if (comp_idx == 0) {
            continue;
        }
        
        // Convert to FAT 8.3 format
        char fat_name[11];
        fat_name_to_83(component, fat_name);
        
        // Search for this component
        fat_dir_entry_t entry;
        uint32_t dir_sector = 0;
        uint32_t dir_offset = 0;
        bool found = false;
        
        if (is_root && fs->type != FAT32) {
            // Search fixed root directory (FAT12/FAT16)
            found = fat_search_root_fixed(fs, fat_name, &entry, &dir_sector, &dir_offset);
        } else {
            // Search directory cluster chain (FAT32 root or any subdir)
            found = fat_search_dir_cluster(fs, current_cluster, fat_name, &entry, &dir_sector, &dir_offset);
        }
        
        if (!found) {
            return false;
        }
        
        // Get the cluster number
        uint32_t cluster = entry.cluster_low;
        if (fs->type == FAT32) {
            cluster |= ((uint32_t)entry.cluster_high << 16);
        }
        
        // Check if there's more path to traverse
        if (*path != '\0') {
            // Must be a directory to continue
            if (!(entry.attr & 0x10)) {
                return false; // Not a directory
            }
            
            current_cluster = cluster;
            is_root = false;
        } else {
            out->fs = fs;
            out->size = entry.size;
            out->pos = 0;
            out->cluster = cluster;
            out->dir_sector = dir_sector;
            out->dir_offset = dir_offset;
            return true;
        }
    }
    
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

static uint32_t fat_find_free_cluster(fat_fs_t* fs) {
    uint8_t sector[512];

    uint32_t total_sectors = fs->dev->sector_count;
    uint32_t data_sectors = total_sectors - fs->data_start;
    uint32_t total_clusters = data_sectors / fs->spc;

    for (uint32_t cluster = 2; cluster < total_clusters + 2; cluster++) {
        uint32_t fat_offset;
        uint32_t fat_sector;
        uint32_t entry_offset;

        if (fs->type == FAT16) {
            fat_offset = cluster * 2;
        } else if (fs->type == FAT32) {
            fat_offset = cluster * 4;
        } else {
            return 0xFFFFFFFF; // FAT12 not supported here
        }

        fat_sector = fs->fat_start + (fat_offset / fs->bps);
        entry_offset = fat_offset % fs->bps;

        if (!fs->dev->read(fs->dev, fat_sector, 1, sector)) {
            return 0xFFFFFFFF;
        }

        if (fs->type == FAT16) {
            uint16_t val = *(uint16_t*)&sector[entry_offset];
            if (val == 0x0000) {
                return cluster;
            }
        } else {
            uint32_t val = *(uint32_t*)&sector[entry_offset] & 0x0FFFFFFF;
            if (val == 0x00000000) {
                return cluster;
            }
        }
    }

    return 0xFFFFFFFF; // no free clusters
}

static bool fat_set_fat_entry(fat_fs_t* fs, uint32_t cluster, uint32_t next) {
    uint32_t fat_offset = 0;
    if (fs->type == FAT16) {
        fat_offset = cluster * 2;
    } else if (fs->type == FAT32) {
        fat_offset = cluster * 4;
    } else {
        return false; // FAT12 not supported here
    }

    uint32_t fat_sector = fs->fat_start + (fat_offset / fs->bps);
    uint32_t entry_offset = fat_offset % fs->bps;

    uint8_t sector[512];
    if (!fs->dev->read(fs->dev, fat_sector, 1, sector)) {
        return false;
    }

    if (fs->type == FAT16) {
        *(uint16_t*)&sector[entry_offset] = next & 0xFFFF;
    } else {
        *(uint32_t*)&sector[entry_offset] = next & 0x0FFFFFFF;
    }

    if (!fs->dev->write(fs->dev, fat_sector, 1, sector)) {
        return false;
    }

    return true;
}

static bool fat_find_free_entry_in_cluster(fat_fs_t* fs, uint32_t dir_cluster,
                                           fat_dir_entry_t** out_entry,
                                           uint32_t* out_sector, uint32_t* out_offset) {
    uint8_t sector[512];
    uint32_t current_cluster = dir_cluster;
    
    while (current_cluster != 0xFFFFFFFF && current_cluster >= 2) {
        uint32_t sector_num = fat_cluster_to_sector(fs, current_cluster);
        
        for (uint32_t sec = 0; sec < fs->spc; sec++) {
            if (!fs->dev->read(fs->dev, sector_num + sec, 1, sector)) {
                return false;
            }
            
            fat_dir_entry_t* entries = (fat_dir_entry_t*)sector;
            uint32_t entries_per_sector = fs->bps / sizeof(fat_dir_entry_t);
            
            for (uint32_t entry = 0; entry < entries_per_sector; entry++) {
                if (entries[entry].name[0] == 0x00 || entries[entry].name[0] == 0xE5) {
                    *out_entry = &entries[entry];
                    *out_sector = sector_num + sec;
                    *out_offset = entry * sizeof(fat_dir_entry_t);
                    return true;
                }
            }
        }
        
        // TODO: Allocate new cluster if directory is full
        current_cluster = fat_get_next_cluster(fs, current_cluster);
    }
    
    return false;
}

bool fat_create(fat_fs_t* fs, const char* name, fat_file_t* out) {
    if (!fs || !name || !out) {
        return false;
    }
    
    // Skip leading slash
    if (name[0] == '/') {
        name++;
    }
    
    // Find the last slash to separate directory path from filename
    const char* filename = name;
    const char* last_slash = NULL;
    for (const char* p = name; *p; p++) {
        if (*p == '/') {
            last_slash = p;
        }
    }
    
    uint32_t dir_cluster;
    bool is_root;
    
    // Navigate to parent directory if path contains subdirectories
    if (last_slash) {
        // Extract directory path
        int dir_path_len = last_slash - name;
        char dir_path[256];
        memcpy(dir_path, name, dir_path_len);
        dir_path[dir_path_len] = '\0';
        
        filename = last_slash + 1;
        
        // Start at root
        dir_cluster = (fs->type == FAT32) ? fs->root_cluster : 0;
        is_root = true;
        
        // Parse directory path components
        char component[256];
        int comp_idx = 0;
        const char* path = dir_path;
        
        while (*path) {
            // Extract next component
            comp_idx = 0;
            while (*path && *path != '/' && comp_idx < 255) {
                component[comp_idx++] = *path++;
            }
            component[comp_idx] = '\0';
            
            if (*path == '/') {
                path++;
            }
            
            if (comp_idx == 0) {
                continue;
            }
            
            // Convert to FAT 8.3 format
            char fat_name[11];
            fat_name_to_83(component, fat_name);
            
            // Search for directory
            fat_dir_entry_t entry;
            bool found = false;
            
            if (is_root && fs->type != FAT32) {
                found = fat_search_root_fixed(fs, fat_name, &entry, NULL, NULL);
            } else {
                found = fat_search_dir_cluster(fs, dir_cluster, fat_name, &entry, NULL, NULL);
            }
            
            if (!found || !(entry.attr & 0x10)) {
                return false; // Directory not found or not a directory
            }
            
            // Move to this directory
            dir_cluster = entry.cluster_low;
            if (fs->type == FAT32) {
                dir_cluster |= ((uint32_t)entry.cluster_high << 16);
            }
            is_root = false;
        }
    } else {
        // Creating in root directory
        dir_cluster = (fs->type == FAT32) ? fs->root_cluster : 0;
        is_root = true;
    }
    
    // Convert filename to FAT 8.3 format
    char fat_name[11];
    fat_name_to_83(filename, fat_name);
    
    uint8_t sector[512];
    uint32_t dir_sector = 0;
    uint32_t dir_offset = 0;
    
    // Find free entry in target directory
    if (is_root && fs->type != FAT32) {
        // Search fixed root directory
        uint32_t root_sectors = ((fs->root_entries * 32) + (fs->bps - 1)) / fs->bps;
        uint32_t root_sector = fs->root_dir;
        
        for (uint32_t s = 0; s < root_sectors; s++) {
            if (!fs->dev->read(fs->dev, root_sector + s, 1, sector)) {
                return false;
            }
            
            fat_dir_entry_t* e = (fat_dir_entry_t*)sector;
            uint32_t ents = fs->bps / 32;
            
            for (uint32_t n = 0; n < ents; n++) {
                if (e[n].name[0] == 0x00 || e[n].name[0] == 0xE5) {
                    memcpy(e[n].name, fat_name, 11);
                    e[n].attr = 0x20;
                    e[n].size = 0;
                    e[n].cluster_low = 0;
                    e[n].cluster_high = 0;
                    
                    if (!fs->dev->write(fs->dev, root_sector + s, 1, sector)) {
                        return false;
                    }
                    
                    out->fs = fs;
                    out->pos = 0;
                    out->size = 0;
                    out->cluster = 0;
                    out->dir_sector = root_sector + s;
                    out->dir_offset = n * sizeof(fat_dir_entry_t);
                    return true;
                }
            }
        }
    } else {
        // Search directory cluster chain
        fat_dir_entry_t* entry_ptr;
        if (fat_find_free_entry_in_cluster(fs, dir_cluster, &entry_ptr, &dir_sector, &dir_offset)) {
            if (!fs->dev->read(fs->dev, dir_sector, 1, sector)) {
                return false;
            }
            
            fat_dir_entry_t* e = (fat_dir_entry_t*)(sector + dir_offset);
            memcpy(e->name, fat_name, 11);
            e->attr = 0x20;
            e->size = 0;
            e->cluster_low = 0;
            e->cluster_high = 0;
            
            if (!fs->dev->write(fs->dev, dir_sector, 1, sector)) {
                return false;
            }
            
            out->fs = fs;
            out->pos = 0;
            out->size = 0;
            out->cluster = 0;
            out->dir_sector = dir_sector;
            out->dir_offset = dir_offset;
            return true;
        }
    }
    
    return false;
}

static bool fat_sync_dir_entry(fat_file_t* file) {
    uint8_t sector[512];

    if (!file || !file->fs) return false;
    if (!file->fs->dev->read(file->fs->dev, file->dir_sector, 1, sector))
        return false;

    fat_dir_entry_t* e =
        (fat_dir_entry_t*)(sector + file->dir_offset);

    e->size = file->size;
    e->cluster_low = file->cluster & 0xFFFF;

    if (file->fs->type == FAT32)
        e->cluster_high = (file->cluster >> 16) & 0xFFFF;

    return file->fs->dev->write(file->fs->dev, file->dir_sector, 1, sector);
}

uint32_t fat_write(fat_file_t* f, const void* buf, uint32_t bytes) {
    if (!f || !buf || !bytes) return 0;

    fat_fs_t* fs = f->fs;
    uint32_t cluster_size = fs->spc * fs->bps;
    const uint8_t* src = buf;
    uint32_t written = 0;

    if (f->cluster == 0) {
        uint32_t c = fat_find_free_cluster(fs);
        if (c == 0xFFFFFFFF) return 0;
        fat_set_fat_entry(fs, c, 0xFFFFFFFF);
        f->cluster = c;
    }

    uint32_t cur = f->cluster;
    uint32_t skip = f->pos / cluster_size;
    for (uint32_t i = 0; i < skip; i++) {
        uint32_t n = fat_get_next_cluster(fs, cur);
        if (n == 0xFFFFFFFF) {
            n = fat_find_free_cluster(fs);
            if (n == 0xFFFFFFFF) break;
            fat_set_fat_entry(fs, cur, n);
            fat_set_fat_entry(fs, n, 0xFFFFFFFF);
        }
        cur = n;
    }

    uint8_t sector[512];

    while (bytes) {
        uint32_t off = f->pos % cluster_size;
        uint32_t sec = off / fs->bps;
        uint32_t in = off % fs->bps;
        uint32_t lba = fat_cluster_to_sector(fs, cur) + sec;

        if (!fs->dev->read(fs->dev, lba, 1, sector)) break;

        uint32_t n = fs->bps - in;
        if (n > bytes) n = bytes;

        memcpy(sector + in, src, n);
        if (!fs->dev->write(fs->dev, lba, 1, sector)) break;

        src += n;
        bytes -= n;
        written += n;
        f->pos += n;
        if (f->pos > f->size) f->size = f->pos;

        if ((f->pos % cluster_size) == 0 && bytes) {
            uint32_t nx = fat_get_next_cluster(fs, cur);
            if (nx == 0xFFFFFFFF) {
                nx = fat_find_free_cluster(fs);
                if (nx == 0xFFFFFFFF) break;
                fat_set_fat_entry(fs, cur, nx);
                fat_set_fat_entry(fs, nx, 0xFFFFFFFF);
            }
            cur = nx;
        }
    }

    fat_sync_dir_entry(f);
    return written;
}