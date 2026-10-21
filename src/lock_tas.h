#pragma once
#include "common.h"
/* Test-and-test-and-set spinlock */
typedef struct { _Alignas(CACHELINE) atomic_int f; } lock_t;
static inline void lock_init(lock_t *l) { atomic_init(&l->f, 0); }
static inline void lock_lock(lock_t *l) {
  for (;;) {
    while (atomic_load_explicit(&l->f, memory_order_relaxed)) cpu_relax();
    if (!atomic_exchange_explicit(&l->f, 1, memory_order_acquire)) return;
  }
}
static inline void lock_unlock(lock_t *l) { atomic_store_explicit(&l->f, 0, memory_order_release); }
