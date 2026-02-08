#pragma once
#include <drivers/fs/fat/fat.h>

void loadConfig(fat_fs_t* fs);
const char* config_get(const char* key, const char* fallback);