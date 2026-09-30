// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

/* Shared native-app allocator integration, originally used by the CTS runner. */
#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *address, size_t size);
void __real_free(void *address);
int __real_posix_memalign(void **address, size_t alignment, size_t size);
size_t __real_malloc_usable_size(const void *address);

void *sceLibcMspaceCreate(const char *name, void *base, size_t size,
                          unsigned flags);
void *sceLibcMspaceMalloc(void *mspace, size_t size);
void *sceLibcMspaceCalloc(void *mspace, size_t count, size_t size);
void *sceLibcMspaceRealloc(void *mspace, void *address, size_t size);
void sceLibcMspaceFree(void *mspace, void *address);
int sceLibcMspacePosixMemalign(void *mspace, void **address, size_t alignment,
                              size_t size);
size_t sceLibcMspaceMallocUsableSize(const void *address);

/* ponytail: fixed 128 MiB, process-lifetime heap; revisit the budget only for
 * measured application needs. Do not unmap underneath late C++ destructors. */
#define PS5_OPENGL_HEAP_SIZE (128u * 1024u * 1024u)

static atomic_int ps5_heap_state;
static void *ps5_heap_base;
static void *ps5_heap_mspace;
/* Owned-heap usable bytes only: not GPU mappings, foreign heaps or process RSS.
 * Relaxed snapshots are observations, not an allocator synchronization fence. */
static atomic_size_t ps5_heap_live_bytes, ps5_heap_peak_bytes, ps5_heap_blocks;
static atomic_size_t ps5_heap_failures, ps5_heap_ambiguous_zero_reallocs;

static void ps5_heap_resize_stats(size_t before, size_t after) {
  size_t live = after >= before
      ? atomic_fetch_add_explicit(&ps5_heap_live_bytes, after - before,
                                  memory_order_relaxed) + after - before
      : atomic_fetch_sub_explicit(&ps5_heap_live_bytes, before - after,
                                  memory_order_relaxed) - (before - after);
  size_t peak = atomic_load_explicit(&ps5_heap_peak_bytes, memory_order_relaxed);
  while (live > peak && !atomic_compare_exchange_weak_explicit(
      &ps5_heap_peak_bytes, &peak, live, memory_order_relaxed, memory_order_relaxed)) {}
}

static void *ps5_heap_record_allocation(void *address, int nonzero) {
  if (address) {
    ps5_heap_resize_stats(0, sceLibcMspaceMallocUsableSize(address));
    atomic_fetch_add_explicit(&ps5_heap_blocks, 1, memory_order_relaxed);
  } else if (nonzero) {
    atomic_fetch_add_explicit(&ps5_heap_failures, 1, memory_order_relaxed);
  }
  return address;
}

static int ps5_heap_ready(void) {
  int state = atomic_load_explicit(&ps5_heap_state, memory_order_acquire);
  if (state == 2)
    return 1;
  if (state != 0)
    return 0;

  int expected = 0;
  if (!atomic_compare_exchange_strong_explicit(
          &ps5_heap_state, &expected, 1, memory_order_acq_rel,
          memory_order_acquire))
    return expected == 2;

  void *base = mmap(NULL, PS5_OPENGL_HEAP_SIZE, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANON, -1, 0);
  if (base == MAP_FAILED) {
    atomic_store_explicit(&ps5_heap_state, -1, memory_order_release);
    return 0;
  }

  ps5_heap_base = base;
  ps5_heap_mspace =
      sceLibcMspaceCreate("PS5-OpenGL", base, PS5_OPENGL_HEAP_SIZE, 0);
  if (ps5_heap_mspace == NULL) {
    ps5_heap_base = NULL;
    munmap(base, PS5_OPENGL_HEAP_SIZE);
    atomic_store_explicit(&ps5_heap_state, -1, memory_order_release);
    return 0;
  }

  atomic_store_explicit(&ps5_heap_state, 2, memory_order_release);
  return 1;
}

static int ps5_heap_owns(const void *address) {
  /* Acquire publication before reading non-atomic heap metadata. */
  if (atomic_load_explicit(&ps5_heap_state, memory_order_acquire) != 2)
    return 0;
  uintptr_t value = (uintptr_t)address;
  uintptr_t base = (uintptr_t)ps5_heap_base;
  return value >= base && value - base < PS5_OPENGL_HEAP_SIZE;
}

