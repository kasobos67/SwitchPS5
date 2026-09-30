"""Exercise the actual download batch body with deferred GPU completion."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    code = r'''
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
constexpr size_t operator""_MiB(unsigned long long n) { return n * 1024 * 1024; }
using ImageId = size_t;
struct Storage { bool ready = false; int value = 0; };
struct Map { std::shared_ptr<Storage> mapped_span; };
namespace Vulkan {
class TextureCacheRuntime {
public:
    int waits = 0;
    bool fail = false;
    std::vector<std::shared_ptr<Storage>> live;
    Map DownloadStagingBuffer(size_t) {
        live.push_back(std::make_shared<Storage>());
        return {live.back()};
    }
    void Finish() {
        ++waits;
        if (fail) throw std::runtime_error("device lost");
        for (auto& item : live) item->ready = true;
        live.clear();
    }
};
}
struct Image {
    size_t unswizzled_size_bytes;
    int info = 0, gpu_addr = 0;
    void DownloadMemory(Map& map, int) { map.mapped_span->value = gpu_addr; }
};
int FullDownloadCopies(int info) { return info; }
int FixSmallVectorADL(int value) { return value; }
void SwizzleImage(std::vector<int>& writes, int address, int, int,
                  std::shared_ptr<Storage>& map, int) {
    assert(map->ready && map->value == address);
    writes.push_back(address);
}
template<class Runtime> struct Case {
    Runtime runtime;
    std::vector<Image> slot_images;
    std::vector<ImageId> images;
    std::vector<int> writes;
    std::vector<int>* gpu_memory = &writes;
    int swizzle_data_buffer = 0;
    bool fallback = false;
    void Run() {
BODY
        fallback = true;
    }
    void Add(size_t bytes, int address) {
        images.push_back(slot_images.size());
        slot_images.push_back({bytes, 0, address});
    }
};
struct OtherRuntime : Vulkan::TextureCacheRuntime {};
int main() {
    Case<Vulkan::TextureCacheRuntime> empty;
    empty.Run(); assert(empty.runtime.waits == 0 && !empty.fallback);
    Case<Vulkan::TextureCacheRuntime> many;
    for(int i=0;i<17;++i) many.Add(1_MiB, i%3);
    many.Run(); assert(many.runtime.waits == 2 && many.writes.size() == 17);
    for(int i=0;i<17;++i) assert(many.writes[i] == i%3); // Overlapping write order.
    Case<Vulkan::TextureCacheRuntime> sizes;
    for(size_t n : {16_MiB, 16_MiB, 1_MiB, 64_MiB, 1_MiB}) sizes.Add(n, 7);
    sizes.Run(); assert(sizes.runtime.waits == 4 && sizes.writes.size() == 5);
    Case<Vulkan::TextureCacheRuntime> failed;
    failed.Add(1_MiB, 4); failed.runtime.fail = true;
    try { failed.Run(); assert(false); } catch(const std::runtime_error&) {}
    assert(failed.writes.empty());
    Case<OtherRuntime> other;
    other.Run(); assert(other.fallback && other.runtime.waits == 0);
}
'''.replace('BODY', (root / 'headless/vulkan_download_batch.inc').read_text())
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-fsanitize=address,undefined',
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Vulkan download batch: bounds, ordering, completion, errors, other-backend fallback PASS')
