#pragma once
#include "common.h"
/* Classic ticket lock: FIFO, but all waiters spin on one shared word */
typedef struct {
  _Alignas(CACHELINE) atomic_uint_fast64_t next;
  _Alignas(CACHELINE) atomic_uint_fast64_t serving;
} lock_t;
static inline void lock_init(lock_t *l) { atomic_init(&l->next, 0); atomic_init(&l->serving, 0); }
static inline void lock_lock(lock_t *l) {
  uint64_t t = atomic_fetch_add_explicit(&l->next, 1, memory_order_relaxed);
  while (atomic_load_explicit(&l->serving, memory_order_acquire) != t) cpu_relax();
}
static inline void lock_unlock(lock_t *l) {
  uint64_t s = atomic_load_explicit(&l->serving, memory_order_relaxed);
  atomic_store_explicit(&l->serving, s + 1, memory_order_release);
}
