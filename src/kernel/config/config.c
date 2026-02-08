#include <drivers/fs/fat/fat.h>
#include <stdint.h>
#include "debug.h"

#define MAX_CONFIG_ENTRIES 64
#define MAX_KEY_LEN        32
#define MAX_VALUE_LEN      64

typedef struct {
    char key[MAX_KEY_LEN];
    char value[MAX_VALUE_LEN];
} config_entry_t;

static config_entry_t config_entries[MAX_CONFIG_ENTRIES];
static uint32_t config_entry_count = 0;

static int str_eq(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static void config_add(const char* key, const char* value) {
    if (config_entry_count >= MAX_CONFIG_ENTRIES)
        return;

    config_entry_t* e = &config_entries[config_entry_count++];

    uint32_t i = 0;
    while (key[i] && i < MAX_KEY_LEN - 1) {
        e->key[i] = key[i];
        i++;
    }
    e->key[i] = 0;

    i = 0;
    while (value[i] && i < MAX_VALUE_LEN - 1) {
        e->value[i] = value[i];
        i++;
    }
    e->value[i] = 0;
}

void loadConfig(fat_fs_t* fs) {
    fat_file_t configFile;

    if (!fat_open(fs, "config/config", &configFile)) {
        log_crit("Config", "Couln't open config file");
        return;
    }

    char buffer[512];
    uint32_t bytesRead = fat_read(&configFile, buffer, sizeof(buffer) - 1);
    buffer[bytesRead] = 0;

    char key[MAX_KEY_LEN];
    char value[MAX_VALUE_LEN];

    uint32_t i = 0;
    while (i < bytesRead) {
        uint32_t k = 0;
        uint32_t v = 0;

        while (buffer[i] != '=' && buffer[i] != '\n' && buffer[i] != 0) {
            if (k < MAX_KEY_LEN - 1)
                key[k++] = buffer[i];
            i++;
        }
        key[k] = 0;

        if (buffer[i] == '=')
            i++;

        while (buffer[i] != '\n' && buffer[i] != 0) {
            if (v < MAX_VALUE_LEN - 1)
                value[v++] = buffer[i];
            i++;
        }
        value[v] = 0;

        if (k > 0) {
            log_info("Config", "Got config %s with value %s", key, value);
            config_add(key, value);
        }

        if (buffer[i] == '\n')
            i++;
    }
}

const char* config_get(const char* key, const char* fallback) {
    for (uint32_t i = 0; i < config_entry_count; i++) {
        if (str_eq(config_entries[i].key, key))
            return config_entries[i].value;
    }
    return fallback;
}
