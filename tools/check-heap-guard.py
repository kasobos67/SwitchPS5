#!/usr/bin/env python3
"""Check the development mspace guard against real host allocations."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
code=r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
static void *sceLibcMspaceMalloc(void *s,size_t n) { (void)s; return malloc(n); }
static void *sceLibcMspaceCalloc(void *s,size_t n,size_t z) { (void)s; return calloc(n,z); }
static void *sceLibcMspaceRealloc(void *s,void *p,size_t n) { (void)s; return realloc(p,n); }
static void sceLibcMspaceFree(void *s,void *p) { (void)s; free(p); }
static int sceLibcMspacePosixMemalign(void *s,void **p,size_t a,size_t n) { (void)s; return posix_memalign(p,a,n); }
static size_t sceLibcMspaceMallocUsableSize(const void *p) { return malloc_usable_size((void *)p); }
'''
code += (root/'headless/heap_guard.h').read_text()
code += r'''
int main(void) {
    unsigned char *p=sceLibcMspaceCalloc(NULL,31,7); assert(p);
    for (unsigned i=0;i<217;++i) assert(p[i]==0);
    memset(p,0x46,217);
    p=sceLibcMspaceRealloc(NULL,p,4096); assert(p);
    for (unsigned i=0;i<217;++i) assert(p[i]==0x46);
    assert(sceLibcMspaceMallocUsableSize(p)>=4096);
    assert(!sceLibcMspaceRealloc(NULL,p,SIZE_MAX));
    sceLibcMspaceFree(NULL,p);
    void *aligned=NULL;
    assert(!sceLibcMspacePosixMemalign(NULL,&aligned,256,103));
    assert((uintptr_t)aligned%256==0);
    memset(aligned,7,103); sceLibcMspaceFree(NULL,aligned);
    assert(!sceLibcMspaceMalloc(NULL,SIZE_MAX));
    assert(!sceLibcMspaceCalloc(NULL,SIZE_MAX,2));
    assert(sceLibcMspacePosixMemalign(NULL,&aligned,256,SIZE_MAX)==ENOMEM);
    p=sceLibcMspaceMalloc(NULL,0); assert(p); sceLibcMspaceFree(NULL,p);
    p=sceLibcMspaceMalloc(NULL,80); assert(p);
    assert(!sceLibcMspaceRealloc(NULL,p,0));
    for (int action=0;action<3;++action) {
        pid_t child=fork(); assert(child>=0);
        if (!child) {
            p=sceLibcMspaceMalloc(NULL,48); assert(p);
            p[sceLibcMspaceMallocUsableSize(p)]=0; /* Inside allocation, corrupt guard. */
            if(action==0) sceLibcMspaceFree(NULL,p);
            if(action==1) (void)sceLibcMspaceRealloc(NULL,p,90);
            if(action==2) (void)sceLibcMspaceMallocUsableSize(p);
            _exit(99);
        }
        int status; assert(waitpid(child,&status,0)==child);
        assert(WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT);
    }
    pid_t child=fork(); assert(child>=0);
    if(!child) {
        p=sceLibcMspaceMalloc(NULL,64); assert(p);
        sceLibcMspaceFree(NULL,p);
        p[9]=0x42;
        eden_heap_guard_drain();
        _exit(99);
    }
    int status; assert(waitpid(child,&status,0)==child);
    assert(WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT);
    for(unsigned i=0;i<EDEN_QUARANTINE_SLOTS+5;++i) {
        p=sceLibcMspaceMalloc(NULL,17); assert(p); sceLibcMspaceFree(NULL,p);
    }
    eden_heap_guard_drain();
    assert(!eden_retired_count && !eden_retired_bytes);
    puts("PASS: guards preserve allocation/reallocation/alignment/zeroing; overflow rejected; damaged free/realloc/usable abort");
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d); (p/'test.c').write_text(code)
    subprocess.run(['clang-18','-std=c11','-O1','-g','-fsanitize=address,undefined','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,timeout=15)