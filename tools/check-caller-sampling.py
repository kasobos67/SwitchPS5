#!/usr/bin/env python3
"""Exercise actual bounded signal caller reads, including malformed frame chains."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'headless/performance.cpp').read_text()
a=s.index('CallerChain CaptureCallerChain(')
helper=s[a:s.index('\n}',a)+2]
code=r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
using CallerChain=std::array<uintptr_t,8>;
'''+helper+r'''
int main() {
 void *allocation=nullptr;assert(!posix_memalign(&allocation,4096,4096));
 auto* words=static_cast<uintptr_t*>(allocation);
 for(unsigned i=0;i<512;++i)words[i]=0;
 uintptr_t rsp=reinterpret_cast<uintptr_t>(words)+128;
 uintptr_t first=rsp+32;
 auto* frame=reinterpret_cast<uintptr_t*>(first);
 for(unsigned i=0;i<8;++i){frame[2*i]=first+16*(i+1);frame[2*i+1]=0x400010+i;}
 auto full=CaptureCallerChain(rsp,first,rsp-256);
 for(unsigned i=0;i<8;++i)assert(full[i]==0x400010+i);
 auto empty=CallerChain{};
 assert(CaptureCallerChain(rsp,first,rsp+1)==empty);
 assert(CaptureCallerChain(rsp,first,rsp-65536)==empty);
 assert(CaptureCallerChain(8,8,0)==empty);
 assert(CaptureCallerChain(rsp,rsp-8,rsp-256)==empty);
 assert(CaptureCallerChain(rsp,first+1,rsp-256)==empty);
 assert(CaptureCallerChain(rsp,(rsp&~uintptr_t(4095))+4096,rsp-256)==empty);
 assert(CaptureCallerChain(rsp,(rsp&~uintptr_t(4095))+4088,rsp-256)==empty);
 assert(CaptureCallerChain((rsp&~uintptr_t(4095))+4090,first,rsp-256)==empty);
 frame[0]=first;auto loop=CaptureCallerChain(rsp,first,rsp-256);
 assert(loop[0]==0x400010 && loop[1]==0);
 frame[0]=first-16;auto backward=CaptureCallerChain(rsp,first,rsp-256);
 assert(backward[0]==0x400010 && backward[1]==0);
 frame[0]=(rsp&~uintptr_t(4095))+4096;
 auto escape=CaptureCallerChain(rsp,first,rsp-256);assert(escape[1]==0);
 free(allocation);std::puts("PASS actual caller walker: valid chain, cycles, alignment, stack/page limits");
}
'''
with tempfile.TemporaryDirectory() as d:
 exe=str(Path(d)/'callers')
 subprocess.run(['clang++-18','-std=c++20','-O1','-Wall','-Wextra','-Werror',
  '-fsanitize=address,undefined','-fno-sanitize-recover=all','-x','c++','-o',exe,'-'],
  input=code,text=True,check=True)
 subprocess.run([exe],check=True,timeout=15)
# Exercise the real report against a tiny symbolized ELF and synthetic records.
with tempfile.TemporaryDirectory() as d:
 run=Path(d); elf=run/'fixture.elf'; cpp=run/'fixture.cpp'
 cpp.write_text('namespace Eden::Performance { namespace { __attribute__((used,noinline)) void PcSignal() {} } }\n'
                '__attribute__((used,noinline)) void KnownUploadWait() {}\nint main(){KnownUploadWait();}\n')
 subprocess.run(['clang++-18','-g','-O0',str(cpp),'-o',str(elf)],check=True)
 symbols=subprocess.check_output(['nm','-n','-C','--defined-only',str(elf)],text=True)
 def address(name):
  return int(next(line.split()[0] for line in symbols.splitlines() if name in line),16)
 bias=0x400000
 anchor=address('::PcSignal(')+bias
 caller=address('KnownUploadWait(')+bias+1
 pc=0x80000178c
 prefix=f'EDEN_PERF_PC_ANCHOR address={anchor:x}\nEDEN_PERF_NATIVE_PC mono_ns=1 pc={pc:x} count=4\n'
 records=(f'EDEN_PERF_NATIVE_CALLERS mono_ns=1 pc={pc:x} count=3 callers={caller:x},0,0,0,0,0,0,0\n'
          f'EDEN_PERF_NATIVE_CALLERS mono_ns=1 pc={pc:x} count=1 callers=0,0,0,0,0,0,0,0\n')
 log=run/'180-heap.log'; log.write_text(prefix+records)
 command=['python3',str(root/'tools/summarize-native-pcs.py'),str(run),str(elf),'--callers']
 result=subprocess.run(command,capture_output=True,text=True,check=True)
 assert 'Caller chains present: 3/4' in result.stdout and 'KnownUploadWait()' in result.stdout
 assert 'NO VALID CHAIN' in result.stdout
 log.write_text(prefix+records.replace('count=3 callers','count=2 callers'))
 bad=subprocess.run(command,capture_output=True,text=True)
 assert bad.returncode and 'Missing/incomplete caller samples' in bad.stderr
 print('PASS actual caller report: return-address resolution, unknown chains, incomplete evidence rejection')
