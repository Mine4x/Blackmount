#include "fat.h"
#include <debug.h>

enum {
    DISK_TYPE_RAM = 0,
    DISK_TYPE_FLOPPY
};

FAT_Disk* init_disk(int type, uint8_t id) {
    FAT_Disk disk;
    disk.type = type;
    disk.id = id;

    if (disk.type == DISK_TYPE_RAM) {
        log_err("FAT", "RamDisk not supported by FAT driver"); 
    }
}