#include "vmm.h"
#include "pmm.h"
#include <debug.h>
#include <string.h>
#include <memory.h>

extern uint64_t hhdm_offset;

// Helper macros
#define PHYS_TO_VIRT(addr) ((void*)((uint64_t)(addr) + hhdm_offset))
#define VIRT_TO_PHYS(addr) ((void*)((uint64_t)(addr) - hhdm_offset))

// Extract page table indices from virtual address
#define PML4_INDEX(addr) (((uint64_t)(addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr) (((uint64_t)(addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)   (((uint64_t)(addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)   (((uint64_t)(addr) >> 12) & 0x1FF)

// Page table entry manipulation
#define PTE_GET_ADDR(entry) ((entry) & PAGE_MASK)
#define PTE_GET_FLAGS(entry) ((entry) & ~PAGE_MASK)
#define PTE_CREATE(addr, flags) (((uint64_t)(addr) & PAGE_MASK) | (flags))

// Kernel address space
static address_space_t kernel_space = {0};

// Get or create a page table at the next level
static page_table_t* vmm_get_next_level(page_table_t* current, size_t index, bool create, uint64_t flags) {
    uint64_t entry = current->entries[index];
    
    // If entry exists, update flags and return it
    if (entry & PAGE_PRESENT) {
        // Update the entry to include new flags (important for USER bit propagation)
        current->entries[index] |= (flags & (PAGE_USER | PAGE_WRITE));
        
        void* phys = (void*)PTE_GET_ADDR(entry);
        return (page_table_t*)PHYS_TO_VIRT(phys);
    }
    
    // If not creating, return NULL
    if (!create) {
        return NULL;
    }
    
    // Allocate new page table
    void* phys = pmm_alloc();
    if (!phys) {
        log_crit("VMM", "Failed to allocate page table");
        return NULL;
    }
    
    // Clear the new page table
    page_table_t* table = (page_table_t*)PHYS_TO_VIRT(phys);
    memset(table, 0, PAGE_SIZE);
    
    // Set the entry with all required flags
    current->entries[index] = PTE_CREATE(phys, flags | PAGE_PRESENT);
    
    return table;
}

void vmm_init(void) {
    log_info("VMM", "Initializing virtual memory manager...");
    
    // Get current CR3 (Limine has already set up paging)
    uint64_t current_cr3 = vmm_get_cr3();
    void* pml4_phys = (void*)(current_cr3 & PAGE_MASK);
    
    log_info("VMM", "Current PML4 at physical: 0x%llx", (uint64_t)pml4_phys);
    
    // Use Limine's page tables as kernel space initially
    kernel_space.pml4 = (page_table_t*)pml4_phys;
    kernel_space.pml4_virt = PHYS_TO_VIRT(pml4_phys);
    
    log_ok("VMM", "Virtual memory manager initialized");
    log_info("VMM", "Kernel space PML4: 0x%llx (phys: 0x%llx)", 
             (uint64_t)kernel_space.pml4_virt, (uint64_t)kernel_space.pml4);
    setup_user_space();
}

void setup_user_space(void) {
    log_info("VMM", "Mapping user space at 0x400000...");
    
    address_space_t* kernel_space = vmm_get_kernel_space();
    
    for (uint64_t vaddr = 0x400000; vaddr < 0x800000; vaddr += PAGE_SIZE) {
        void* phys = pmm_alloc();
        
        if (!phys) {
            log_err("VMM", "Failed to allocate frame!");
            break;
        }
        
        uint64_t flags = PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        
        vmm_map(kernel_space, (void*)vaddr, phys, flags);
    }
    
    log_ok("VMM", "User space mapped (4MB at 0x400000)");
}

address_space_t* vmm_get_kernel_space(void) {
    return &kernel_space;
}

void* vmm_map_contiguous(address_space_t* space, void* virt, size_t count, uint64_t flags) {
    if (!count || !virt) {
        return NULL;
    }

    // Allocate a physically contiguous block of frames
    void* phys = pmm_alloc_pages(count);
    if (!phys) {
        log_err("VMM", "Failed to allocate %zu contiguous physical pages", count);
        return NULL;
    }

    // Map virtual -> physical contiguously
    if (!vmm_map_range(space, virt, phys, count, flags)) {
        log_err("VMM", "Failed to map contiguous range at 0x%llx", (uint64_t)virt);
        pmm_free_pages(phys, count);
        return NULL;
    }

    log_info("VMM", "Mapped %zu contiguous pages: virt=0x%llx -> phys=0x%llx",
             count, (uint64_t)virt, (uint64_t)phys);

    return virt;
}

