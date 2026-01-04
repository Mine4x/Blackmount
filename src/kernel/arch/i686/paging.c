#include "paging.h"
#include <heap.h>
#include <string.h>
#include <memory.h>
#include <debug.h>

static uint32_t* page_directory = 0;
static uint32_t next_phys_page = 0x00400000; // Start physical allocator at 4MB

static uint32_t alloc_phys_page(void) {
    uint32_t page = next_phys_page;
    next_phys_page += PAGE_SIZE;
    return page;
}

static inline void load_cr3(uint32_t phys) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(phys) : "memory");
}

static inline void enable_paging(void) {
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; // PG bit
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
}

static inline void invlpg(uint32_t virt) {
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

void map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x03FF;
    uint32_t* page_table;
    uint32_t pt_phys;

    if (page_directory[pd_index] & PAGE_PRESENT) {
        // Page table already exists - get its physical address
        pt_phys = page_directory[pd_index] & ~0xFFF;
        // Since paging isn't enabled yet, phys == virt for our structures
        page_table = (uint32_t*)pt_phys;
    } else {
        // Allocate new page table
        pt_phys = alloc_phys_page();
        page_table = (uint32_t*)pt_phys; // Identity mapped for now
        memset(page_table, 0, PAGE_SIZE);
        page_directory[pd_index] = pt_phys | PAGE_PRESENT | PAGE_RW;
    }

    page_table[pt_index] = (phys & ~0xFFF) | (flags & 0xFFF) | PAGE_PRESENT;
}

void paging_init(void) {
    // Allocate page directory at a known physical address
    uint32_t pd_phys = alloc_phys_page();
    page_directory = (uint32_t*)pd_phys;
    memset(page_directory, 0, PAGE_SIZE);

    /* Identity map first 16 MB (includes kernel, heap, and our page structures) */
    for (uint32_t addr = 0; addr < 0x01000000; addr += PAGE_SIZE) {
        map_page(addr, addr, PAGE_RW);
    }

    /* Also identity map where our physical allocator is allocating */
    for (uint32_t addr = 0x00400000; addr < next_phys_page; addr += PAGE_SIZE) {
        if (addr >= 0x01000000) { // If beyond 16MB
            map_page(addr, addr, PAGE_RW);
        }
    }

    load_cr3(pd_phys);
    enable_paging();
}