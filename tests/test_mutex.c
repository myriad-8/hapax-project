#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "common.h"
#include LOCK_HEADER

#ifndef LOCK_NAME
#define LOCK_NAME "unknown"
#endif

static lock_t A, B;
static uint64_t cntA, cntB;      /* plain (non-atomic) on purpose: the lock must protect them */
static atomic_int in_cs;
static int iters;

static void fail(const char *m) { fprintf(stderr, "[%s] FAIL: %s\n", LOCK_NAME, m); exit(1); }

static void *w_single(void *arg) {
  (void)arg;
  for (int i = 0; i < iters; i++) {
    lock_lock(&A);
    if (atomic_exchange(&in_cs, 1)) fail("two threads inside critical section");
    cntA++;
    atomic_store(&in_cs, 0);
    lock_unlock(&A);
  }
  return NULL;
}

static void *w_multi(void *arg) {   /* hold A and B, release in random order */
  unsigned seed = (unsigned)(uintptr_t)arg * 2654435761u + 1;
  for (int i = 0; i < iters; i++) {
    lock_lock(&A); lock_lock(&B);
    cntA++; cntB++;
    if (rand_r(&seed) & 1) { lock_unlock(&A); lock_unlock(&B); }
    else                   { lock_unlock(&B); lock_unlock(&A); }
  }
  return NULL;
}

static void run(void *(*fn)(void *), int T) {
  pthread_t *th = malloc(sizeof(pthread_t) * (size_t)T);
  for (int i = 0; i < T; i++) pthread_create(&th[i], NULL, fn, (void *)(uintptr_t)(i + 1));
  for (int i = 0; i < T; i++) pthread_join(th[i], NULL);
  free(th);
}

int main(int argc, char **argv) {
  int T = argc > 1 ? atoi(argv[1]) : 8;
  iters = argc > 2 ? atoi(argv[2]) : 200000;
  lock_init(&A); lock_init(&B);

  run(w_single, T);
  if (cntA != (uint64_t)T * (uint64_t)iters) fail("single-lock counter mismatch");

  cntA = cntB = 0;
  run(w_multi, T);
  if (cntA != (uint64_t)T * (uint64_t)iters || cntB != cntA) fail("multi-lock counter mismatch");

  printf("[%s] PASS (threads=%d iters=%d)\n", LOCK_NAME, T, iters);
  return 0;
}
