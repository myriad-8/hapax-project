#pragma once
#include "hapax_common.h"
/* Hapax Locks, invisible waiters (HapaxIW) -- paper Listing 2.
 * Arrive/Depart collocated on purpose (paper); 'owner' is the lock context the
 * paper keeps in the lock body in its LD_PRELOAD implementations. */
typedef struct {
  _Alignas(128) atomic_uint_fast64_t arrive;
  atomic_uint_fast64_t depart;
  uint64_t owner;                 /* hapax of current holder; touched only by holder */
} lock_t;

static inline void lock_init(lock_t *l) {
  atomic_init(&l->arrive, 0); atomic_init(&l->depart, 0); l->owner = 0;
}

static inline void lock_lock(lock_t *l) {
  uint64_t hapax = hapax_alloc();
  uint64_t pred = atomic_exchange(&l->arrive, hapax);
  uint64_t last_seen = 0;
  while (atomic_load_explicit(&l->depart, memory_order_acquire) != pred) {
    uint64_t verify = last_seen;
    for (;;) {                                  /* proxy waiting on the slot */
      last_seen = atomic_load_explicit(&hapax_to_slot(pred, 0)->v, memory_order_acquire);
      if (last_seen == pred) goto enter;        /* direct expedited handover */
      if (last_seen != verify) break;           /* slot changed: recheck Depart */
      cpu_relax();
    }
  }
enter:
  l->owner = hapax;
}

static inline void lock_unlock(lock_t *l) {
  uint64_t hapax = l->owner;
  atomic_store_explicit(&l->depart, hapax, memory_order_release);
  atomic_store_explicit(&hapax_to_slot(hapax, 0)->v, hapax, memory_order_release);
}
