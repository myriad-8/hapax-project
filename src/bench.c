#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>
#include "common.h"
#include LOCK_HEADER

#ifndef LOCK_NAME
#define LOCK_NAME "unknown"
#endif
#define LAT_EVERY 64          /* sample every 64th acquisition's wait time */
#define LAT_CAP   (1u << 18)  /* max samples per thread */
#define QUIET_NCS 20000       /* non-critical work during "quiet" burst phases */

static lock_t g_lock;
static volatile uint64_t g_counter;      /* shared data protected by the lock */
static atomic_int g_stop;
static pthread_barrier_t g_bar;
static int g_cs = 0, g_ncs = 0, g_pin = 1, g_ncpu = 1;
static int g_cpus[4096];
static uint64_t g_burst_ticks = 0;
static int g_rand = 1;   /* randomize non-critical section length in [0,2*ncs) */

typedef struct __attribute__((aligned(64))) {
  int id;
  uint64_t ops;
  uint64_t *lat;
  size_t nlat;
} tdata_t;

static inline void spin(int n) {
  for (int i = 0; i < n; i++) __asm__ __volatile__("" ::: "memory");
}

static void *worker(void *arg) {
  tdata_t *t = (tdata_t *)arg;
  if (g_pin) {
    cpu_set_t s; CPU_ZERO(&s); CPU_SET(g_cpus[t->id % g_ncpu], &s);
    pthread_setaffinity_np(pthread_self(), sizeof s, &s);
  }
  pthread_barrier_wait(&g_bar);
  uint64_t ops = 0;
  uint64_t rng = 0x9E3779B97F4A7C15ull * (uint64_t)(t->id + 1);
  while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
    int sample = ((ops & (LAT_EVERY - 1)) == 0) && t->nlat < LAT_CAP;
    uint64_t t0 = sample ? rdtsc() : 0;
    lock_lock(&g_lock);
    if (sample) t->lat[t->nlat++] = rdtsc() - t0;
    g_counter++;
    spin(g_cs);
    lock_unlock(&g_lock);
    int ncs = g_ncs;
    if (g_burst_ticks && ((rdtsc() / g_burst_ticks) & 1)) ncs = QUIET_NCS;
    if (g_rand && ncs) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; ncs = (int)(rng % (2u * (unsigned)ncs)); }
    spin(ncs);
    ops++;
  }
  t->ops = ops;
  return NULL;
}

static int cmp_u64(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return (x > y) - (x < y);
}

static double now_ns(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e9 + ts.tv_nsec;
}

int main(int argc, char **argv) {
  int T = 4, rep = 0, burst_ms = 0, opt;
  double dur = 3.0;
  while ((opt = getopt(argc, argv, "t:d:c:n:p:r:B:R:H")) != -1) {
    switch (opt) {
      case 't': T = atoi(optarg); break;
      case 'd': dur = atof(optarg); break;
      case 'c': g_cs = atoi(optarg); break;
      case 'n': g_ncs = atoi(optarg); break;
      case 'p': g_pin = atoi(optarg); break;
      case 'r': rep = atoi(optarg); break;
      case 'B': burst_ms = atoi(optarg); break;
      case 'R': g_rand = atoi(optarg); break;
      case 'H':
        puts("lock,threads,cs,ncs,burst_ms,pin,rep,dur_s,total_ops,mops,p50_ns,p99_ns,max_ns,min_ops,max_ops,jain,ok");
        return 0;
      default:
        fprintf(stderr, "usage: %s -t threads -d secs -c cs_iters -n ncs_iters -p pin -r rep -B burst_ms\n", argv[0]);
        return 1;
    }
  }
  { cpu_set_t m; CPU_ZERO(&m); sched_getaffinity(0, sizeof m, &m);
    g_ncpu = 0;
    for (int c = 0; c < CPU_SETSIZE && g_ncpu < 4096; c++) if (CPU_ISSET(c, &m)) g_cpus[g_ncpu++] = c;
    if (g_ncpu == 0) { g_ncpu = 1; g_cpus[0] = 0; } }
  lock_init(&g_lock);

  if (burst_ms) {   /* calibrate tick rate to size burst phases */
    double a = now_ns(); uint64_t c0 = rdtsc();
    struct timespec ts = {0, 20 * 1000 * 1000}; nanosleep(&ts, NULL);
    double b = now_ns(); uint64_t c1 = rdtsc();
    g_burst_ticks = (uint64_t)((double)burst_ms * 1e6 * (double)(c1 - c0) / (b - a));
  }

  tdata_t *td = aligned_alloc(64, sizeof(tdata_t) * (size_t)T);
  pthread_t *th = malloc(sizeof(pthread_t) * (size_t)T);
  memset(td, 0, sizeof(tdata_t) * (size_t)T);
  pthread_barrier_init(&g_bar, NULL, (unsigned)T + 1);
  for (int i = 0; i < T; i++) {
    td[i].id = i;
    td[i].lat = malloc(sizeof(uint64_t) * LAT_CAP);
    pthread_create(&th[i], NULL, worker, &td[i]);
  }
  pthread_barrier_wait(&g_bar);
  double a = now_ns(); uint64_t c0 = rdtsc();
  struct timespec ts = {(time_t)dur, (long)((dur - (time_t)dur) * 1e9)};
  nanosleep(&ts, NULL);
  atomic_store(&g_stop, 1);
  double b = now_ns(); uint64_t c1 = rdtsc();
  for (int i = 0; i < T; i++) pthread_join(th[i], NULL);

  double elapsed = (b - a) / 1e9;
  double tpn = (double)(c1 - c0) / (b - a);   /* ticks per ns */

  uint64_t total = 0, mn = UINT64_MAX, mx = 0, nl = 0;
  double s1 = 0, s2 = 0;
  for (int i = 0; i < T; i++) {
    total += td[i].ops;
    if (td[i].ops < mn) mn = td[i].ops;
    if (td[i].ops > mx) mx = td[i].ops;
    s1 += (double)td[i].ops; s2 += (double)td[i].ops * (double)td[i].ops;
    nl += td[i].nlat;
  }
  double jain = s2 > 0 ? (s1 * s1) / ((double)T * s2) : 0;
  uint64_t *all = malloc(sizeof(uint64_t) * (nl ? nl : 1)), k = 0;
  for (int i = 0; i < T; i++)
    for (size_t j = 0; j < td[i].nlat; j++) all[k++] = td[i].lat[j];
  qsort(all, nl, sizeof(uint64_t), cmp_u64);
  double p50 = nl ? all[nl / 2] / tpn : 0;
  double p99 = nl ? all[(size_t)((double)nl * 0.99)] / tpn : 0;
  double pmax = nl ? all[nl - 1] / tpn : 0;

  int ok = (g_counter == total);   /* mutual exclusion sanity check */
  if (!ok) fprintf(stderr, "CORRECTNESS FAILURE: counter=%llu ops=%llu\n",
                   (unsigned long long)g_counter, (unsigned long long)total);

  printf("%s,%d,%d,%d,%d,%d,%d,%.3f,%llu,%.4f,%.0f,%.0f,%.0f,%llu,%llu,%.4f,%d\n",
         LOCK_NAME, T, g_cs, g_ncs, burst_ms, g_pin, rep, elapsed,
         (unsigned long long)total, (double)total / elapsed / 1e6,
         p50, p99, pmax, (unsigned long long)mn, (unsigned long long)mx, jain, ok);
  return ok ? 0 : 2;
}
