#pragma once
#include "hapax_common.h"
/* HapaxVW -- paper Listing 3 (visible waiters, assured positive handover). */
typedef struct {
  _Alignas(128) atomic_uint_fast64_t arrive;
  atomic_uint_fast64_t depart;
  uint64_t owner;
} lock_t;

static inline void lock_init(lock_t *l) {
  atomic_init(&l->arrive, 0); atomic_init(&l->depart, 0); l->owner = 0;
}
static inline uint32_t vw_salt(lock_t *l) { return (uint32_t)(uintptr_t)l; }

static inline void lock_lock(lock_t *l) {
  uint64_t hapax = hapax_alloc();
  uint64_t pred = atomic_exchange(&l->arrive, hapax);
  if (atomic_load_explicit(&l->depart, memory_order_acquire) != pred) {
    hapax_slot_t *slot = hapax_to_slot(pred, vw_salt(l));
    uint64_t expect = 0;
    if (!atomic_compare_exchange_strong(&slot->v, &expect, pred)) {
      /* slot occupied (hash collision): fall back to Tidex-style global spinning */
      while (atomic_load_explicit(&l->depart, memory_order_acquire) != pred) cpu_relax();
      goto enter;
    }
    /* registered as visible waiter; ratify against Depart to close race vs unlock */
    if (atomic_load(&l->depart) == pred) {
      expect = pred;
      atomic_compare_exchange_strong(&slot->v, &expect, 0);   /* unpublish */
      goto enter;
    }
    while (atomic_load_explicit(&slot->v, memory_order_acquire) == pred) cpu_relax();
  }
enter:
  l->owner = hapax;
}

static inline void lock_unlock(lock_t *l) {
  uint64_t hapax = l->owner;
  hapax_slot_t *slot = hapax_to_slot(hapax, vw_salt(l));
  uint64_t expect = hapax;
  if (atomic_compare_exchange_strong(&slot->v, &expect, 0)) return;  /* positive handover */
  atomic_store(&l->depart, hapax);                                   /* uncontended / collision / tardy waiter */
  expect = hapax;
  atomic_compare_exchange_strong(&slot->v, &expect, 0);              /* close race vs tardy waiter */
}
