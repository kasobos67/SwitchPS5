// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <new>
#include "common/fiber.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/vfs/vfs_vector.h"

// Count C++ object ownership in this isolated, in-memory builder test.
static std::atomic<long> live_allocations{};
void* operator new(std::size_t size) {
    if (auto* p = std::malloc(size ? size : 1)) { ++live_allocations; return p; }
    throw std::bad_alloc{};
}
void operator delete(void* p) noexcept { if (p) { --live_allocations; std::free(p); } }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }

int main() {
    using namespace FileSys;
    for (unsigned cycle = 0; cycle < 4; ++cycle) {
        const auto before = live_allocations.load();
        std::weak_ptr<VfsFile> source_lifetime;
        {
            const std::vector<u8> payload{0, 1, 42, 127, 255};
            auto source = std::make_shared<VectorVfsFile>(payload, "data.bin");
            source_lifetime = source;
            auto nested = std::make_shared<VectorVfsDirectory>(
                std::vector<VirtualFile>{source}, std::vector<VirtualDir>{}, "nested");
            auto root = std::make_shared<VectorVfsDirectory>(
                std::vector<VirtualFile>{std::make_shared<VectorVfsFile>(std::vector<u8>{}, "empty")},
                std::vector<VirtualDir>{nested, std::make_shared<VectorVfsDirectory>(
                    std::vector<VirtualFile>{}, std::vector<VirtualDir>{}, "sibling")});
            auto image = CreateRomFS(root);
            if (!image) return 2;
            auto restored = ExtractRomFS(image);
            if (!restored) return 2;
            auto data = restored->GetFileRelative("nested/data.bin");
            auto empty = restored->GetFile("empty");
            if (!data || data->ReadAllBytes() != payload || !empty || empty->GetSize() != 0 ||
                !restored->GetSubdirectory("sibling")) return 2;
        }
        if (!source_lifetime.expired()) {
            std::fputs("RomFS builder retained a source file after destruction\n", stderr);
            return 1;
        }
        source_lifetime.reset();
        if (cycle && live_allocations != before) {
            std::fprintf(stderr, "RomFS builder retained %ld C++ allocations\n",
                         live_allocations.load() - before);
            return 1;
        }
    }
    std::puts("RomFS nested/empty/sibling round-trip, source lifetime and allocation balance PASS");
    for (unsigned cycle = 0; cycle < 4; ++cycle) {
        const auto before = live_allocations.load();
        {
            auto host = Common::Fiber::ThreadToFiber();
            std::shared_ptr<Common::Fiber> worker;
            unsigned calls = 0;
            worker = std::make_shared<Common::Fiber>([&] {
                for (;;) {
                    ++calls;
                    Common::Fiber::YieldTo(worker, *host);
                }
            });
            Common::Fiber::YieldTo(host, *worker);
            Common::Fiber::YieldTo(host, *worker);
            if (calls != 2) return 2;
            worker.reset(); // Deliberately abandon a suspended stack.
            host->Exit();
        }
        if (cycle && live_allocations != before) {
            std::fputs("Abandoned fiber retained C++ allocations\n", stderr);
            return 1;
        }
    }
    std::puts("Fiber round trips and abandoned-stack allocation balance PASS");
}
