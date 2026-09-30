// Run through tools/check-gpu-recovery.py against the same generated header as the app.
#include "common/slot_vector.h"
#include "gpu_failure.h"
#include <cassert>
#include <stdexcept>
#include <thread>
struct Resource {
    static inline int live{};
    explicit Resource(bool fail) { if (fail) throw std::runtime_error("unsupported format"); ++live; }
    Resource(Resource&&) noexcept { ++live; }
    Resource& operator=(Resource&&) noexcept = default;
    ~Resource() { --live; }
};
int main() {
    {
        Common::SlotVector<Resource> slots;
        const auto first = slots.insert(false);
        const auto reusable = slots.insert(false);
        slots.erase(reusable);
        for (int i=0;i<3;++i) {
            bool caught=false;
            try { (void)slots.insert(true); }
            catch (const std::runtime_error&) { caught=true; }
            assert(caught && slots.size()==1 && Resource::live==1);
        }
        const auto replacement=slots.insert(false);
        assert(replacement==reusable && slots.size()==2);
        slots.erase(first);
    }
    assert(Resource::live==0);
    std::thread worker([] {
        try { throw std::runtime_error("VK_ERROR_FORMAT_NOT_SUPPORTED"); }
        catch (...) { Eden::RecordGpuFailure(std::current_exception()); }
    });
    worker.join();
    auto error=Eden::TakeGpuFailure();
    assert(error && !Eden::TakeGpuFailure());
    try { std::rethrow_exception(error); }
    catch (const std::runtime_error& e) { assert(std::string_view(e.what())=="VK_ERROR_FORMAT_NOT_SUPPORTED"); }
}
