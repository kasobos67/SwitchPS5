#!/usr/bin/env python3
"""Count real W^X transitions while executing and invalidating direct branch chains."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
cache = Path((root / '.local/headless-cache').read_text().strip())
assert (root / 'headless/jit-compile-batch.inc').read_text() in (cache / 'build/headless/a64_interface.cpp').read_text(), 'Host batch derivative is stale'
source = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
static unsigned calls;
int memfd_create(const char *name, unsigned flags) {
    if (getenv("EDEN_TEST_NO_ALIAS")) { errno=EPERM; return -1; }
    int (*original)(const char*,unsigned)=dlsym(RTLD_NEXT,"memfd_create");
    assert(original);
    return original(name,flags);
}
int mprotect(void *address, size_t bytes, int mode) {
    static int (*original)(void*,size_t,int);
    if (!original) original=dlsym(RTLD_NEXT,"mprotect");
    assert(original && !((mode&PROT_WRITE) && (mode&PROT_EXEC)));
    ++calls;
    return original(address,bytes,mode);
}
__attribute__((destructor)) static void report(void) {
    fprintf(stderr,"JIT_BATCH_PROTECTION calls=%u\n",calls);
}
'''
with tempfile.TemporaryDirectory(prefix='jit-batch-') as temporary:
    library = str(Path(temporary) / 'protection.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Werror', '-shared', '-fPIC',
                    '-x', 'c', '-', '-ldl', '-o', library], input=source, text=True, check=True)
    for fallback in (False, True):
        env={**os.environ, 'LD_PRELOAD': library}
        if fallback: env['EDEN_TEST_NO_ALIAS']='1'
        result = subprocess.run([str(cache / 'build/bin/eden-memory-check'), '--compile-chains'],
                                env=env, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=90)
        print(result.stdout, end='')
        print(result.stderr, end='')
        result.check_returncode()
        calls = int(re.search(r'JIT_BATCH_PROTECTION calls=(\d+)', result.stderr)[1])
        assert 0 < calls < (262144 * 3 if fallback else 10)
        assert f'active={int(not fallback)}' in result.stderr
        assert 'EDEN_JIT_PRESSURE' in result.stderr, 'Cache evacuation was not exercised'
print('PASS: alias and denied-alias fallback, 786432 blocks each, cache evacuation, invalidation/relink, single-step, unmapped targets and no RWX')