address_space_t* vmm_create_address_space(void) {
    address_space_t* space = (address_space_t*)PHYS_TO_VIRT(pmm_alloc());
    if (!space) {
        log_crit("VMM", "Failed to allocate address space structure");
        return NULL;
    }
    
    // Allocate PML4
    void* pml4_phys = pmm_alloc();
    if (!pml4_phys) {
        log_crit("VMM", "Failed to allocate PML4");
        pmm_free(VIRT_TO_PHYS(space));
        return NULL;
    }
    
    space->pml4 = (page_table_t*)pml4_phys;
    space->pml4_virt = PHYS_TO_VIRT(pml4_phys);
    
    // Clear PML4
    memset(space->pml4_virt, 0, PAGE_SIZE);
    
    // Copy kernel mappings (upper half)
    page_table_t* kernel_pml4 = (page_table_t*)kernel_space.pml4_virt;
    page_table_t* new_pml4 = (page_table_t*)space->pml4_virt;
    
    // Copy entries 256-511 (higher half) from kernel space
    for (int i = 256; i < 512; i++) {
        new_pml4->entries[i] = kernel_pml4->entries[i];
    }
    
    log_info("VMM", "Created new address space at 0x%llx", (uint64_t)pml4_phys);
    
    return space;
}

void vmm_destroy_address_space(address_space_t* space) {
    if (!space || space == &kernel_space) {
        return;
    }
    
    page_table_t* pml4 = (page_table_t*)space->pml4_virt;
    
    // Free user space page tables (entries 0-255)
    for (int pml4e = 0; pml4e < 256; pml4e++) {
        if (!(pml4->entries[pml4e] & PAGE_PRESENT)) continue;
        
        page_table_t* pdpt = (page_table_t*)PHYS_TO_VIRT(PTE_GET_ADDR(pml4->entries[pml4e]));
        
        for (int pdpte = 0; pdpte < 512; pdpte++) {
            if (!(pdpt->entries[pdpte] & PAGE_PRESENT)) continue;
            
            page_table_t* pd = (page_table_t*)PHYS_TO_VIRT(PTE_GET_ADDR(pdpt->entries[pdpte]));
            
            for (int pde = 0; pde < 512; pde++) {
                if (!(pd->entries[pde] & PAGE_PRESENT)) continue;
                
                page_table_t* pt = (page_table_t*)PHYS_TO_VIRT(PTE_GET_ADDR(pd->entries[pde]));
                
                // Free the page table
                pmm_free(VIRT_TO_PHYS(pt));
            }
            
            // Free the page directory
            pmm_free(VIRT_TO_PHYS(pd));
        }
        
        // Free the PDPT
        pmm_free(VIRT_TO_PHYS(pdpt));
    }
    
    // Free PML4
    pmm_free(space->pml4);
    
    // Free address space structure
    pmm_free(VIRT_TO_PHYS(space));
}

void vmm_switch_space(address_space_t* space) {
    if (!space) return;
    vmm_set_cr3((uint64_t)space->pml4);
}

bool vmm_map(address_space_t* space, void* virt, void* phys, uint64_t flags) {
    if (!space) {
        space = &kernel_space;
    }
    
    // Ensure address is page-aligned
    uint64_t virt_addr = (uint64_t)virt & PAGE_MASK;
    uint64_t phys_addr = (uint64_t)phys & PAGE_MASK;
    
    // Get indices
    size_t pml4_idx = PML4_INDEX(virt_addr);
    size_t pdpt_idx = PDPT_INDEX(virt_addr);
    size_t pd_idx = PD_INDEX(virt_addr);
    size_t pt_idx = PT_INDEX(virt_addr);
    
    // Walk page tables
    page_table_t* pml4 = (page_table_t*)space->pml4_virt;
    
    page_table_t* pdpt = vmm_get_next_level(pml4, pml4_idx, true, PAGE_WRITE | PAGE_USER);
    if (!pdpt) return false;
    
    page_table_t* pd = vmm_get_next_level(pdpt, pdpt_idx, true, PAGE_WRITE | PAGE_USER);
    if (!pd) return false;
    
    page_table_t* pt = vmm_get_next_level(pd, pd_idx, true, PAGE_WRITE | PAGE_USER);
    if (!pt) return false;
    
    // Set the page table entry
    pt->entries[pt_idx] = PTE_CREATE(phys_addr, flags | PAGE_PRESENT);
    
    // Invalidate TLB
    vmm_invlpg((void*)virt_addr);
    
    return true;
}

