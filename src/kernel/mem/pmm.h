#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PAGE_SIZE 4096

// Initialize the physical memory manager
void pmm_init(void);

// Allocate a physical page (returns physical address)
void* pmm_alloc(void);

// Allocate multiple contiguous physical pages
void* pmm_alloc_pages(size_t count);

// Free a physical page
void pmm_free(void* page);

// Free multiple contiguous physical pages
void pmm_free_pages(void* page, size_t count);

// Get memory statistics
uint64_t pmm_get_total_memory(void);
uint64_t pmm_get_used_memory(void);
uint64_t pmm_get_free_memory(void);

#endif // PMM_H