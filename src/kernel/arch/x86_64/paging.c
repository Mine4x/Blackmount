#include "paging.h"
#include <heap.h>
#include <string.h>
#include <memory.h>
#include <debug.h>

static pml4_t* kernel_pml4 = 0;
static pml4_t* current_pml4 = 0;
static uint64_t next_phys_page = 0x00400000; // Start physical allocator at 4MB

static uint64_t alloc_phys_page(void) {
    uint64_t page = next_phys_page;
    next_phys_page += PAGE_SIZE;
    return page;
}

static inline void load_cr3(uint64_t phys) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(phys) : "memory");
}

static inline void enable_paging(void) {
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; // PG bit
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
}

static inline void invlpg(uint64_t virt) {
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

// Map a page in a specific PML4 (or current if NULL)
static void map_page_in_pml4(pml4_t* pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!pml4) pml4 = current_pml4;
    
    // Extract indices from virtual address (48-bit canonical)
    uint64_t pml4_index = (virt >> 39) & 0x1FF;
    uint64_t pdp_index  = (virt >> 30) & 0x1FF;
    uint64_t pd_index   = (virt >> 21) & 0x1FF;
    uint64_t pt_index   = (virt >> 12) & 0x1FF;
    
    // Get or create PDP
    pdp_t* pdp;
    uint64_t pdp_phys;
    if ((*pml4)[pml4_index] & PAGE_PRESENT) {
        pdp_phys = (*pml4)[pml4_index] & ~0xFFF;
        pdp = (pdp_t*)pdp_phys; // Identity mapped for now
    } else {
        pdp_phys = alloc_phys_page();
        pdp = (pdp_t*)pdp_phys;
        memset(pdp, 0, PAGE_SIZE);
        (*pml4)[pml4_index] = pdp_phys | PAGE_PRESENT | PAGE_RW;
    }
    
    // Get or create PD
    page_directory_t* pd;
    uint64_t pd_phys;
    if ((*pdp)[pdp_index] & PAGE_PRESENT) {
        pd_phys = (*pdp)[pdp_index] & ~0xFFF;
        pd = (page_directory_t*)pd_phys;
    } else {
        pd_phys = alloc_phys_page();
        pd = (page_directory_t*)pd_phys;
        memset(pd, 0, PAGE_SIZE);
        (*pdp)[pdp_index] = pd_phys | PAGE_PRESENT | PAGE_RW;
    }
    
    // Get or create PT
    page_table_t* pt;
    uint64_t pt_phys;
    if ((*pd)[pd_index] & PAGE_PRESENT) {
        pt_phys = (*pd)[pd_index] & ~0xFFF;
        pt = (page_table_t*)pt_phys;
    } else {
        pt_phys = alloc_phys_page();
        pt = (page_table_t*)pt_phys;
        memset(pt, 0, PAGE_SIZE);
        (*pd)[pd_index] = pt_phys | PAGE_PRESENT | PAGE_RW;
    }
    
    // Map the actual page
    (*pt)[pt_index] = (phys & ~0xFFF) | (flags & 0xFFF) | PAGE_PRESENT;
}

void map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    map_page_in_pml4(current_pml4, virt, phys, flags);
}

void paging_init(void) {
    // Allocate PML4 at a known physical address
    uint64_t pml4_phys = alloc_phys_page();
    kernel_pml4 = (pml4_t*)pml4_phys;
    current_pml4 = kernel_pml4;
    memset(kernel_pml4, 0, PAGE_SIZE);
    
    /* Identity map first 16 MB (includes kernel, heap, and our page structures) */
    for (uint64_t addr = 0; addr < 0x01000000; addr += PAGE_SIZE) {
        map_page(addr, addr, PAGE_RW);
    }
    
    /* Also identity map where our physical allocator is allocating */
    for (uint64_t addr = 0x00400000; addr < next_phys_page; addr += PAGE_SIZE) {
        if (addr >= 0x01000000) { // If beyond 16MB
            map_page(addr, addr, PAGE_RW);
        }
    }
    
    load_cr3(pml4_phys);
    enable_paging();
}

pml4_t* create_page_directory(void) {
    uint64_t pml4_phys = alloc_phys_page();
    pml4_t* pml4 = (pml4_t*)pml4_phys;
    memset(pml4, 0, PAGE_SIZE);
    return pml4;
}

void clone_kernel_mappings(pml4_t* dest) {
    // Copy kernel space mappings (typically upper half - entries 256-511 for higher half kernel)
    // Adjust this range based on your kernel's memory layout
    // For higher half kernel, copy upper 256 entries; for lower half, copy lower entries
    for (int i = 256; i < 512; i++) {
        (*dest)[i] = (*kernel_pml4)[i];
    }
}

void switch_page_directory(pml4_t* pml4) {
    current_pml4 = pml4;
    uint64_t phys = (uint64_t)pml4; // Identity mapped
    load_cr3(phys);
}

void reset_page_directory(void) {
    current_pml4 = kernel_pml4;
    uint64_t phys = (uint64_t)kernel_pml4;
    load_cr3(phys);
}

uint64_t get_page_directory_phys(pml4_t* pml4) {
    return (uint64_t)pml4; // Identity mapped
}