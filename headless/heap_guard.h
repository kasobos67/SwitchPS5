// SPDX-License-Identifier: GPL-3.0-or-later
// Development-only trailing guards around the existing mspace allocator.
#include <string.h>
#include <limits.h>
#define EDEN_HEAP_GUARD_BYTES 32u
static size_t eden_guard_request(size_t size) {
    return size <= SIZE_MAX-EDEN_HEAP_GUARD_BYTES ? size+EDEN_HEAP_GUARD_BYTES : 0;
}
static void eden_guard_check(const void *p) {
    if (!p) return;
    size_t bytes=sceLibcMspaceMallocUsableSize(p);
    if (bytes >= EDEN_HEAP_GUARD_BYTES) {
        const unsigned char *tail=(const unsigned char *)p+bytes-EDEN_HEAP_GUARD_BYTES;
        for (unsigned i=0;i<EDEN_HEAP_GUARD_BYTES;++i)
            if (tail[i]!=0xd3) goto damaged;
        return;
    }
damaged:
    fprintf(stderr,"EDEN_HEAP_CORRUPTION pointer=%p usable=%zu caller=%p\n",
            p,bytes,__builtin_return_address(0));
    abort();
}
static void *eden_guard_mark(void *p) {
    if (p) {
        size_t bytes=sceLibcMspaceMallocUsableSize(p);
        if (bytes<EDEN_HEAP_GUARD_BYTES) abort();
        memset((unsigned char *)p+bytes-EDEN_HEAP_GUARD_BYTES,0xd3,EDEN_HEAP_GUARD_BYTES);
    }
    return p;
}
/* Diagnostic only: delay reuse, so writes after free remain attributable. */
#include <stdatomic.h>
#define EDEN_QUARANTINE_SLOTS 4096u
#define EDEN_QUARANTINE_BYTES (64u*1024u*1024u)
struct eden_retired_block { void *space,*p,*caller; size_t bytes; };
static struct eden_retired_block eden_retired[EDEN_QUARANTINE_SLOTS];
static size_t eden_retired_head,eden_retired_count,eden_retired_bytes;
static atomic_flag eden_retired_lock=ATOMIC_FLAG_INIT;
static void eden_retire_oldest(void) {
    struct eden_retired_block b=eden_retired[eden_retired_head];
    const unsigned char *data=b.p;
    for(size_t i=0;i<b.bytes;++i) if(data[i]!=0xd7) {
        atomic_flag_clear_explicit(&eden_retired_lock,memory_order_release);
        fprintf(stderr,"EDEN_HEAP_AFTER_FREE pointer=%p bytes=%zu offset=%zu value=%02x freed_by=%p\n",
                b.p,b.bytes,i,data[i],b.caller);
        abort();
    }
    eden_retired_head=(eden_retired_head+1)%EDEN_QUARANTINE_SLOTS;
    --eden_retired_count; eden_retired_bytes-=b.bytes;
    sceLibcMspaceFree(b.space,b.p);
}
static void eden_quarantine(void *space,void *p,void *caller) {
    if(!p) return;
    size_t bytes=sceLibcMspaceMallocUsableSize(p);
    while(atomic_flag_test_and_set_explicit(&eden_retired_lock,memory_order_acquire))
        __builtin_ia32_pause();
    while(eden_retired_count && (eden_retired_count==EDEN_QUARANTINE_SLOTS ||
          bytes>EDEN_QUARANTINE_BYTES-eden_retired_bytes)) eden_retire_oldest();
    if(bytes>EDEN_QUARANTINE_BYTES) {
        sceLibcMspaceFree(space,p);
    } else {
        memset(p,0xd7,bytes);
        eden_retired[(eden_retired_head+eden_retired_count)%EDEN_QUARANTINE_SLOTS]=
            (struct eden_retired_block){space,p,caller,bytes};
        ++eden_retired_count; eden_retired_bytes+=bytes;
    }
    atomic_flag_clear_explicit(&eden_retired_lock,memory_order_release);
}
void eden_heap_guard_drain(void) {
    while(atomic_flag_test_and_set_explicit(&eden_retired_lock,memory_order_acquire))
        __builtin_ia32_pause();
    while(eden_retired_count) eden_retire_oldest();
    atomic_flag_clear_explicit(&eden_retired_lock,memory_order_release);
}
static void *eden_guard_malloc(void *space,size_t size) {
    size_t n=eden_guard_request(size);
    if (!n) { errno=ENOMEM; return NULL; }
    return eden_guard_mark(sceLibcMspaceMalloc(space,n));
}
static void *eden_guard_calloc(void *space,size_t count,size_t size) {
    if (size && count>SIZE_MAX/size) { errno=ENOMEM; return NULL; }
    size_t n=eden_guard_request(count*size);
    if (!n) { errno=ENOMEM; return NULL; }
    return eden_guard_mark(sceLibcMspaceCalloc(space,1,n));
}
static void eden_guard_free(void *space,void *p) {
    eden_guard_check(p);
    eden_quarantine(space,p,__builtin_return_address(0));
}
static void *eden_guard_realloc(void *space,void *p,size_t size) {
    eden_guard_check(p);
    // Diagnostic build uses the permitted free-and-NULL realloc(p,0) behavior.
    if (p && !size) { eden_guard_free(space,p); return NULL; }
    size_t n=eden_guard_request(size);
    if (!n) { errno=ENOMEM; return NULL; }
    return eden_guard_mark(sceLibcMspaceRealloc(space,p,n));
}
static int eden_guard_memalign(void *space,void **p,size_t alignment,size_t size) {
    size_t n=eden_guard_request(size);
    if (!n) return ENOMEM;
    int result=sceLibcMspacePosixMemalign(space,p,alignment,n);
    if (!result) eden_guard_mark(*p);
    return result;
}
static size_t eden_guard_usable(const void *p) {
    eden_guard_check(p);
    return p ? sceLibcMspaceMallocUsableSize(p)-EDEN_HEAP_GUARD_BYTES : 0;
}
#define sceLibcMspaceMalloc eden_guard_malloc
#define sceLibcMspaceCalloc eden_guard_calloc
#define sceLibcMspaceRealloc eden_guard_realloc
#define sceLibcMspaceFree eden_guard_free
#define sceLibcMspacePosixMemalign eden_guard_memalign
#define sceLibcMspaceMallocUsableSize eden_guard_usable