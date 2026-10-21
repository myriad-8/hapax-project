#pragma once
#include "common.h"
typedef struct { pthread_mutex_t m; } lock_t;
static inline void lock_init(lock_t *l) { pthread_mutex_init(&l->m, NULL); }
static inline void lock_lock(lock_t *l) { pthread_mutex_lock(&l->m); }
static inline void lock_unlock(lock_t *l) { pthread_mutex_unlock(&l->m); }
