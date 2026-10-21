#pragma once
#include "hapax_common.h"
/*
 * HapaxAD: YOUR adaptive layer, built ON TOP of HapaxVW (Listing 3). It only
 * adds behaviour the VW protocol already tolerates, so it stays correct.
 *
 *  (A) Contention-predicted unlock fast path ("rollback"):
 *      If this acquisition did not have to wait AND this thread's recent
 *      history says contention is rare (per-thread EMA, no shared writes),
 *      unlock first tries CAS(Arrive: hapax -> pred). If nobody arrived, the
 *      lock is back to unlocked state and we skip all waiting-array traffic.
 *      If the CAS fails a waiter exists -> normal VW unlock.
 *      (The paper only mentions the rollback as an optional optimisation; the
 *      history-gated policy and its evaluation are what is new here.)
 *
 *  (B) -DAD_VACATE=K : bounded visible waiting. A waiter spins visibly for K
 *      pauses; if no handover happened it vacates its slot (CAS pred->0) and
 *      falls back to spinning on Depart, freeing the slot for others.
 *      A failed vacate-CAS means handover already happened.
 *
 * NOT done on purpose: mixing invisible (IW) and visible (VW) waiters on the
 * SAME array is unsafe -- a VW waiter treats ANY change of its slot as
 * handover, so an IW unlocker storing into a colliding slot would grant it the
 * lock wrongly. A hybrid needs a separate array for each kind.
 */
#ifndef AD_EMA_MAX
#define AD_EMA_MAX 128          /* of 1024: skip rollback if >12.5% of recent acquisitions waited */
#endif

typedef struct {
  _Alignas(128) atomic_uint_fast64_t arrive;
  atomic_uint_fast64_t depart;
  uint64_t owner;       /* holder's hapax */
  uint64_t pred;        /* predecessor value seen at acquisition (for rollback) */
  uint32_t try_rollback;/* policy decision made at lock time */
} lock_t;

static _Thread_local uint32_t tl_ema = 0;   /* 0..1024: how often I had to wait recently */

static inline void lock_init(lock_t *l) {
  atomic_init(&l->arrive, 0); atomic_init(&l->depart, 0);
  l->owner = 0; l->pred = 0; l->try_rollback = 0;
}
static inline uint32_t ad_salt(lock_t *l) { return (uint32_t)(uintptr_t)l; }

static inline void lock_lock(lock_t *l) {
  uint64_t hapax = hapax_alloc();
  uint64_t pred = atomic_exchange(&l->arrive, hapax);
  int waited = 0;
  if (atomic_load_explicit(&l->depart, memory_order_acquire) != pred) {
    waited = 1;
    hapax_slot_t *slot = hapax_to_slot(pred, ad_salt(l));
    uint64_t expect = 0;
    if (!atomic_compare_exchange_strong(&slot->v, &expect, pred)) {
      while (atomic_load_explicit(&l->depart, memory_order_acquire) != pred) cpu_relax();
      goto enter;
    }
    if (atomic_load(&l->depart) == pred) {
      expect = pred;
      atomic_compare_exchange_strong(&slot->v, &expect, 0);
      goto enter;
    }
#ifdef AD_VACATE
    for (unsigned n = 0; atomic_load_explicit(&slot->v, memory_order_acquire) == pred; ) {
      cpu_relax();
      if (++n >= (unsigned)(AD_VACATE)) {
        expect = pred;
        if (atomic_compare_exchange_strong(&slot->v, &expect, 0)) {   /* vacated */
          while (atomic_load_explicit(&l->depart, memory_order_acquire) != pred) cpu_relax();
        }                                                             /* else: handover already happened */
        break;
      }
    }
#else
    while (atomic_load_explicit(&slot->v, memory_order_acquire) == pred) cpu_relax();
#endif
  }
enter:
  if (waited) tl_ema += (1024 - tl_ema) >> 3; else tl_ema -= tl_ema >> 3;
  l->owner = hapax;
  l->pred = pred;
  l->try_rollback = (!waited && tl_ema < AD_EMA_MAX);
}

static inline void lock_unlock(lock_t *l) {
  uint64_t hapax = l->owner;
  if (l->try_rollback) {
    uint64_t expect = hapax;
    if (atomic_compare_exchange_strong(&l->arrive, &expect, l->pred)) return;  /* nobody arrived */
  }
  hapax_slot_t *slot = hapax_to_slot(hapax, ad_salt(l));
  uint64_t expect = hapax;
  if (atomic_compare_exchange_strong(&slot->v, &expect, 0)) return;   /* positive handover */
  atomic_store(&l->depart, hapax);
  expect = hapax;
  atomic_compare_exchange_strong(&slot->v, &expect, 0);
}
