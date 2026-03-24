#ifndef MODULE_H
#define MODULE_H

typedef struct module {
    const char *name;
    int (*start)(void);
    void (*exit)(void);
} module_t;

int module_init();
int module_register(module_t* mod);
void module_start();
void module_exit();

#endif