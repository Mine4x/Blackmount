#ifndef CONFIG_H
#define CONFIG_H

int config_init(const char *path);
char *get_var(const char *key, const char *fallback);
void config_free(void);

#endif