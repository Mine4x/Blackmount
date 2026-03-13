#include "dma.h"
#include "pmm.h"
#include "vmm.h"
#include <limine/limine_req.h>
#include <memory.h>
#include <util/spinlock.h>

#define DMA_VIRT_BASE        0xFFFFE00000000000ULL
#define DMA_VIRT_PAGE_COUNT  65536UL

#define ISA_LIMIT_PHYS       (16ULL * 1024 * 1024)

#define ISA_POOL_PAGES       64
#define ISA_POOL_MAX_TRIES   (ISA_POOL_PAGES * 8)

#define META_CAP             256

#define SPINLOCK_INIT { 0 }

#define DMA_MAP_FLAGS  (PAGE_PRESENT | PAGE_WRITE | PAGE_NOCACHE | PAGE_GLOBAL)

typedef struct {
    void    *virt;
    uint64_t phys;
    size_t   size;
    size_t   pages;
    int      zone;
    bool     from_isa_pool;
    bool     used;
} meta_t;

static uint64_t   virt_bitmap[DMA_VIRT_PAGE_COUNT / 64];
static spinlock_t virt_lock = SPINLOCK_INIT;

static meta_t     meta[META_CAP];
static spinlock_t meta_lock = SPINLOCK_INIT;

typedef struct isa_node {
    uint64_t phys;
    struct isa_node *next;
} isa_node_t;

static isa_node_t  isa_nodes[ISA_POOL_PAGES];
static isa_node_t *isa_free_list = NULL;
static int         isa_pool_size = 0;
static spinlock_t  isa_lock = SPINLOCK_INIT;

static void *virt_range_alloc(size_t pages) {
    spin_lock(&virt_lock);

    size_t run = 0, start = 0;
    for (size_t i = 0; i < DMA_VIRT_PAGE_COUNT; i++) {
        bool free = !(virt_bitmap[i >> 6] & (1ULL << (i & 63)));
        if (free) {
            if (!run) start = i;
            if (++run == pages) {
                for (size_t j = start; j <= i; j++)
                    virt_bitmap[j >> 6] |= 1ULL << (j & 63);
                spin_unlock(&virt_lock);
                return (void *)(DMA_VIRT_BASE + start * PAGE_SIZE);
            }
        } else {
            run = 0;
        }
    }

    spin_unlock(&virt_lock);
    return NULL;
}

static void virt_range_free(void *virt, size_t pages) {
    size_t start = ((uintptr_t)virt - DMA_VIRT_BASE) / PAGE_SIZE;
    spin_lock(&virt_lock);
    for (size_t i = start; i < start + pages; i++)
        virt_bitmap[i >> 6] &= ~(1ULL << (i & 63));
    spin_unlock(&virt_lock);
}

static void isa_pool_init(void) {
    for (int tries = 0;
         tries < ISA_POOL_MAX_TRIES && isa_pool_size < ISA_POOL_PAGES;
         tries++)
    {
        void *p = pmm_alloc();
        if (!p) break;

        if ((uint64_t)p >= ISA_LIMIT_PHYS) {
            pmm_free(p);
            continue;
        }

        isa_nodes[isa_pool_size].phys = (uint64_t)p;
        isa_nodes[isa_pool_size].next = isa_free_list;
        isa_free_list = &isa_nodes[isa_pool_size];
        isa_pool_size++;
    }
}

static uint64_t isa_page_alloc(void) {
    spin_lock(&isa_lock);
    if (!isa_free_list) {
        spin_unlock(&isa_lock);
        return 0;
    }
    isa_node_t *n = isa_free_list;
    isa_free_list = n->next;
    spin_unlock(&isa_lock);
    return n->phys;
}

static void isa_page_free(uint64_t phys) {
    for (int i = 0; i < isa_pool_size; i++) {
        if (isa_nodes[i].phys == phys) {
            spin_lock(&isa_lock);
            isa_nodes[i].next = isa_free_list;
            isa_free_list = &isa_nodes[i];
            spin_unlock(&isa_lock);
            return;
        }
    }
    pmm_free((void *)phys);
}

