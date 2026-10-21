#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>

#define CACHELINE 64

#if defined(__x86_64__)
#include <x86intrin.h>
static inline void cpu_relax(void) { __builtin_ia32_pause(); }
static inline uint64_t rdtsc(void) { return __rdtsc(); }
#elif defined(__aarch64__)
static inline void cpu_relax(void) { __asm__ __volatile__("yield"); }
static inline uint64_t rdtsc(void) { uint64_t v; __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(v)); return v; }
#else
#error "Unsupported architecture: add cpu_relax() and rdtsc()"
#endif
