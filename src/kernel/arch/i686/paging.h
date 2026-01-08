#pragma once
#include <stdint.h>

#define PAGE_SIZE 4096
#define PAGE_PRESENT  0x1
#define PAGE_RW       0x2
#define PAGE_USER     0x4

// Page Directory Entry
typedef uint32_t pde_t;

// Page Table Entry
typedef uint32_t pte_t;

// Page Directory (1024 entries)
typedef pde_t page_directory_t[1024];

// Page Table (1024 entries)
typedef pte_t page_table_t[1024];

void paging_init(void);
void map_page(uint32_t virt, uint32_t phys, uint32_t flags);

// Process-specific page directory functions
page_directory_t* create_page_directory(void);
void switch_page_directory(page_directory_t* pd);
void clone_kernel_mappings(page_directory_t* dest);
void reset_page_directory(void);
uint32_t get_page_directory_phys(page_directory_t* pd);