#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Page table entry flags
#define PAGE_PRESENT    (1ULL << 0)
#define PAGE_WRITE      (1ULL << 1)
#define PAGE_USER       (1ULL << 2)
#define PAGE_WRITETHROUGH (1ULL << 3)
#define PAGE_NOCACHE    (1ULL << 4)
#define PAGE_ACCESSED   (1ULL << 5)
#define PAGE_DIRTY      (1ULL << 6)
#define PAGE_HUGE       (1ULL << 7)
#define PAGE_GLOBAL     (1ULL << 8)
#define PAGE_NX         (1ULL << 63)
#define DEFAULT_PRIV_PAGE_FLAGS (PAGE_PRESENT | PAGE_WRITE)

// Page size
#define PAGE_SIZE       4096
#define PAGE_MASK       0xFFFFFFFFFFFFF000ULL

// Virtual memory layout
#define KERNEL_BASE     0xFFFFFFFF80000000ULL
#define USER_MAX        0x00007FFFFFFFFFFFULL

// Page table structure (all 4 levels use same structure)
typedef struct {
    uint64_t entries[512];
} __attribute__((aligned(PAGE_SIZE))) page_table_t;

// Address space structure
typedef struct {
    page_table_t* pml4;           // Top-level page table (physical address)
    void* pml4_virt;              // Virtual address for accessing PML4
} address_space_t;

// Initialize the VMM (sets up kernel page tables)
void vmm_init(void);

// Maps the userspace
void setup_user_space(void);

// Get the kernel address space
address_space_t* vmm_get_kernel_space(void);

// Create a new address space (for processes)
address_space_t* vmm_create_address_space(void);

// Destroy an address space
void vmm_destroy_address_space(address_space_t* space);

// Switch to an address space (loads CR3)
void vmm_switch_space(address_space_t* space);

// Map a virtual address to a physical address
bool vmm_map(address_space_t* space, void* virt, void* phys, uint64_t flags);

// Map multiple pages
bool vmm_map_range(address_space_t* space, void* virt, void* phys, size_t pages, uint64_t flags);

// Unmap a virtual address
void vmm_unmap(address_space_t* space, void* virt);

// Unmap multiple pages
void vmm_unmap_range(address_space_t* space, void* virt, size_t pages);

// Get physical address for a virtual address
void* vmm_get_physical(address_space_t* space, void* virt);

// Check if a virtual address is mapped
bool vmm_is_mapped(address_space_t* space, void* virt);

// Allocate and map a contiguous range of physical pages
void* vmm_map_contiguous(address_space_t* space, void* virt, size_t count, uint64_t flags);

// Helper functions for allocating and mapping pages
void* vmm_alloc_page(address_space_t* space, void* virt, uint64_t flags);
void* vmm_alloc_pages(address_space_t* space, void* virt, size_t count, uint64_t flags);
void vmm_free_page(address_space_t* space, void* virt);
void vmm_free_pages(address_space_t* space, void* virt, size_t count);

// TLB invalidation
static inline void vmm_invlpg(void* virt) {
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

// Get current CR3
static inline uint64_t vmm_get_cr3(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

// Set CR3
static inline void vmm_set_cr3(uint64_t cr3) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

#endif // VMM_H