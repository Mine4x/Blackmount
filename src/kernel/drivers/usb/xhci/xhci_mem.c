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

dma_buf_t* alloc_xhci_memory(size_t size)
{
    if (size == 0) {
        log_err(XHCI_MEM_MODULE, "Attempted xHCI DMA allocation with size 0!");
        return NULL;
    }

    dma_buf_t* memblock = dma_alloc(size, DMA_ZONE_NORMAL);

    //void *ptr = memblock->virt;

    return memblock;
}

void free_xhci_memory(dma_buf_t* memblock)
{
    dma_free(memblock);
}

uintptr_t xhci_get_physical_addr(void* vaddr)
{
    void *virt = vmm_get_physical(vmm_get_kernel_space(), vaddr);

    return (uintptr_t)virt;
}
