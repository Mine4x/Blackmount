#pragma once
#include <stdint.h>

#define PAGE_SIZE 4096

// Page flags (x86_64)
#define PAGE_PRESENT  0x001
#define PAGE_RW       0x002
#define PAGE_USER     0x004
#define PAGE_PWT      0x008
#define PAGE_PCD      0x010
#define PAGE_ACCESSED 0x020
#define PAGE_DIRTY    0x040
#define PAGE_PS       0x080
#define PAGE_GLOBAL   0x100
#define PAGE_NX       (1ULL << 63)

// Paging entry types (64-bit)
typedef uint64_t pml4e_t;
typedef uint64_t pdpe_t;
typedef uint64_t pde_t;
typedef uint64_t pte_t;

// Paging structure sizes (512 entries each)
typedef pml4e_t pml4_t[512];
typedef pdpe_t  pdp_t[512];
typedef pde_t   page_directory_t[512];
typedef pte_t   page_table_t[512];

// Paging API
void paging_init(void);
void map_page(uint64_t virt, uint64_t phys, uint64_t flags);

// Process-specific address space management
pml4_t* create_page_directory(void);
void switch_page_directory(pml4_t* pml4);
void clone_kernel_mappings(pml4_t* dest);
void reset_page_directory(void);
uint64_t get_page_directory_phys(pml4_t* pml4);
