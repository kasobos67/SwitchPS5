#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Host check of the heap that grows by pieces (headless/heap_arenas.inc).

Builds the app's heap source as headless/CMakeLists.txt derives it and runs it on Linux over a
stand-in for the console's mspace functions: a first-fit allocator with checked block headers.
Eight threads allocate, resize and free blocks of every size, each filled with a pattern that
is checked when the block changes hands, so an owner looked up wrongly, a block handed out
twice or a piece used before it has memory shows as a damaged pattern or header (or a fault:
the range is unreadable until committed). Then the limits: one request larger than a piece, a
large request when the console has no memory for a block of its own, the heap filled to its
3 GiB, and the same run with the range refused (the old whole-heap path).
"""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
heap = (root / 'third_party/app_heap.c').read_text()
for old, new in (
    ('128 MiB', '3072 MiB'),
    ('(128u * 1024u * 1024u)', '(3072u * 1024u * 1024u)'),
    ('#include <sys/mman.h>', '#include <sys/mman.h>\nvoid *eden_heap_pages(size_t);\nvoid eden_heap_pages_free(void *, size_t);'),
    ('"PS5-OpenGL"', '"Eden-headless"'),
):
    assert old in heap, old
    heap = heap.replace(old, new)
begin = heap.index('static int ps5_heap_ready(void) {')
end = heap.index('void ps5_opengl_heap_stats_print(unsigned iteration) {')
heap = heap[:begin] + (root / 'headless/heap_arenas.inc').read_text() + heap[end:]

MOCK = r'''
// A stand-in for the console: mspaces over caller memory, and the range the heap grows in.
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define BLOCK_MAGIC 0xb10cb10cb10cb10cull
#define ALIAS_MAGIC 0xa11a5a11a5a11a50ull
struct block { size_t size; uint64_t magic; struct block *next_free; uint64_t used; };  // 32 bytes before the data
struct space { pthread_mutex_t lock; struct block *free_list; char *start, *end; uint64_t magic; };
_Static_assert(sizeof(struct block) == 32, "header");

void *sceLibcMspaceCreate(const char *name, void *base, size_t size, unsigned flags) {
    (void)name; (void)flags;
    assert(size >= 4096 && ((uintptr_t)base & 15) == 0);
    struct space *space = base;
    pthread_mutex_init(&space->lock, NULL);
    space->magic = 0x5bace5bace5bace5ull;
    space->start = (char *)base + 128;
    space->end = (char *)base + size;
    struct block *first = (struct block *)space->start;
    first->size = (size_t)(space->end - space->start) - sizeof(struct block);
    first->magic = BLOCK_MAGIC; first->next_free = NULL; first->used = 0;
    space->free_list = first;
    return space;
}
static struct block *header_of(const void *address) {
    struct block *block = (struct block *)address - 1;
    if (block->magic == ALIAS_MAGIC) block = (struct block *)block->next_free;   // an aligned block: its real header
    if (block->magic != BLOCK_MAGIC) { fprintf(stderr, "damaged block header at %p\n", address); abort(); }
    return block;
}
// Joins neighbouring free blocks (the console's allocator does this as blocks are freed).
static void coalesce(struct space *space) {
    space->free_list = NULL;
    for (char *at = space->start; at < space->end;) {
        struct block *block = (struct block *)at;
        if (block->magic != BLOCK_MAGIC) { fprintf(stderr, "damaged block chain in a space\n"); abort(); }
        if (!block->used) {
            for (;;) {
                struct block *after = (struct block *)((char *)(block + 1) + block->size);
                if ((char *)after >= space->end || after->used) break;
                if (after->magic != BLOCK_MAGIC) { fprintf(stderr, "damaged block chain in a space\n"); abort(); }
                block->size += sizeof(struct block) + after->size;
                after->magic = 0;
            }
            block->next_free = space->free_list;
            space->free_list = block;
        }
        at = (char *)(block + 1) + block->size;
    }
}
static void *take(struct space *space, size_t size) {
    size = (size + 15) & ~(size_t)15;
    if (size < 32) size = 32;
    void *result = NULL;
    pthread_mutex_lock(&space->lock);
    assert(space->magic == 0x5bace5bace5bace5ull);
    for (int attempt = 0; attempt < 2 && result == NULL; ++attempt) {
    if (attempt == 1) coalesce(space);
    for (struct block **link = &space->free_list; *link != NULL; link = &(*link)->next_free) {
        struct block *block = *link;
        assert(block->magic == BLOCK_MAGIC && !block->used);
        if (block->size < size) continue;
        if (block->size >= size + sizeof(struct block) + 64) {
            struct block *rest = (struct block *)((char *)(block + 1) + size);
            rest->size = block->size - size - sizeof(struct block);
            rest->magic = BLOCK_MAGIC; rest->used = 0; rest->next_free = block->next_free;
            block->size = size;
            *link = rest;
        } else {
            *link = block->next_free;
        }
        block->used = 1; block->next_free = NULL;
        result = block + 1;
        break;
    }
    }
    pthread_mutex_unlock(&space->lock);
    return result;
}
void *sceLibcMspaceMalloc(void *space, size_t size) { return take(space, size); }
void *sceLibcMspaceCalloc(void *space, size_t count, size_t size) {
    void *address = take(space, count * size);
    return address ? memset(address, 0, count * size) : NULL;
}
size_t sceLibcMspaceMallocUsableSize(const void *address) {
    const struct block *alias = (const struct block *)address - 1;
    const struct block *block = header_of(address);
    assert(block->used == 1);
    return alias->magic == ALIAS_MAGIC ? alias->size : block->size;
}
void sceLibcMspaceFree(void *handle, void *address) {
    struct space *space = handle;
    struct block *block = header_of(address);
    // The block must lie in the space it is returned to: a wrong owner is the bug this check hunts.
    if ((char *)block < space->start || (char *)block >= space->end || block->used != 1) {
        fprintf(stderr, "block %p freed to a space that does not hold it\n", address); abort();
    }
    pthread_mutex_lock(&space->lock);
    block->used = 0;
    block->next_free = space->free_list;
    space->free_list = block;
    pthread_mutex_unlock(&space->lock);
}
void *sceLibcMspaceRealloc(void *space, void *address, size_t size) {
    const size_t before = sceLibcMspaceMallocUsableSize(address);
    if (size <= before && size != 0) return address;
    if (size == 0) { sceLibcMspaceFree(space, address); return NULL; }
    void *moved = take(space, size);
    if (moved == NULL) return NULL;   // like the console: the caller's block stays
    memcpy(moved, address, before);
    sceLibcMspaceFree(space, address);
    return moved;
}
int sceLibcMspacePosixMemalign(void *space, void **address, size_t alignment, size_t size) {
    if (alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0) return EINVAL;
    if (alignment <= 16) { *address = take(space, size); return *address ? 0 : ENOMEM; }
    char *raw = take(space, size + alignment + sizeof(struct block));
    if (raw == NULL) return ENOMEM;
    char *aligned = (char *)(((uintptr_t)raw + sizeof(struct block) + alignment - 1) & ~(uintptr_t)(alignment - 1));
    struct block *alias = (struct block *)aligned - 1;
    alias->magic = ALIAS_MAGIC; alias->next_free = (struct block *)raw - 1; alias->size = size; alias->used = 1;
    *address = aligned;
    return 0;
}
int sceKernelUsleep(unsigned int microseconds) { return usleep(microseconds); }

// The C library's own allocator, for what the app's heap does not own.
void *__real_malloc(size_t size) { return malloc(size); }
void *__real_calloc(size_t count, size_t size) { return calloc(count, size); }
void *__real_realloc(void *address, size_t size) { return realloc(address, size); }
void __real_free(void *address) { free(address); }
int __real_posix_memalign(void **address, size_t alignment, size_t size) { return posix_memalign(address, alignment, size); }
size_t malloc_usable_size(void *);
size_t __real_malloc_usable_size(const void *address) { return malloc_usable_size((void *)address); }

// The range the heap grows in: unreadable until committed, so a piece used early faults.
static int refuse_range;
static atomic_size_t committed_bytes;
void *eden_heap_reserve(size_t size) {
    if (refuse_range) return NULL;
    void *address = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return address == MAP_FAILED ? NULL : address;
}
int eden_heap_commit(void *address, size_t size) {
    assert(((uintptr_t)address & 0x1fffff) == 0 && (size & 0x1fffff) == 0);
    if (mprotect(address, size, PROT_READ | PROT_WRITE) != 0) return -1;
    atomic_fetch_add(&committed_bytes, size);
    return 0;
}
static int refuse_pages;   // the console has no memory left for a block of its own
void *eden_heap_pages(size_t size) {
    if (refuse_pages) return NULL;
    void *address = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return address == MAP_FAILED ? NULL : address;
}
void eden_heap_pages_free(void *address, size_t size) { munmap(address, size); }
'''

TEST = r'''
void *__wrap_malloc(size_t);
void *__wrap_calloc(size_t, size_t);
void *__wrap_realloc(void *, size_t);
void __wrap_free(void *);
int __wrap_posix_memalign(void **, size_t, size_t);
size_t __wrap_malloc_usable_size(const void *);
unsigned eden_heap_arenas_created(void);
size_t eden_heap_committed(void);
size_t eden_heap_large_held(unsigned *blocks);

static void fill(unsigned char *block, size_t size, unsigned seed) {
    // The start and the end of a large block are enough to see another owner writing into it.
    for (size_t i = 0; i < size; i = (i == 4095 && size > 8192) ? size - 4096 : i + 1)
        block[i] = (unsigned char)(seed + i * 131u);
}
static void verify(const unsigned char *block, size_t size, unsigned seed) {
    for (size_t i = 0; i < size; i = (i == 4095 && size > 8192) ? size - 4096 : i + 1)
        if (block[i] != (unsigned char)(seed + i * 131u)) { fprintf(stderr, "pattern damaged at %p+%zu\n", (void *)block, i); abort(); }
}
struct held { unsigned char *block; size_t size; unsigned seed; };
static atomic_ulong operations;
static void *worker(void *argument) {
    unsigned state = (unsigned)(uintptr_t)argument * 2654435761u + 1;
    enum { SLOTS = 200 };
    struct held held[SLOTS] = {0};
    for (int round = 0; round < 60000; ++round) {
        state = state * 1664525u + 1013904223u;
        struct held *slot = &held[(state >> 8) % SLOTS];
        const unsigned kind = (state >> 20) % 1000;
        size_t size = kind < 700 ? 1 + (state >> 4) % 2000              // small: the thread's arena
                    : kind < 930 ? 4096 + (state >> 4) % 400000          // medium: arena or the spaces
                    : kind < 997 ? 300000 + (size_t)(state >> 4) % 6000000
                                 : 20000000 + (size_t)(state >> 4) % 40000000;
        if (slot->block == NULL) {
            const unsigned how = (state >> 28) % 4;
            if (how == 0) {
                slot->block = __wrap_calloc(1, size);
                if (slot->block) for (size_t i = 0; i < size && i < 4096; ++i) assert(slot->block[i] == 0);
            } else if (how == 1) {
                void *address = NULL;
                const size_t alignment = (size_t)64 << ((state >> 12) % 8);
                if (__wrap_posix_memalign(&address, alignment, size) == 0) {
                    assert(((uintptr_t)address & (alignment - 1)) == 0);
                    slot->block = address;
                }
            } else {
                slot->block = __wrap_malloc(size);
            }
            if (slot->block == NULL) continue;   // the heap is full for this size right now
            assert(__wrap_malloc_usable_size(slot->block) >= size);
            slot->size = size; slot->seed = state;
            fill(slot->block, size, slot->seed);
        } else if ((state >> 30) & 1) {
            verify(slot->block, slot->size, slot->seed);
            unsigned char *moved = __wrap_realloc(slot->block, size);
            if (moved == NULL) continue;         // the old block stays
            // The start came along (the pattern covers a block's first and last 4096 bytes, so
            // only its start is known at the new size).
            const size_t kept = size < slot->size ? size : slot->size;
            verify(moved, kept < 4096 ? kept : 4096, slot->seed);
            slot->block = moved; slot->size = size; slot->seed = state;
            fill(moved, size, slot->seed);
        } else {
            verify(slot->block, slot->size, slot->seed);
            __wrap_free(slot->block);
            slot->block = NULL;
        }
        atomic_fetch_add(&operations, 1);
    }
    for (int i = 0; i < SLOTS; ++i)
        if (held[i].block) { verify(held[i].block, held[i].size, held[i].seed); __wrap_free(held[i].block); }
    return NULL;
}

int main(int argc, char **argv) {
    refuse_range = argc > 1 && strcmp(argv[1], "whole") == 0;
    const size_t piece = (size_t)128 << 20, heap = (size_t)3072 << 20;
    void *first = __wrap_malloc(100);
    assert(first != NULL);
    if (!refuse_range) {
        // It starts with one piece, not the whole heap.
        assert(eden_heap_committed() == piece && atomic_load(&committed_bytes) == piece);
        assert(__wrap_posix_memalign(&first, 24, 100) == EINVAL && eden_heap_committed() == piece);
    } else {
        assert(eden_heap_committed() == heap && atomic_load(&committed_bytes) == 0);
    }
    pthread_t threads[8];
    for (uintptr_t i = 0; i < 8; ++i) assert(pthread_create(&threads[i], NULL, worker, (void *)(i + 1)) == 0);
    for (int i = 0; i < 8; ++i) pthread_join(threads[i], NULL);
    const size_t after_threads = eden_heap_committed();
    const size_t peak = atomic_load(&ps5_heap_peak_bytes);
    assert(eden_heap_arenas_created() >= 8);
    if (!refuse_range) {
        assert(after_threads > piece && after_threads < heap && atomic_load(&committed_bytes) == after_threads);
        // A large block has memory of its own: the heap's pieces do not grow, and it is given
        // back when freed. It can grow in place of nothing: a resize moves it.
        unsigned blocks_alive = 99;
        assert(eden_heap_large_held(&blocks_alive) == 0 && blocks_alive == 0);
        unsigned char *large = __wrap_malloc(5 * piece / 2);
        assert(large != NULL && eden_heap_committed() == after_threads);
        assert(eden_heap_large_held(&blocks_alive) == 5 * piece / 2 && blocks_alive == 1);
        assert(__wrap_malloc_usable_size(large) == 5 * piece / 2);
        fill(large, 5 * piece / 2, 7);
        verify(large, 5 * piece / 2, 7);
        assert(__wrap_realloc(large, 2 * piece) == large);           // still most of it: it stays
        unsigned char *grown = __wrap_realloc(large, 3 * piece);      // larger: moved, the start kept
        assert(grown != NULL);
        verify(grown, 4096, 7);
        unsigned char *shrunk = __wrap_realloc(grown, 1000);          // small again: into an arena
        assert(shrunk != NULL && eden_heap_large_held(&blocks_alive) == 0 && blocks_alive == 0);
        verify(shrunk, 1000 < 4096 ? 1000 : 4096, 7);
        unsigned char *back = __wrap_realloc(shrunk, (size_t)40 << 20); // and out of the range again
        assert(back != NULL && eden_heap_large_held(&blocks_alive) == ((size_t)40 << 20));
        verify(back, 1000, 7);
        __wrap_free(back);
        assert(eden_heap_large_held(&blocks_alive) == 0 && eden_heap_committed() == after_threads);
        void *aligned = NULL;
        assert(__wrap_posix_memalign(&aligned, 4096, (size_t)33 << 20) == 0 && ((uintptr_t)aligned & 4095) == 0);
        assert(eden_heap_large_held(&blocks_alive) == ((size_t)33 << 20));
        __wrap_free(aligned);
        assert(__wrap_posix_memalign(&aligned, (size_t)1 << 22, (size_t)33 << 20) == 0 && ((uintptr_t)aligned & (((size_t)1 << 22) - 1)) == 0);
        assert(eden_heap_large_held(&blocks_alive) == 0);            // an alignment only a space can give
        __wrap_free(aligned);
        // With no memory left for a block of its own, a large request takes room in the heap.
        refuse_pages = 1;
        unsigned char *inside = __wrap_malloc((size_t)40 << 20);
        refuse_pages = 0;
        assert(inside != NULL && eden_heap_large_held(&blocks_alive) == 0 && blocks_alive == 0);
        fill(inside, (size_t)40 << 20, 9);
        unsigned char *outside = __wrap_realloc(inside, (size_t)48 << 20); // memory is back: moved out
        assert(outside != NULL && eden_heap_large_held(&blocks_alive) == ((size_t)48 << 20));
        verify(outside, 4096, 9);
        __wrap_free(outside);
        assert(eden_heap_large_held(&blocks_alive) == 0 && blocks_alive == 0);
    }
    // Fill the heap with blocks under the large size: the limit is its 3 GiB, and running out
    // returns NULL without damage.
    enum { MANY = 4096 };
    static unsigned char *blocks[MANY];
    int count = 0;
    while (count < MANY && (blocks[count] = __wrap_malloc((size_t)24 << 20)) != NULL) {
        fill(blocks[count], (size_t)24 << 20, (unsigned)count);
        ++count;
    }
    assert(count > 20 && count < MANY && eden_heap_committed() <= heap);
    assert(__wrap_malloc((size_t)-1 - 4096) == NULL);
    void *unaligned = NULL;
    assert(__wrap_posix_memalign(&unaligned, 4096, (size_t)-1 - 8192) != 0);
    for (int i = 0; i < count; ++i) { verify(blocks[i], (size_t)24 << 20, (unsigned)i); __wrap_free(blocks[i]); }
    // After that, everything still works.
    unsigned char *again = __wrap_malloc(1000);
    assert(again != NULL);
    fill(again, 1000, 3); verify(again, 1000, 3);
    __wrap_free(again);
    __wrap_free(first);
    printf("%s: %lu operations on 8 threads, %u arenas, most in use at once %zu MiB, %zu MiB of pieces after the threads, "
           "%zu MiB when full (%d blocks of 24 MiB)\n",
           refuse_range ? "whole heap at once" : "heap by pieces", atomic_load(&operations), eden_heap_arenas_created(),
           peak >> 20, after_threads >> 20, eden_heap_committed() >> 20, count);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='eden-heap-') as work:
    work = pathlib.Path(work)
    source = work / 'heap.c'
    source.write_text(MOCK + heap + TEST)
    for flags, label in ((['-O1', '-g', '-fsanitize=undefined', '-fno-sanitize-recover=all'], 'checked'),
                         (['-O2', '-fsanitize=thread'], 'thread sanitizer')):
        binary = work / ('heap-' + label.split()[0])
        subprocess.run(['clang-18', '-std=gnu11', '-pthread', '-Wall', '-Wextra', '-Wno-unused-function',
                        '-Wno-unused-parameter', *flags, str(source), '-o', str(binary)], check=True)
        for mode in ((), ('whole',)):
            if label != 'checked' and mode:
                continue
            result = subprocess.run([str(binary), *mode], capture_output=True, text=True, timeout=900)
            sys.stdout.write(f'[{label}] ' + result.stdout)
            if result.returncode != 0:
                sys.stderr.write(result.stderr[-3000:])
                sys.exit(f'heap check failed ({label} {" ".join(mode)}): exit {result.returncode}')
print('Heap by pieces: growth on demand, owners by address, requests larger than a piece, the 3 GiB limit, '
      'the whole-heap fallback and eight threads under the thread sanitizer PASS')
