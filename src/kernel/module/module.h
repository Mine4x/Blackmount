#ifndef MODULE_H
#define MODULE_H

#include <stdbool.h>

typedef struct module {
    char name[214];
    bool enabled;
    int (*start)(void);
    void (*exit)(void);
} module_t;

int module_init();
int module_register(module_t* mod);
void module_start();
void module_exit();
void module_disable_others(const char* enabled_list);

#endif