static meta_t *meta_alloc(void) {
    spin_lock(&meta_lock);
    for (int i = 0; i < META_CAP; i++) {
        if (!meta[i].used) {
            meta[i].used = true;
            spin_unlock(&meta_lock);
            return &meta[i];
        }
    }
    spin_unlock(&meta_lock);
    return NULL;
}

static meta_t *meta_find(void *virt) {
    for (int i = 0; i < META_CAP; i++) {
        if (meta[i].used && meta[i].virt == virt)
            return &meta[i];
    }
    return NULL;
}

static void meta_free(meta_t *m) {
    spin_lock(&meta_lock);
    m->used = false;
    spin_unlock(&meta_lock);
}

void dma_init(void) {
    isa_pool_init();
}

void *dma_alloc(size_t size,
                size_t alignment,
                size_t boundary,
                int zone)
{
    if (!size) return NULL;

    if (alignment && (alignment & (alignment - 1)))
        return NULL;

    if (boundary && (boundary & (boundary - 1)))
        return NULL;

    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (;;) {

        uint64_t phys = 0;
        void *phys_ptr = NULL;
        bool from_isa_pool = false;

        if (zone == DMA_ZONE_ISA) {
            if (pages == 1) {
                phys = isa_page_alloc();
                if (!phys) return NULL;
                from_isa_pool = true;
            } else {
                phys_ptr = pmm_alloc_pages(pages);
                if (!phys_ptr) return NULL;
                phys = (uint64_t)phys_ptr;
                if (phys >= ISA_LIMIT_PHYS)
                    goto retry;
            }
        } else {
            phys_ptr = pmm_alloc_pages(pages);
            if (!phys_ptr) return NULL;
            phys = (uint64_t)phys_ptr;
        }

        if (alignment && (phys & (alignment - 1)))
            goto retry;

        if (boundary) {
            uint64_t end = phys + size - 1;
            if ((phys & ~(boundary - 1)) != (end & ~(boundary - 1)))
                goto retry;
        }

        void *virt = virt_range_alloc(pages);
        if (!virt)
            goto retry;

        address_space_t *ks = vmm_get_kernel_space();
        if (!vmm_map_range(ks, virt, (void *)phys, pages, DMA_MAP_FLAGS)) {
            virt_range_free(virt, pages);
            goto retry;
        }

        memset(virt, 0, pages * PAGE_SIZE);

        meta_t *m = meta_alloc();
        if (!m) {
            vmm_unmap_range(ks, virt, pages);
            virt_range_free(virt, pages);
            goto retry;
        }

        m->virt = virt;
        m->phys = phys;
        m->size = size;
        m->pages = pages;
        m->zone = zone;
        m->from_isa_pool = from_isa_pool;

        return virt;

retry:
        if (from_isa_pool)
            isa_page_free(phys);
        else if (phys_ptr)
            pmm_free_pages((void *)phys, pages);
    }
}

void dma_free(void *ptr) {
    if (!ptr) return;

    meta_t *m = meta_find(ptr);
    if (!m) return;

    address_space_t *ks = vmm_get_kernel_space();

    vmm_unmap_range(ks, m->virt, m->pages);
    virt_range_free(m->virt, m->pages);

    if (m->from_isa_pool)
        isa_page_free(m->phys);
    else
        pmm_free_pages((void *)m->phys, m->pages);

    meta_free(m);
}

void dma_cache_flush(void *ptr, size_t size) {
    if (!ptr) return;

    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t end  = addr + size;
    addr &= ~(uintptr_t)63;

    for (; addr < end; addr += 64)
        __asm__ volatile("clflush (%0)" :: "r"(addr) : "memory");

    __asm__ volatile("mfence" ::: "memory");
}

void dma_cache_invalidate(void *ptr, size_t size) {
    dma_cache_flush(ptr, size);
}