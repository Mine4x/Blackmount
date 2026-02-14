#pragma once
#include <drivers/fs/fat/fat.h>

void loadConfig();
const char* config_get(const char* key, const char* fallback);