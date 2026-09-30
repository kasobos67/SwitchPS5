#!/usr/bin/env python3
"""Check the production host-import protection path, including partial ranges."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#define PS5_KERNEL_PROT_CPU_READ 1
#define PS5_KERNEL_PROT_CPU_WRITE 2
#define PS5_KERNEL_PROT_GPU_READ 16
#define PS5_KERNEL_PROT_GPU_WRITE 32
static unsigned queries, changes, mode;
static int sceKernelQueryMemoryProtection(void *p,void **start,void **end,uint32_t *prot) {
    ++queries;
    uintptr_t at=(uintptr_t)p;
    *start=(void *)(at & ~(uintptr_t)0xfff);
    *end=(void *)(((uintptr_t)*start)+0x1000);
    *prot=(mode==1 && queries==2) ? 3 : 0x33;
    if(mode==2) *end=p;
    return mode==3 ? -1 : 0;
}
static int sceKernelMprotect(const void *p,size_t n,int prot) {
    assert(p && n && prot==0x33); ++changes; return mode==2 || mode==3 ? -1 : 0;
}
'''+(root/'tools/radv-host-memory.inc').read_text()+r'''
int main(void) {
    assert(radv_ps5_memory_grant_gpu((void *)0x1800,0x2000));
    assert(queries==3 && changes==0);
    mode=1; queries=changes=0;
    assert(radv_ps5_memory_grant_gpu((void *)0x1800,0x2000));
    assert(queries==2 && changes==1);
    for(mode=2;mode<=3;++mode) {
        queries=changes=0;
        assert(!radv_ps5_memory_grant_gpu((void *)0x1800,0x2000));
        assert(queries==1 && changes==1);
    }
    queries=changes=0;
    assert(!radv_ps5_memory_grant_gpu(NULL,1));
    assert(!radv_ps5_memory_grant_gpu((void *)0x1000,0));
    assert(!radv_ps5_memory_grant_gpu((void *)(UINTPTR_MAX-3),8));
    assert(!queries && !changes);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp)
    (path/'check.c').write_text(source)
    subprocess.run(['clang-18','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    str(path/'check.c'),'-o',str(path/'check')],check=True)
    subprocess.run([str(path/'check')],check=True)
print('Host import: existing GPU range/subviews, partial protection, query failure, malformed range, overflow PASS')
