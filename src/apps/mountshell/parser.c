#include "parser.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *key;
    char *value;
} entry;

static entry *entries = NULL;
static size_t entry_count = 0;
static size_t entry_capacity = 0;

static int is_space(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

static char *trim(char *str) {
    while (*str && is_space(*str)) str++;
    char *end = str;
    while (*end) end++;
    while (end > str && is_space(*(end - 1))) end--;
    *end = '\0';
    return str;
}

static void add_entry(char *key, char *value) {
     if (entry_count >= entry_capacity) {
        entry_capacity = entry_capacity ? entry_capacity * 2 : 16;
        entries = realloc(entries, entry_capacity * sizeof(entry));
    }
    entries[entry_count].key = strdup(key);
    entries[entry_count].value = strdup(value);
    entry_count++;
}

static void parse_line(char *line) {
    char *eq = strchr(line, '=');
    if (!eq) return;
    *eq = '\0';
    char *key = trim(line);
    char *value = trim(eq + 1);
    if (*key) add_entry(key, value);
}

int config_init(const char *path) {
    int fd = open(path);
    if (fd < 0) return -1;

    char buf[1024];
    char line[2048];
    int line_len = 0;

    size_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (size_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') {
                line[line_len] = '\0';
                char *t = trim(line);
                if (*t && *t != '#') parse_line(t);
                line_len = 0;
            } else {
                if (line_len < (int)(sizeof(line) - 1)) {
                    line[line_len++] = c;
                }
            }
        }
    }

    if (line_len > 0) {
        line[line_len] = '\0';
        char *t = trim(line);
        if (*t && *t != '#') parse_line(t);
    }

    close(fd);
    return 0;
}

char *get_var(const char *key, const char *fallback) {
    for (size_t i = 0; i < entry_count; i++) {
        if (strcmp(entries[i].key, key) == 0) {
            return entries[i].value;
        }
    }
    return (char *)fallback;
}

void config_free() {
    for (size_t i = 0; i < entry_count; i++) {
        free(entries[i].key);
        free(entries[i].value);
    }
    free(entries);
    entries = NULL;
    entry_count = 0;
    entry_capacity = 0;
}