bool vmm_map_range(address_space_t* space, void* virt, void* phys, size_t pages, uint64_t flags) {
    uint64_t virt_addr = (uint64_t)virt;
    uint64_t phys_addr = (uint64_t)phys;
    
    for (size_t i = 0; i < pages; i++) {
        if (!vmm_map(space, (void*)virt_addr, (void*)phys_addr, flags)) {
            // Rollback mappings on failure
            for (size_t j = 0; j < i; j++) {
                vmm_unmap(space, (void*)((uint64_t)virt + j * PAGE_SIZE));
            }
            return false;
        }
        virt_addr += PAGE_SIZE;
        phys_addr += PAGE_SIZE;
    }
    
    return true;
}

void vmm_unmap(address_space_t* space, void* virt) {
    if (!space) {
        space = &kernel_space;
    }
    
    uint64_t virt_addr = (uint64_t)virt & PAGE_MASK;
    
    // Get indices
    size_t pml4_idx = PML4_INDEX(virt_addr);
    size_t pdpt_idx = PDPT_INDEX(virt_addr);
    size_t pd_idx = PD_INDEX(virt_addr);
    size_t pt_idx = PT_INDEX(virt_addr);
    
    // Walk page tables
    page_table_t* pml4 = (page_table_t*)space->pml4_virt;
    
    page_table_t* pdpt = vmm_get_next_level(pml4, pml4_idx, false, 0);
    if (!pdpt) return;
    
    page_table_t* pd = vmm_get_next_level(pdpt, pdpt_idx, false, 0);
    if (!pd) return;
    
    page_table_t* pt = vmm_get_next_level(pd, pd_idx, false, 0);
    if (!pt) return;
    
    // Clear the entry
    pt->entries[pt_idx] = 0;
    
    // Invalidate TLB
    vmm_invlpg((void*)virt_addr);
}

void vmm_unmap_range(address_space_t* space, void* virt, size_t pages) {
    uint64_t virt_addr = (uint64_t)virt;
    
    for (size_t i = 0; i < pages; i++) {
        vmm_unmap(space, (void*)virt_addr);
        virt_addr += PAGE_SIZE;
    }
}

void* vmm_get_physical(address_space_t* space, void* virt) {
    if (!space) {
        space = &kernel_space;
    }
    
    uint64_t virt_addr = (uint64_t)virt;
    
    // Get indices
    size_t pml4_idx = PML4_INDEX(virt_addr);
    size_t pdpt_idx = PDPT_INDEX(virt_addr);
    size_t pd_idx = PD_INDEX(virt_addr);
    size_t pt_idx = PT_INDEX(virt_addr);
    
    // Walk page tables
    page_table_t* pml4 = (page_table_t*)space->pml4_virt;
    
    page_table_t* pdpt = vmm_get_next_level(pml4, pml4_idx, false, 0);
    if (!pdpt) return NULL;
    
    page_table_t* pd = vmm_get_next_level(pdpt, pdpt_idx, false, 0);
    if (!pd) return NULL;
    
    page_table_t* pt = vmm_get_next_level(pd, pd_idx, false, 0);
    if (!pt) return NULL;
    
    uint64_t entry = pt->entries[pt_idx];
    if (!(entry & PAGE_PRESENT)) {
        return NULL;
    }
    
    // Return physical address with offset
    uint64_t phys = PTE_GET_ADDR(entry);
    uint64_t offset = virt_addr & ~PAGE_MASK;
    return (void*)(phys | offset);
}

bool vmm_is_mapped(address_space_t* space, void* virt) {
    return vmm_get_physical(space, virt) != NULL;
}

void* vmm_alloc_page(address_space_t* space, void* virt, uint64_t flags) {
    void* phys = pmm_alloc();
    if (!phys) {
        return NULL;
    }
    
    if (!vmm_map(space, virt, phys, flags)) {
        pmm_free(phys);
        return NULL;
    }
    
    return virt;
}

void* vmm_alloc_pages(address_space_t* space, void* virt, size_t count, uint64_t flags) {
    // Allocate physical pages
    void* phys = pmm_alloc_pages(count);
    if (!phys) {
        return NULL;
    }
    
    // Map them
    if (!vmm_map_range(space, virt, phys, count, flags)) {
        pmm_free_pages(phys, count);
        return NULL;
    }
    
    return virt;
}

void vmm_free_page(address_space_t* space, void* virt) {
    void* phys = vmm_get_physical(space, virt);
    if (phys) {
        vmm_unmap(space, virt);
        pmm_free(phys);
    }
}

void vmm_free_pages(address_space_t* space, void* virt, size_t count) {
    uint64_t virt_addr = (uint64_t)virt;
    
    for (size_t i = 0; i < count; i++) {
        void* phys = vmm_get_physical(space, (void*)virt_addr);
        if (phys) {
            vmm_unmap(space, (void*)virt_addr);
            pmm_free(phys);
        }
        virt_addr += PAGE_SIZE;
    }
}