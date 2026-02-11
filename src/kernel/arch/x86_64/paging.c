#include "paging.h"
#include <heap.h>
#include <string.h>
#include <memory.h>
#include <debug.h>

#define KERNEL_VIRT_BASE 0xffffffff80000000ULL
#define PHYS_TO_VIRT(addr) ((void*)((uint64_t)(addr) + KERNEL_VIRT_BASE))
#define VIRT_TO_PHYS(addr) ((uint64_t)(addr) - KERNEL_VIRT_BASE)

static pml4_t* kernel_pml4 = NULL;
static pml4_t* current_pml4 = NULL;
static uint64_t next_phys_page = 0x01000000; // Start at 16MB

extern uint8_t kernel_start;
extern uint8_t kernel_end;

static uint64_t alloc_phys_page(void) {
    uint64_t page = next_phys_page;
    next_phys_page += PAGE_SIZE;
    memset(PHYS_TO_VIRT(page), 0, PAGE_SIZE);
    return page;
}

static inline void load_cr3(uint64_t phys) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(phys) : "memory");
}

static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static inline void invlpg(uint64_t virt) {
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

static void map_page_in_pml4(pml4_t* pml4_virt, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_index = (virt >> 39) & 0x1FF;
    uint64_t pdp_index  = (virt >> 30) & 0x1FF;
    uint64_t pd_index   = (virt >> 21) & 0x1FF;
    uint64_t pt_index   = (virt >> 12) & 0x1FF;
    
    // Get or create PDP
    pdp_t* pdp_virt;
    if ((*pml4_virt)[pml4_index] & PAGE_PRESENT) {
        uint64_t pdp_phys = (*pml4_virt)[pml4_index] & ~0xFFF;
        pdp_virt = (pdp_t*)PHYS_TO_VIRT(pdp_phys);
    } else {
        uint64_t pdp_phys = alloc_phys_page();
        pdp_virt = (pdp_t*)PHYS_TO_VIRT(pdp_phys);
        (*pml4_virt)[pml4_index] = pdp_phys | PAGE_PRESENT | PAGE_RW;
    }
    
    // Get or create PD
    page_directory_t* pd_virt;
    if ((*pdp_virt)[pdp_index] & PAGE_PRESENT) {
        uint64_t pd_phys = (*pdp_virt)[pdp_index] & ~0xFFF;
        pd_virt = (page_directory_t*)PHYS_TO_VIRT(pd_phys);
    } else {
        uint64_t pd_phys = alloc_phys_page();
        pd_virt = (page_directory_t*)PHYS_TO_VIRT(pd_phys);
        (*pdp_virt)[pdp_index] = pd_phys | PAGE_PRESENT | PAGE_RW;
    }
    
    // Get or create PT
    page_table_t* pt_virt;
    if ((*pd_virt)[pd_index] & PAGE_PRESENT) {
        uint64_t pt_phys = (*pd_virt)[pd_index] & ~0xFFF;
        pt_virt = (page_table_t*)PHYS_TO_VIRT(pt_phys);
    } else {
        uint64_t pt_phys = alloc_phys_page();
        pt_virt = (page_table_t*)PHYS_TO_VIRT(pt_phys);
        (*pd_virt)[pd_index] = pt_phys | PAGE_PRESENT | PAGE_RW;
    }
    
    // Map the page
    (*pt_virt)[pt_index] = (phys & ~0xFFF) | (flags & 0xFFF) | PAGE_PRESENT;
}

void map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    map_page_in_pml4(current_pml4, virt, phys, flags);
    invlpg(virt);
}

void paging_init(void) {
    // Get current PML4 from CR3 (Limine already set up paging perfectly)
    uint64_t current_cr3 = read_cr3();
    uint64_t pml4_phys = current_cr3 & ~0xFFF;
    
    // Just use Limine's page tables - DON'T modify them yet
    kernel_pml4 = (pml4_t*)PHYS_TO_VIRT(pml4_phys);
    current_pml4 = kernel_pml4;
    
    log_ok("PAGING", "Using Limine page tables at phys 0x%llx", pml4_phys);
}

pml4_t* create_page_directory(void) {
    uint64_t pml4_phys = alloc_phys_page();
    pml4_t* pml4_virt = (pml4_t*)PHYS_TO_VIRT(pml4_phys);
    clone_kernel_mappings(pml4_virt);
    return pml4_virt;
}

void clone_kernel_mappings(pml4_t* dest) {
    // Copy upper half (kernel space)
    for (int i = 256; i < 512; i++) {
        (*dest)[i] = (*kernel_pml4)[i];
    }
}

void switch_page_directory(pml4_t* pml4_virt) {
    current_pml4 = pml4_virt;
    uint64_t phys = VIRT_TO_PHYS((uint64_t)pml4_virt);
    load_cr3(phys);
}

void reset_page_directory(void) {
    switch_page_directory(kernel_pml4);
}

uint64_t get_page_directory_phys(pml4_t* pml4_virt) {
    return VIRT_TO_PHYS((uint64_t)pml4_virt);
}