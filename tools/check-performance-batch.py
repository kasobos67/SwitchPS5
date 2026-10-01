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
# The stdout buffer alone: the log pipes that follow it need the app (headless/log_pipe.h).
buffering = main[main.index('        static char stdout_buffer'):main.index('        // Console storage writes take')]
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
static char marker[96], refused[96];
static int exit_requests;
int sceKernelDebugOutText(int channel,const char *text) {
    assert(channel==0);
    strcpy(strncmp(text,"EDEN_PPSA99121_EXIT_REFUSED",27) ? marker : refused,text); return 0;
}
/* The system's own exit request comes after the return marker; here it is refused, so the
   wait to be closed must follow. */
int sceSystemServiceLoadExec(const char *path,const char **args) {
    assert(!strcmp(path,"exit") && args==NULL && marker[0] && !refused[0]);
    ++exit_requests; return 0x80aa0001;
}
int sceKernelUsleep(uint32_t delay) {
    assert(delay==100000 && marker[0] && refused[0]); longjmp(done,1);
}
void catchReturnFromMain(int);
int main(void) {
    if(!setjmp(done)) catchReturnFromMain(0);
    assert(!strcmp(marker,"EDEN_PPSA99121_MAIN_RETURN status=0\\n"));
    assert(exit_requests==1 && !strcmp(refused,"EDEN_PPSA99121_EXIT_REFUSED rc=80aa0001\\n"));
    marker[0]=0; refused[0]=0;
    if(!setjmp(done)) catchReturnFromMain(7);
    assert(!strcmp(marker,"EDEN_PPSA99121_MAIN_RETURN status=7\\n"));
    assert(exit_requests==2 && refused[0]);
}
''')
    exe = work / 'return'
    subprocess.run(['cc',str(source),str(root/'src/lifecycle.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=5)
main_text = main
assert '_Exit(' not in main_text, 'a native title must not end itself with _Exit(): the kernel answers it with signal 12'
print('Buffered output/explicit flush, return markers, the system exit request with its fallback wait, '
      'and serial shader-cache ordering PASS')
