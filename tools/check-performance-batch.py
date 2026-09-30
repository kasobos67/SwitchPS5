#!/usr/bin/env python3
"""Exercise stdout buffering and the real native return marker; check cache ordering."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
main = (root / 'headless/main.cpp').read_text()
assert main.index('system.GPU().Start();') < main.index('LoadDiskResources(') < main.index('system.Run();')
assert 'Settings::values.use_disk_shader_cache = game;' in main
assert 'Settings::values.use_asynchronous_shaders = false;' in main
assert 'strict_context_required = true;' in (root / 'headless/graphics.cpp').read_text()
assert 'Level::Debug' not in main
assert 'std::setvbuf(stderr, nullptr, _IONBF, 0);' in main
buffering = main[main.index('        static char stdout_buffer'):main.index('        std::set_new_handler')]
with tempfile.TemporaryDirectory(prefix='eden-batch-') as work:
    work = Path(work)
    source = work / 'buffer.cpp'
    source.write_text('''#include <cstdio>
#include <cassert>
#include <unistd.h>
int main(int argc,char **argv) {
    assert(argc==2 && std::freopen(argv[1],"w",stdout));
''' + buffering + '''
    for(int i=0;i<10000;++i) std::fputc('x',stdout);
    assert(lseek(fileno(stdout),0,SEEK_CUR)<10000);
    assert(std::fflush(stdout)==0);
    assert(lseek(fileno(stdout),0,SEEK_CUR)==10000);
}
''')
    exe = work / 'buffer'
    subprocess.run(['g++','-std=c++20',str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(work/'output')],check=True)
    assert (work/'output').read_bytes() == b'x'*10000
    source = work / 'return.c'
    source.write_text('''#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static jmp_buf done;
static char marker[96];
int sceKernelDebugOutText(int channel,const char *text) {
    assert(channel==0); strcpy(marker,text); return 0;
}
int sceKernelUsleep(uint32_t delay) {
    assert(delay==100000 && marker[0]); longjmp(done,1);
}
void catchReturnFromMain(int);
int main(void) {
    if(!setjmp(done)) catchReturnFromMain(0);
    assert(!strcmp(marker,"EDEN_PPSA99121_MAIN_RETURN status=0\\n"));
    marker[0]=0;
    if(!setjmp(done)) catchReturnFromMain(7);
    assert(!strcmp(marker,"EDEN_PPSA99121_MAIN_RETURN status=7\\n"));
}
''')
    exe = work / 'return'
    subprocess.run(['cc',str(source),str(root/'src/lifecycle.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=5)
print('Buffered output/explicit flush, success/failure completion markers and serial shader-cache ordering PASS')
