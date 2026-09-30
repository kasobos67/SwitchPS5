/* Eden PS5 port: per-thread arenas carved from the owned direct-memory heap.
 *
 * One Sony mspace serialised every allocation in the process. Its lock collapses
 * under contention: a malloc/free pair costs 225 ns alone but 33/67/138 us with
 * 2/4/8 contending threads (EDEN_PERF_ALLOC),
 * which made allocation-heavy shader compilation ~100-250x slower than on a
 * Zen 2 host. A fixed split of the heap into arenas (run 220741) exhausted a
 * partition at level load, so capacity is not partitioned here: the whole heap
 * stays one mspace, and each allocating thread (up to EDEN_HEAP_MAX_ARENAS) gets
 * a lazily created 8 MiB arena allocated *from* it, aligned to its own 8 MiB
 * slot. Small requests use the thread's arena and fall back to the main space;
 * frees and reallocs find the owner in O(1) from the address's slot.
 */
#include <pthread.h>
#include <string.h>

#define EDEN_HEAP_ARENA_SHIFT 23u /* 8 MiB arenas and slots (<=256 MiB reserved) */
#define EDEN_HEAP_ARENA_SIZE (1u << EDEN_HEAP_ARENA_SHIFT)
/* Absolute 8 MiB slots covering the heap (+1: the base need not be slot aligned). */
#define EDEN_HEAP_SLOTS ((PS5_OPENGL_HEAP_SIZE >> EDEN_HEAP_ARENA_SHIFT) + 1u)
#define EDEN_HEAP_MAX_ARENAS 32u
/* Larger requests (and all over-aligned ones) always use the main space. */
#define EDEN_HEAP_SMALL_REQUEST (256u * 1024u)

static void *_Atomic eden_heap_slots[EDEN_HEAP_SLOTS]; /* arena mspace owning each slot */
static void *eden_heap_arena_list[EDEN_HEAP_MAX_ARENAS];
static atomic_uint eden_heap_arena_count;
static atomic_uint eden_heap_next_shared;
static pthread_key_t eden_heap_arena_key;
static int eden_heap_arena_key_valid;
static atomic_flag eden_heap_arena_create_lock = ATOMIC_FLAG_INIT;