void *__wrap_malloc(size_t size) {
  return ps5_heap_ready()
      ? ps5_heap_record_allocation(sceLibcMspaceMalloc(ps5_heap_mspace, size), size != 0)
      : __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size) {
  return ps5_heap_ready()
      ? ps5_heap_record_allocation(sceLibcMspaceCalloc(ps5_heap_mspace, count, size),
                                   count != 0 && size != 0)
      : __real_calloc(count, size);
}

void *__wrap_realloc(void *address, size_t size) {
  if (address == NULL)
    return __wrap_malloc(size);
  if (!ps5_heap_owns(address))
    return __real_realloc(address, size);
  size_t before = sceLibcMspaceMallocUsableSize(address);
  void *result = sceLibcMspaceRealloc(ps5_heap_mspace, address, size);
  if (result)
    ps5_heap_resize_stats(before, sceLibcMspaceMallocUsableSize(result));
  else if (size)
    atomic_fetch_add_explicit(&ps5_heap_failures, 1, memory_order_relaxed);
  else
    /* Preserve platform realloc(p,0) behavior; NULL does not tell us whether
     * p was freed. Mark the counters inconclusive instead of guessing. */
    atomic_fetch_add_explicit(&ps5_heap_ambiguous_zero_reallocs, 1, memory_order_relaxed);
  return result;
}

void __wrap_free(void *address) {
  if (ps5_heap_owns(address)) {
    ps5_heap_resize_stats(sceLibcMspaceMallocUsableSize(address), 0);
    atomic_fetch_sub_explicit(&ps5_heap_blocks, 1, memory_order_relaxed);
    sceLibcMspaceFree(ps5_heap_mspace, address);
  } else
    __real_free(address);
}

int __wrap_posix_memalign(void **address, size_t alignment, size_t size) {
  if (!ps5_heap_ready())
    return __real_posix_memalign(address, alignment, size);
  int result = sceLibcMspacePosixMemalign(ps5_heap_mspace, address, alignment, size);
  if (result == 0)
    ps5_heap_record_allocation(*address, size != 0);
  else if (result == ENOMEM)
    atomic_fetch_add_explicit(&ps5_heap_failures, 1, memory_order_relaxed);
  return result;
}

size_t __wrap_malloc_usable_size(const void *address) {
  return ps5_heap_owns(address) ? sceLibcMspaceMallocUsableSize(address)
                                : __real_malloc_usable_size(address);
}

void ps5_opengl_heap_stats_print(unsigned iteration) {
  if (iteration == 0) {
    int state = atomic_load_explicit(&ps5_heap_state, memory_order_acquire);
    printf("[ps5-opengl-cts] mspace state=%d base=%p size=%u\n",
           state, state == 2 ? ps5_heap_base : NULL, PS5_OPENGL_HEAP_SIZE);
  }
}

/* The native import converter rejects unresolved weak application symbols.
 * A diagnostic build's strong definition overrides this default no-op. */
__attribute__((weak)) void ps5_opengl_gpu_snapshot(const char *phase, unsigned iteration) {
  (void)phase;
  (void)iteration;
}
void ps5_opengl_heap_snapshot(const char *phase, unsigned iteration) {
  ps5_opengl_gpu_snapshot(phase, iteration);
  printf("[ps5-opengl-heap] phase=%s sample=%u state=%d live_bytes=%zu peak_bytes=%zu "
         "blocks=%zu failures=%zu ambiguous_zero_reallocs=%zu\n", phase, iteration,
         atomic_load_explicit(&ps5_heap_state, memory_order_acquire),
         atomic_load_explicit(&ps5_heap_live_bytes, memory_order_relaxed),
         atomic_load_explicit(&ps5_heap_peak_bytes, memory_order_relaxed),
         atomic_load_explicit(&ps5_heap_blocks, memory_order_relaxed),
         atomic_load_explicit(&ps5_heap_failures, memory_order_relaxed),
         atomic_load_explicit(&ps5_heap_ambiguous_zero_reallocs, memory_order_relaxed));
}
