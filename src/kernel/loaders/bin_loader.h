#pragma once
#include <stdint.h>

int bin_load_elf(const char *path, uint32_t priority, uint32_t parent);
int bin_load_elf_argv(const char *path, uint32_t priority, uint32_t parent,
                      int argc, const char **argv,
                      int envc, const char **envp);