unsigned eden_heap_arenas_created(void) {
  return atomic_load_explicit(&eden_heap_arena_count, memory_order_relaxed);
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

  void *base = eden_heap_pages(PS5_OPENGL_HEAP_SIZE);
  if (base == NULL) {
    atomic_store_explicit(&ps5_heap_state, -1, memory_order_release);
    return 0;
  }

  ps5_heap_base = base;
  ps5_heap_mspace =
      sceLibcMspaceCreate("Eden-headless", base, PS5_OPENGL_HEAP_SIZE, 0);
  if (ps5_heap_mspace == NULL) {
    ps5_heap_base = NULL;
    eden_heap_pages_free(base, PS5_OPENGL_HEAP_SIZE);
    atomic_store_explicit(&ps5_heap_state, -1, memory_order_release);
    return 0;
  }
  /* libthr keeps specific data in its own storage; no recursion into malloc. */
  eden_heap_arena_key_valid = pthread_key_create(&eden_heap_arena_key, NULL) == 0;

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

static uintptr_t eden_heap_slot(const void *address) {
  return ((uintptr_t)address >> EDEN_HEAP_ARENA_SHIFT) -
         ((uintptr_t)ps5_heap_base >> EDEN_HEAP_ARENA_SHIFT);
}

/* The mspace owning an owned block: its slot's arena, else the main space. */
static void *eden_heap_owner(const void *address) {
  const uintptr_t slot = eden_heap_slot(address);
  void *arena = atomic_load_explicit(&eden_heap_slots[slot], memory_order_acquire);
  return arena != NULL ? arena : ps5_heap_mspace;
}

/* Allocate and register one arena block from the main space (NULL on failure). */
static void *eden_heap_create_arena(void) {
  while (atomic_flag_test_and_set_explicit(&eden_heap_arena_create_lock, memory_order_acquire)) {}
  void *arena = NULL;
  const unsigned count = atomic_load_explicit(&eden_heap_arena_count, memory_order_relaxed);
  if (count < EDEN_HEAP_MAX_ARENAS) {
    void *block = NULL;
    if (sceLibcMspacePosixMemalign(ps5_heap_mspace, &block, EDEN_HEAP_ARENA_SIZE,
                                   EDEN_HEAP_ARENA_SIZE) == 0 && block != NULL) {
      arena = sceLibcMspaceCreate("Eden-arena", block, EDEN_HEAP_ARENA_SIZE, 0);
      if (arena != NULL) {
        const uintptr_t slot = eden_heap_slot(block);
        eden_heap_arena_list[count] = arena;
        atomic_store_explicit(&eden_heap_slots[slot], arena, memory_order_release);
        atomic_store_explicit(&eden_heap_arena_count, count + 1, memory_order_release);
      } else {
        sceLibcMspaceFree(ps5_heap_mspace, block);
      }
    }
  }
  atomic_flag_clear_explicit(&eden_heap_arena_create_lock, memory_order_release);
  return arena;
}

/* This thread's arena, created on its first small allocation. NULL = main space. */
static void *eden_heap_thread_arena(void) {
  if (!eden_heap_arena_key_valid)
    return NULL;
  const uintptr_t stored = (uintptr_t)pthread_getspecific(eden_heap_arena_key);
  if (stored != 0)
    return eden_heap_arena_list[stored - 1];
  void *arena = eden_heap_create_arena();
  unsigned index;
  if (arena != NULL) {
    index = atomic_load_explicit(&eden_heap_arena_count, memory_order_acquire) - 1;
    while (eden_heap_arena_list[index] != arena)
      --index; /* another thread may have appended meanwhile */
  } else {
    /* Arena limit reached: share the existing arenas round-robin. */
    const unsigned count = atomic_load_explicit(&eden_heap_arena_count, memory_order_acquire);
    if (count == 0)
      return NULL;
    index = atomic_fetch_add_explicit(&eden_heap_next_shared, 1, memory_order_relaxed) % count;
    arena = eden_heap_arena_list[index];
  }
  pthread_setspecific(eden_heap_arena_key, (void *)(uintptr_t)(index + 1));
  return arena;
}

static void *eden_heap_allocate(size_t count, size_t size, int zero) {
  const size_t bytes = count * size;
  if (bytes < EDEN_HEAP_SMALL_REQUEST) {
    void *arena = eden_heap_thread_arena();
    if (arena != NULL) {
      void *address = zero ? sceLibcMspaceCalloc(arena, count, size)
                           : sceLibcMspaceMalloc(arena, bytes);
      if (address != NULL)
        return address;
    }
  }
  return zero ? sceLibcMspaceCalloc(ps5_heap_mspace, count, size)
              : sceLibcMspaceMalloc(ps5_heap_mspace, bytes);
}

void *__wrap_malloc(size_t size) {
  return ps5_heap_ready()
      ? ps5_heap_record_allocation(eden_heap_allocate(1, size, 0), size != 0)
      : __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size) {
  if (!ps5_heap_ready())
    return __real_calloc(count, size);
  if (size != 0 && count > (size_t)-1 / size) {
    atomic_fetch_add_explicit(&ps5_heap_failures, 1, memory_order_relaxed);
    return NULL;
  }
  return ps5_heap_record_allocation(eden_heap_allocate(count, size, 1),
                                    count != 0 && size != 0);
}

void *__wrap_realloc(void *address, size_t size) {
  if (address == NULL)
    return __wrap_malloc(size);
  if (!ps5_heap_owns(address))
    return __real_realloc(address, size);
  void *owner = eden_heap_owner(address);
  size_t before = sceLibcMspaceMallocUsableSize(address);
  void *result = sceLibcMspaceRealloc(owner, address, size);
  if (result) {
    ps5_heap_resize_stats(before, sceLibcMspaceMallocUsableSize(result));
    return result;
  }
  if (size == 0) {
    /* Preserve platform realloc(p,0) behavior; NULL does not tell us whether
     * p was freed. Mark the counters inconclusive instead of guessing. */
    atomic_fetch_add_explicit(&ps5_heap_ambiguous_zero_reallocs, 1, memory_order_relaxed);
    return NULL;
  }
  if (owner == ps5_heap_mspace) {
    atomic_fetch_add_explicit(&ps5_heap_failures, 1, memory_order_relaxed);
    return NULL;
  }
  /* A full arena cannot grow the block in place: move it. */
  void *moved = eden_heap_allocate(1, size, 0);
  if (moved == NULL) {
    atomic_fetch_add_explicit(&ps5_heap_failures, 1, memory_order_relaxed);
    return NULL;
  }
  memcpy(moved, address, before < size ? before : size);
  ps5_heap_resize_stats(before, sceLibcMspaceMallocUsableSize(moved));
  sceLibcMspaceFree(owner, address);
  return moved;
}

void __wrap_free(void *address) {
  if (ps5_heap_owns(address)) {
    ps5_heap_resize_stats(sceLibcMspaceMallocUsableSize(address), 0);
    atomic_fetch_sub_explicit(&ps5_heap_blocks, 1, memory_order_relaxed);
    sceLibcMspaceFree(eden_heap_owner(address), address);
  } else
    __real_free(address);
}

int __wrap_posix_memalign(void **address, size_t alignment, size_t size) {
  if (!ps5_heap_ready())
    return __real_posix_memalign(address, alignment, size);
  int result = ENOMEM;
  if (size < EDEN_HEAP_SMALL_REQUEST && alignment <= 4096) {
    void *arena = eden_heap_thread_arena();
    if (arena != NULL)
      result = sceLibcMspacePosixMemalign(arena, address, alignment, size);
  }
  if (result == ENOMEM)
    result = sceLibcMspacePosixMemalign(ps5_heap_mspace, address, alignment, size);
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

