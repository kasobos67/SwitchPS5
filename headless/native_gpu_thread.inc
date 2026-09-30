// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-only ThreadManager lifecycle; RunWorker retains Eden's worker body.
#include <memory>
#include <cstdio>
#include <system_error>
#include "diagnostics.h"
#include "gpu_failure.h"

namespace VideoCommon::GPUThread {
void ThreadManager::StartThread(VideoCore::RendererBase& renderer,
        Core::Frontend::GraphicsContext& context, Tegra::Control::Scheduler& scheduler) {
    Stop();
    (void)Eden::TakeGpuFailure();
    stop_source = std::stop_source{};
    rasterizer = renderer.ReadRasterizer();
    const auto check = [](int result) {
        if (result) throw std::system_error(result, std::generic_category(), "PS5 GPU worker");
    };
    pthread_attr_t attributes;
    check(pthread_attr_init(&attributes));
    struct DestroyAttributes {
        pthread_attr_t* attributes;
        ~DestroyAttributes() {
            if (pthread_attr_destroy(attributes)) std::terminate();
        }
    } cleanup{&attributes};
    check(pthread_attr_setstacksize(&attributes, 1024 * 1024));
    struct StartData {
        ThreadManager* owner;
        VideoCore::RendererBase& renderer;
        Core::Frontend::GraphicsContext& context;
        Tegra::Control::Scheduler& scheduler;
    };
    auto data = std::make_unique<StartData>(StartData{this, renderer, context, scheduler});
    check(pthread_create(&thread, &attributes, [](void* opaque) noexcept -> void* {
        const std::unique_ptr<StartData> start{static_cast<StartData*>(opaque)};
        try {
            start->owner->RunWorker(start->renderer, start->context, start->scheduler);
        } catch (const std::exception& error) {
            Eden::Report("GPU worker", error.what());
            Eden::RecordGpuFailure(std::current_exception());
            start->owner->stop_source.request_stop();
        } catch (...) {
            Eden::Report("GPU worker", "Unknown exception");
            Eden::RecordGpuFailure(std::current_exception());
            start->owner->stop_source.request_stop();
        }
        return nullptr;
    }, data.get()));
    data.release();
    thread_joinable = true;
    std::puts("EDEN_GPU_WORKER_STACK bytes=1048576");
}

void ThreadManager::Stop() {
    stop_source.request_stop();
    if (thread_joinable) {
        if (pthread_join(thread, nullptr)) std::terminate();
        thread_joinable = false;
    }
}
} // namespace VideoCommon::GPUThread
