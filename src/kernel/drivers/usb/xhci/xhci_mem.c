#include "xhci_mem.h"
#include <mem/vmm.h>
#include <mem/dma.h>
#include <debug.h>
#include <memory.h>

uintptr_t xhci_map_mmio(uint64_t pci_bar_address, uint32_t bar_size)
{
    size_t page_count = bar_size / PAGE_SIZE;

    void* vbase = vmm_map_contiguous(vmm_get_kernel_space(), (void*)pci_bar_address, page_count, PAGE_NOCACHE | DEFAULT_PRIV_PAGE_FLAGS);

    return (uintptr_t)vbase;
}

void* alloc_xhci_memory(size_t size, size_t alingment, size_t boundary)
{
    if (size == 0) {
        log_err(XHCI_MEM_MODULE, "Attempted xHCI DMA allocation with size 0!");
        return NULL;
    }
    if (alingment == 0) {
        log_err(XHCI_MEM_MODULE, "Attempted xHCI DMA allocation with alingment 0!");
        return NULL;
    }
    if (boundary == 0) {
        log_err(XHCI_MEM_MODULE, "Attempted xHCI DMA allocation with boundary 0!");
        return NULL;
    }

    void* memblock = dma_alloc(size, alingment, boundary, DMA_ZONE_NORMAL);

    return memblock;
}

void free_xhci_memory(void* memblock)
{
    dma_free(memblock);
}

uintptr_t xhci_get_physical_addr(void* vaddr)
{
    void *virt = vmm_get_physical(vmm_get_kernel_space(), vaddr);

    return (uintptr_t)virt;
}
