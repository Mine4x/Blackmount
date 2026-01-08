#pragma once
#include <stdint.h>

#define PAGE_SIZE 4096

#define PAGE_PRESENT  0x1
#define PAGE_RW       0x2
#define PAGE_USER     0x4

void paging_init(void);
void map_page(uint32_t virt, uint32_t phys, uint32_t flags);
