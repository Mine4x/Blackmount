#pragma once
#include <stdint.h>

typedef struct {
    volatile uint32_t lock;
} spinlock_t;

static inline void spin_lock(spinlock_t *l) {
    while (1) {
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(&l->lock, &expected, 1, 0,
                                        __ATOMIC_ACQUIRE,
                                        __ATOMIC_RELAXED)) {
            return;
        }
        while (__atomic_load_n(&l->lock, __ATOMIC_RELAXED)) {
            __asm__ volatile("pause");
        }
    }
}

static inline void spin_unlock(spinlock_t *l) {
    __atomic_store_n(&l->lock, 0, __ATOMIC_RELEASE);
}