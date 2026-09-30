"""Compare the actual generated collector with upstream; no console required.

Usage: python3 tools/check-vulkan-gc.py SOURCE GENERATED_HEADLESS
"""
from pathlib import Path
import subprocess
import sys
import tempfile

source, generated = map(Path, sys.argv[1:])
relative = 'video_core/texture_cache/texture_cache.h'

def body(path):
    text = path.read_text()
    start = text.index('void TextureCache<P>::RunGarbageCollector() {')
    end = text.index('\ntemplate <class P>', start)
    # Exclude opt-in diagnostic timers, not the algorithm under test.
    return text[text.index('    bool high_priority_mode', start):end].rstrip()[:-1]

def budget(path):
    text = path.read_text()
    start = text.index('        const s64 device_local_memory =')
    return text[start:text.index('    } else {', start)]

code = r'''
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include <boost/container/small_vector.hpp>
#include "common/lru_cache.h"
#include "common/scope_exit.h"
constexpr size_t operator""_MiB(unsigned long long n) { return n * 1024 * 1024; }
constexpr size_t operator""_GiB(unsigned long long n) { return n * 1024 * 1024 * 1024; }
using ImageId = size_t;
namespace Common { u64 AlignUp(u64 n, u64 a) { return (n+a-1)/a*a; } }
namespace ImageFlagBits {
constexpr unsigned IsDecoding=1, BadOverlap=2, CostlyLoad=4, Tracked=8,
                   AcceleratedUpload=16, Converted=32;
}
bool True(unsigned n) { return n != 0; }
bool False(unsigned n) { return n == 0; }
struct Info { int format=0, num_samples=1; };
bool IsPixelFormatASTC(int f) { return f == 1; }
u64 TranscodedAstcSize(u64 n, int) { return n * 4; }
struct Life { bool alive=true, tracked=true, written=false; int id=0; };
struct Storage {
    bool pinned=false, ready=false;
    size_t bytes=0;
    std::shared_ptr<Life> source;
};
struct Map { std::shared_ptr<Storage> mapped_span; };
namespace Vulkan {
class TextureCacheRuntime {
public:
    int waits=0, copies=0;
    bool fail=false;
    size_t peak_pinned=0;
    u64 device_memory=4_GiB;
    u64 GetDeviceLocalMemory() { return device_memory; }
    std::vector<std::shared_ptr<Storage>> buffers;
    void Complete() {
        for (auto& b : buffers) {
            if (b->source && !b->ready) {
                assert(b->source->alive && b->source->tracked);
                b->ready = true;
            }
        }
    }
    Map DownloadStagingBuffer(size_t n, bool pin=false) {
        // Model earlier submits completing while the next image is recorded.
        Complete();
        auto it = std::ranges::find_if(buffers, [](const auto& b) {
            return b->ready && !b->pinned;
        });
        auto b = it == buffers.end() ? std::make_shared<Storage>() : *it;
        if (it == buffers.end()) buffers.push_back(b);
        *b = Storage{pin, false, n, {}};
        size_t pinned=0;
        for (auto& item : buffers) if (item->pinned) pinned+=item->bytes;
        peak_pinned = std::max(peak_pinned, pinned);
        return {b};
    }
    void FreeDeferredStagingBuffer(Map& m) {
        assert(m.mapped_span->pinned);
        m.mapped_span->pinned=false;
    }
    void Finish() {
        ++waits;
        if (fail) throw std::runtime_error("device lost");
        Complete();
    }
    bool Unpinned() {
        return std::ranges::none_of(buffers, [](const auto& b) {return b->pinned;});
    }
};
}
struct OtherRuntime : Vulkan::TextureCacheRuntime {};
struct Image {
    u64 guest_size_bytes=1_MiB, unswizzled_size_bytes=1_MiB, scale_tick=0;
    Info info;
    unsigned flags=ImageFlagBits::Tracked;
    bool scaled=false, dirty=true;
    size_t lru_index=0;
    int gpu_addr=0;
    std::vector<ImageId> aliased_images, overlapping_images;
    std::shared_ptr<Life> life=std::make_shared<Life>();
    bool HasScaled() const { return scaled; }
    void DownloadMemory(Map& map, int) {
        assert(life->alive && life->tracked);
        map.mapped_span->source=life;
    }
};
using ImageBase=Image;
int FullDownloadCopies(Info) { return 0; }
int FixSmallVectorADL(int n) { return n; }
void SwizzleImage(std::vector<int>& events, int address, Info, int,
                  const std::shared_ptr<Storage>& map, int) {
    assert(map->ready && map->source->alive && map->source->tracked);
    assert(map->source->id == address);
    map->source->written=true;
    events.push_back(address);
}
struct Traits { using ObjectType=ImageId; using TickType=u64; };
template<class Runtime> struct Case {
    Runtime runtime;
    Common::LeastRecentlyUsedCache<Traits> lru_cache;
    std::vector<Image> slot_images;
    std::vector<int> events;
    std::vector<int>* gpu_memory=&events;
    int swizzle_data_buffer=0;
    u64 frame_tick=100, total_used_memory=0, expected_memory=128_MiB,
        critical_memory=256_MiB, minimum_memory=0;
    static constexpr s64 TARGET_THRESHOLD=4_GiB, DEFAULT_EXPECTED_MEMORY=1_GiB+125_MiB,
                         DEFAULT_CRITICAL_MEMORY=1_GiB+625_MiB;
    void OriginalBudget() {
ORIGINAL_BUDGET
    }
    void BatchedBudget() {
BATCHED_BUDGET
    }
    bool IsDownloadable(const Image& i) { return i.dirty; }
    u64 GetScaledImageSizeBytes(const Image& i) { return i.unswizzled_size_bytes*3; }
    void UntrackImage(Image& i, ImageId) {
        assert(!IsDownloadable(i) || True(i.flags & ImageFlagBits::BadOverlap) || i.life->written);
        i.life->tracked=false;
    }
    void UnregisterImage(ImageId id) { lru_cache.Free(slot_images[id].lru_index); }
    void DeleteImage(ImageId id, bool) {
        auto& image=slot_images[id];
DELETE_ACCOUNTING
        for (auto other : image.overlapping_images) {
            auto& i=slot_images[other];
            std::erase(i.overlapping_images, id);
            if (i.overlapping_images.empty()) i.flags &= ~ImageFlagBits::BadOverlap;
        }
        for (auto other : image.aliased_images) std::erase(slot_images[other].aliased_images, id);
        image.life->alive=false;
        events.push_back(-image.gpu_addr);
    }
    void Add(Image i, u64 tick) {
        const auto id=slot_images.size();
        i.gpu_addr=int(id)+1;
        i.life=std::make_shared<Life>();
        i.life->id=i.gpu_addr;
        i.lru_index=lru_cache.Insert(id,tick);
        slot_images.push_back(std::move(i));
    }
    void Original() {
ORIGINAL
    }
    void Batched() {
BATCHED
    }
};
void Compare(unsigned seed) {
    Case<Vulkan::TextureCacheRuntime> before, after;
    std::mt19937 rng(seed);
    const u64 usage=300_MiB + (rng()%400)*1_MiB;
    before.total_used_memory=after.total_used_memory=usage;
    before.expected_memory=after.expected_memory=usage - (rng()%60)*1_MiB;
    before.critical_memory=after.critical_memory=usage + 10_MiB - (rng()%40)*1_MiB;
    for (int n=0;n<60;++n) {
        Image i;
        i.dirty=rng()%3 != 0;
        i.scaled=rng()%8 == 0;
        i.info.format=rng()%2;
        i.info.num_samples=rng()%5 ? 1 : 4;
        i.flags |= rng()%8 == 0 ? ImageFlagBits::CostlyLoad : 0;
        i.flags |= rng()%9 == 0 ? ImageFlagBits::IsDecoding : 0;
        i.flags |= rng()%7 == 0 ? ImageFlagBits::Converted : 0;
        i.unswizzled_size_bytes=i.guest_size_bytes=(1+rng()%4)*1_MiB;
        before.Add(i,n); after.Add(i,n);
    }
    // Deleting one overlapping entry can enable the next dirty download.
    for (auto* c : {&before,&after}) {
        c->slot_images[1].overlapping_images={2};
        c->slot_images[2].overlapping_images={1};
        c->slot_images[1].flags|=ImageFlagBits::BadOverlap;
        c->slot_images[2].flags|=ImageFlagBits::BadOverlap;
        c->slot_images[4].aliased_images={5};
        c->slot_images[5].aliased_images={4};
    }
    before.Original(); after.Batched();
    assert(before.events == after.events);
    assert(before.total_used_memory == after.total_used_memory);
    assert(after.runtime.waits <= before.runtime.waits);
    assert(after.runtime.peak_pinned <= 32_MiB && after.runtime.Unpinned());
    for (size_t i=0;i<60;++i) {
        assert(before.slot_images[i].life->alive == after.slot_images[i].life->alive);
        assert(before.slot_images[i].flags == after.slot_images[i].flags);
    }
}
int main() {
    for (u64 memory : {1_GiB,2_GiB,3_GiB,4_GiB,6_GiB,8_GiB}) {
        Case<Vulkan::TextureCacheRuntime> old, now;
        Case<OtherRuntime> other;
        old.runtime.device_memory=now.runtime.device_memory=other.runtime.device_memory=memory;
        old.OriginalBudget(); now.BatchedBudget(); other.BatchedBudget();
        assert(now.critical_memory==old.critical_memory && now.minimum_memory==old.minimum_memory);
        assert(now.expected_memory>=old.expected_memory && now.expected_memory<=now.critical_memory);
        assert(other.expected_memory==old.expected_memory);
        if(memory>=3_GiB) assert(memory-now.expected_memory>=1_GiB);
        if(memory==4_GiB) {
            assert(old.expected_memory==1717986919ULL && now.expected_memory==2576980378ULL);
            assert(now.critical_memory==3435973837ULL);
            assert(old.expected_memory<2024734720ULL && now.expected_memory>2024734720ULL);
        }
    }
    for (unsigned seed=0;seed<1000;++seed) Compare(seed);
    Case<Vulkan::TextureCacheRuntime> before, after;
    before.total_used_memory=after.total_used_memory=240_MiB;
    for(int i=0;i<20;++i) { before.Add({},0); after.Add({},0); }
    before.Original(); after.Batched();
    assert(before.events == after.events && before.runtime.waits==20 && after.runtime.waits==5);
    for(u64 frame : {0,9,24,25,49,50}) {
        Case<Vulkan::TextureCacheRuntime> a,b;
        a.frame_tick=b.frame_tick=frame;
        a.total_used_memory=b.total_used_memory=240_MiB;
        a.Add({},0); b.Add({},0);
        a.Original(); b.Batched(); assert(a.events==b.events);
    }
    Case<Vulkan::TextureCacheRuntime> failed;
    failed.total_used_memory=240_MiB;
    failed.Add({},0); failed.Add({},0); failed.runtime.fail=true;
    try { failed.Batched(); assert(false); } catch(const std::runtime_error&) {}
    assert(failed.events.empty() && failed.runtime.Unpinned());
    Case<Vulkan::TextureCacheRuntime> large;
    large.total_used_memory=240_MiB;
    Image big; big.unswizzled_size_bytes=64_MiB;
    large.Add(big,0); large.Batched(); assert(large.runtime.peak_pinned==0 && large.runtime.waits==1);
    Case<OtherRuntime> other;
    other.total_used_memory=240_MiB;
    for(int i=0;i<20;++i) other.Add({},0);
    other.Batched(); assert(other.runtime.waits==20 && other.runtime.peak_pinned==0);
}
'''
original = (source / 'src' / relative).read_text()
start = original.index('    if (image.HasScaled()) {', original.index('void TextureCache<P>::DeleteImage('))
end = original.index('    const GPUVAddr gpu_addr', start)
code = code.replace('DELETE_ACCOUNTING', original[start:end])
code = code.replace('ORIGINAL_BUDGET', budget(source / 'src' / relative))
code = code.replace('BATCHED_BUDGET', budget(generated / 'vulkan-cache' / relative))
code = code.replace('ORIGINAL', body(source / 'src' / relative))
code = code.replace('BATCHED', body(generated / 'vulkan-cache' / relative))
boost_includes = ['-I' + str(p) for p in sorted((source / '.cache/cpm/boost').glob('*/libs/*/include'))]
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'check.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-O1', '-g', '-fsanitize=address,undefined',
                    '-I' + str(source / 'src'), *boost_includes,
                    str(path / 'check.cpp'), '-o', str(path / 'check')], check=True)
    subprocess.run([str(path / 'check')], check=True)
print('GC PASS: exact original eviction/write order across 1000 pressure/alias cases; '
      '20 dirty waits -> 5; lifetime, bounds, failure, age, budget headroom and other-backend checks')
