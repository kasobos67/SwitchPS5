// SPDX-License-Identifier: GPL-3.0-or-later
// Diagnostic-only native queue test, called with a drained GL context.
#include <sys/mman.h>
#include <cstdint>
extern "C" {
int sceAgcSetSubmitMode(int);
int sceAgcSuspendPoint();
int sceAgcDriverSubmitDcb(void*);
uint32_t* sceAgcCbReleaseMem(void*,uint8_t,int16_t,uint64_t,int8_t,void*,uint32_t,
                           uint64_t,uint16_t,uint16_t,int8_t,int32_t);
int64_t sceKernelGetDirectMemorySize();
int sceKernelAllocateDirectMemory(int64_t,int64_t,size_t,size_t,int,int64_t*);
int sceKernelMapDirectMemory(void**,size_t,int,int,int64_t,size_t);
int sceKernelReleaseDirectMemory(int64_t,size_t);
int sceKernelUsleep(uint32_t);
}
namespace Eden {
namespace {
long long ProbeClock(clockid_t id) {
    timespec t{}; Check(clock_gettime(id,&t)==0,"native probe clock");
    return static_cast<long long>(t.tv_sec)*1000000000LL+t.tv_nsec;
}
void ProbeNativeQueue(unsigned memory_type) {
    struct Command {
        uint32_t *bottom,*top,*up,*down;
        uintptr_t callback; void* user; uint32_t reserved,padding;
    };
    struct Submit { void* words; uint32_t count; uint8_t flag,padding[3]; };
    static_assert(sizeof(Command)==56 && sizeof(Submit)==16);
    constexpr size_t bytes=0x4000;
    int64_t physical=-1;
    Check(sceKernelAllocateDirectMemory(0,sceKernelGetDirectMemorySize(),bytes,bytes,memory_type,&physical)==0,
          "native probe allocate");
    void* memory=nullptr;
    if (sceKernelMapDirectMemory(&memory,bytes,0x33,0,physical,bytes)!=0 || !memory) {
        sceKernelReleaseDirectMemory(physical,bytes);
        Check(false,"native probe map");
    }
    SCOPE_EXIT {
        if (munmap(memory,bytes)!=0 || sceKernelReleaseDirectMemory(physical,bytes)!=0)
            std::_Exit(1);
    };
    const auto flush=[](const void* p,size_t n) {
        for (size_t i=0;i<n;i+=64) __builtin_ia32_clflush(static_cast<const char*>(p)+i);
        __builtin_ia32_mfence();
    };
    auto* words=static_cast<uint32_t*>(memory);
    auto* head=words+1024; auto* tail=words+1040; auto* marker=words+1056;
    Check(sceAgcSuspendPoint()==0,"native probe initial period");
    Check(sceAgcSetSubmitMode(0)==0,"native probe submit mode");
    // variant: baseline, color release, L2 destination, timestamp last.
    for (const unsigned data : {0u,1u,2u,3u}) {
        for (unsigned sleep=0;sleep<2;++sleep) for (unsigned rep=0;rep<4;++rep) {
            std::memset(memory,0,bytes);
            Command cb{words,words+512,words,words+512,0,nullptr,0,0};
            Check(sceAgcCbReleaseMem(&cb,40,0x30c,0,0,head,3,0,0,0,0,0),"native head timestamp");
            if (data==1)
                Check(sceAgcCbReleaseMem(&cb,45,12,1,0,nullptr,0,0,0,1,0,0),"native color release");
            if (data!=3)
                Check(sceAgcCbReleaseMem(&cb,40,0x30c,0,0,tail,3,0,0,0,0,0),"native tail timestamp");
            Check(sceAgcCbReleaseMem(&cb,40,0x30c,data==2?1:0,0,marker,1,101,0,0,3,0),"native value release");
            if (data==3)
                Check(sceAgcCbReleaseMem(&cb,40,0x30c,0,0,tail,3,0,0,0,3,0),"native final timestamp");
            Submit submit{words,static_cast<uint32_t>(cb.up-words),0,{0,0,0}};
            flush(memory,bytes);
            const auto start=ProbeClock(CLOCK_MONOTONIC), cpu=ProbeClock(CLOCK_THREAD_CPUTIME_ID);
            if (sceAgcDriverSubmitDcb(&submit)!=0) std::_Exit(1); // Uncertain GPU ownership: no cleanup.
            const auto issued=ProbeClock(CLOCK_MONOTONIC);
            long long first=0,end=0; unsigned polls=0;
            for (;;) {
                flush(head,192);
                const auto now=ProbeClock(CLOCK_MONOTONIC);
                if (!first && *reinterpret_cast<volatile uint64_t*>(head)) first=now-start;
                if (*reinterpret_cast<volatile uint64_t*>(marker)==101 &&
                    *reinterpret_cast<volatile uint64_t*>(tail)!=0) { end=now; break; }
                if (now-start>2000000000LL) std::_Exit(1); // Never free an unretired command buffer.
                ++polls;
                if (sleep) sceKernelUsleep(1000); else __builtin_ia32_pause();
            }
            const auto used=ProbeClock(CLOCK_THREAD_CPUTIME_ID)-cpu;
            const auto begin_ticks=*reinterpret_cast<volatile uint64_t*>(head);
            const auto end_ticks=*reinterpret_cast<volatile uint64_t*>(tail);
            Check(first>0 && end_ticks>=begin_ticks,"native probe GPU timestamps");
            std::printf("EDEN_NATIVE_QUEUE memory_type=%u data=%u sleep=%u rep=%u submit_ns=%lld first_ns=%lld wall_ns=%lld cpu_ns=%lld gpu_ticks=%llu polls=%u marker=101\n",
                memory_type,data,sleep,rep,issued-start,first,end-start,used,
                static_cast<unsigned long long>(end_ticks-begin_ticks),polls);
            Check(sceAgcSuspendPoint()==0,"native probe final period");
        }
    }
    std::printf("EDEN_NATIVE_QUEUE_PASS cases=32 memory_type=%u matrix_submit_mode=0\n",memory_type);
}
}
}
