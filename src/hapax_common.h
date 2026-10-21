#pragma once
/*
 * Shared pieces of the Hapax family, ported from Dice & Kogan,
 * "Hapax Locks: Value-Based Mutual Exclusion" (arXiv 2511.14608 / PPoPP'26),
 * Listings 2 and 3.
 *
 *  - hapax values: 48-bit thread "zone" | 16-bit local sub-sequence,
 *    allocated in 64K blocks from one global allocator. 0 is never allocated.
 *  - one global waiting array, 4096 slots of 8 bytes, 4096-byte aligned
 *  - ToSlot(): take the zone (hapax >> 16), multiply by 17, mask.
 *    NOTE: the hash line is garbled in the copy of the paper I could read; the
 *    prose says "high-order bits, multiply by 17". VERIFY against the PDF.
 */
#include "common.h"

#define HAPAX_ARRAY_SIZE 4096u
#define HAPAX_BLOCK_MASK 0xFFFFull

typedef struct { atomic_uint_fast64_t v; } hapax_slot_t;

static hapax_slot_t g_waiting_array[HAPAX_ARRAY_SIZE] __attribute__((aligned(4096)));
static atomic_uint_fast64_t g_hapax_allocator __attribute__((aligned(128)));
static _Thread_local uint64_t tl_private_hapax = 0;

static inline uint64_t hapax_alloc(void) {
  uint64_t hapax = tl_private_hapax++;
  if (__builtin_expect((hapax & HAPAX_BLOCK_MASK) == 0, 0)) {
    /* crossed edge of block: reprovision from the global allocator */
    hapax = atomic_fetch_add(&g_hapax_allocator, 1) + 1;
    hapax <<= 16;
    tl_private_hapax = hapax + 1;
  }
  return hapax;
}

static inline hapax_slot_t *hapax_to_slot(uint64_t hapax, uint32_t salt) {
  return &g_waiting_array[(((hapax >> 16) * 17) + salt) & (HAPAX_ARRAY_SIZE - 1)];
}
