#ifndef MODULE_H
#define MODULE_H

typedef struct module {
    const char *name;
    int (*init)(void);
    void (*exit)(void);
} module_t;

#endif