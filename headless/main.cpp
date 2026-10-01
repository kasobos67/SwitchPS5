#ifdef EDEN_DEV_ROM_ID
#include "development_input.h"
#endif
#include <utility>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <algorithm>
#include <exception>
#include "assets_dir.h"
#include "gpu_failure.h"
#include "guest_fault.h"
#ifdef PS5_NATIVE
#include "elevation/elevation.hpp"
#include <sys/stat.h>
#endif
#ifdef EDEN_DEV_VULKAN
#include "sdk_audit.h"
#endif
#include <filesystem>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <new>
#include <string_view>
#include <stdexcept>
#include <fstream>
#include <nlohmann/json.hpp>
#include "devices.h"
#include "diagnostics.h"
#include "log_pipe.h"
#include "controller_applet.h"
#include "preferences.h"
#include "metadata_bridge.h"
#ifdef EDEN_PS5_OPENGL
#include "graphics.h"
#include "video_core/renderer_base.h"
#include "video_core/rasterizer_interface.h"
#endif
#ifdef PS5_NATIVE
#include "native_directory.h"
#include "cache_budget.h"
#include "performance.h"
#include "stall_watchdog.h"
#include "dev_vulkan.h"
#include "../src/fastmem.h"
#include "prosperoeden/frontend.h"
extern "C" void ps5_opengl_heap_snapshot(const char*, unsigned);
extern "C" std::int64_t sceKernelGetDirectMemorySize();
#else
#include <malloc.h>
#include "mock_devices.h"
#endif
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/cpu_manager.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/frontend/applets.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/set/settings_types.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#ifdef PS5_NATIVE
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-private-field"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#pragma clang diagnostic pop
#endif
#ifdef EDEN_DEV_PROFILE
#include "watch.h"
#include "core/arm/debug.h"
#include "core/memory.h"
extern "C" bool eden_jit_shared;  // headless/dynarmic/jit_group_support.inc
#endif
#include "video_core/gpu.h"

class HeadlessWindow final : public Core::Frontend::EmuWindow {
public:
    HeadlessWindow() { UpdateCurrentFramebufferLayout(1280, 720); }
    ~HeadlessWindow() override = default;
    bool IsShown() const override { return false; }
    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override {
        return std::make_unique<Core::Frontend::GraphicsContext>();
    }
};

#ifdef PS5_NATIVE
// First start with filesystem access: copy what the sandbox kept (settings, covers, Eden's saves
// and caches) into /data/prosperoeden. Only folders that do not exist yet are filled, and the
// sandbox copy stays, so an older ProsperoEden still finds its data.
static void MigrateSandboxData() {
    const std::filesystem::path sandbox{"/mnt/sandbox/PPSA99008_000/download0"};
    const std::pair<std::filesystem::path, std::string> moves[] = {
        {sandbox / "eden-headless-g7/user", Eden::UserDir()},
        {sandbox / "prosperoeden/covers", Eden::CoversDir()},
    };
    const auto options = std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing;
    for (const auto& [from, to] : moves) {
        std::error_code error;
        if (Eden::DirectoryExists(to) || !Eden::DirectoryExists(from.string())) continue;
        std::filesystem::copy(from, to, options, error);
        const std::string detail = from.string() + " -> " + to + (error ? " failed: " + error.message() : "");
        Eden::Report("data migration", detail.c_str());
    }
    // Settings files (not the covers folder) go to config/.
    if (!Eden::DirectoryExists(Eden::ConfigDir()) && Eden::DirectoryExists((sandbox / "prosperoeden").string())) {
        std::error_code error;
        std::filesystem::create_directories(Eden::ConfigDir(), error);
        for (std::filesystem::directory_iterator it{sandbox / "prosperoeden", error}, end; !error && it != end; it.increment(error))
            if (it->is_regular_file())
                std::filesystem::copy_file(it->path(), std::filesystem::path{Eden::ConfigDir()} / it->path().filename(),
                                           std::filesystem::copy_options::skip_existing, error);
        Eden::Report("data migration", error ? ("settings failed: " + error.message()).c_str() : "settings copied");
    }
}
#endif

int main(int argc, char** argv) {
    try {
#if defined(EDEN_DEV_PROFILE) && defined(PS5_NATIVE)
        Eden::Stall::Start();
#endif
        std::FILE* report = stdout;
        SCOPE_EXIT { if (report != stdout) std::fclose(report); };
        std::setvbuf(report, nullptr, _IONBF, 0);
#ifdef PS5_NATIVE
        // Filesystem access beyond the sandbox, first: every path below depends on it
        // (assets_dir.h). Requested once, still single-threaded. Without it the app keeps its
        // sandbox paths.
        Eden::FilesystemAccessStatus() = static_cast<int>(elevation::request(elevation::Capability::filesystem));
        if (Eden::FilesystemAccess()) MigrateSandboxData();
        for (const auto& folder : {Eden::UserDir(), Eden::ConfigDir(), Eden::CoversDir(), Eden::LogsDir()}) {
            std::error_code folder_error;
            std::filesystem::create_directories(folder, folder_error);
        }
#ifdef EDEN_DEV_PROFILE
        // UI inspection captures are disposable; keep saves, settings and shader caches.
        for (const char* name : {"ui-preview.bmp", "ui-main.bmp", "ui-nav.bmp",
                                 "ui-library.bmp", "ui-about.bmp"})
            std::filesystem::remove(std::filesystem::path{Eden::ConfigDir()} / name);
        for (const char* name : {"eden_log.txt", "eden_log.txt.old.txt"})
            std::filesystem::remove(std::filesystem::path{Eden::UserDir()} / "log" / name);
#endif
        // Keep the previous session's logs: a freeze is diagnosed after the app is reopened.
        for (const char* name : {"stderr.log", "heap.log"}) {
            const std::string log = Eden::LogFile(name);
            std::rename(log.c_str(), (log.substr(0, log.size() - 4) + ".prev.log").c_str());
        }
        if (!std::freopen(Eden::LogFile("stderr.log").c_str(), "w", stderr) ||
            !std::freopen(Eden::LogFile("heap.log").c_str(), "w", stdout)) return 2;
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        // Batch SDK success traces; phase receipts still flush explicitly.
        static char stdout_buffer[64 * 1024];
        if (std::setvbuf(stdout, stdout_buffer, _IOFBF, sizeof(stdout_buffer)) != 0) return 2;
        // Console storage writes take ~25 ms each; background threads copy both streams to disk.
        static Eden::LogPipe stderr_pipe, stdout_pipe;
        if (!stderr_pipe.Attach(stderr) || !stdout_pipe.Attach(stdout))
            Eden::Report("logs", "Asynchronous log writing unavailable; writing directly");
        std::set_new_handler([] {
            ps5_opengl_heap_snapshot("allocation_failure", 0);
            std::fflush(stdout);
            Eden::Report("allocation failure", "operator new: heap exhausted");
            throw std::bad_alloc{};
        });
        // Name the exception in klog before aborting; stderr files vanish with the sandbox.
        std::set_terminate([] {
            const char* detail = "no active exception";
            if (const auto current = std::current_exception()) {
                try {
                    std::rethrow_exception(current);
                } catch (const std::exception& error) {
                    detail = error.what();
                } catch (...) {
                    detail = "non-standard exception";
                }
            }
            Eden::Report("terminate", detail);
            std::abort();
        });
        {
            const std::string access = "status=" + std::to_string(Eden::FilesystemAccessStatus()) +
                " app=" + Eden::AppDir() + " data=" + Eden::UserDir() + " game_files=" + Eden::AssetsDir();
            Eden::Report("filesystem access", access.c_str());
            if (Eden::FilesystemAccess() && Eden::AssetsDir() == Eden::kDefaultAssetsDir)
                for (const char* folder : {"/keys", "/firmware", "/roms", "/updates"})
                    (void)mkdir((std::string{Eden::kDefaultAssetsDir} + folder).c_str(), 0777);
            // RADV keeps its shader cache in the app folder (radv_ps5_platform.c's default is /app0).
            if (Eden::FilesystemAccess())
                setenv("MESA_SHADER_CACHE_DIR", Eden::AppFile("radv-shader-cache").c_str(), 1);
        }
        report = std::fopen(Eden::LogFile("result.tsv").c_str(), "w");
        if (!report) { report = stdout; return 2; }
        std::puts("[headless-startup] directories_ready");
#ifdef EDEN_DEV_VULKAN
        if (std::filesystem::exists(Eden::AppFile("sdk-audit.txt"))) {
            Eden::AuditSdk();
            std::puts("EDEN_SDK_AUDIT_COMPLETE");
            std::fflush(stdout);
            // Keep the sandbox mounted for the existing bounded FTP collector.
            std::this_thread::sleep_for(std::chrono::seconds(30));
            return 0;
        }
#endif
        const std::string user_dir = Eden::UserDir();
#ifdef EDEN_PS5_OPENGL
        const auto native_shader_cache = std::filesystem::path{user_dir} / "cache/native-opengl";
        if (setenv("PS5_GLTHREAD", "1", 1) != 0)
            throw std::runtime_error("Cannot configure GL worker");
#if defined(EDEN_DEV_PROFILE) && !defined(EDEN_DEV_VULKAN)
        Eden::Performance::BeginPcSampling();
        // A driver rebuild already invalidates these compiler records. Drop its
        // obsolete slots so repeated development builds fit the 256 MiB sandbox.
        std::string cached_runtime;
        std::ifstream(native_shader_cache / "development-runtime") >> cached_runtime;
        if (cached_runtime != EDEN_DEV_GL_VERSION) {
            std::filesystem::create_directories(native_shader_cache);
            std::error_code directory_error;
            size_t removed = 0;
            for (const auto& entry : Eden::ReadNativeDirectory(native_shader_cache, directory_error)) {
                const auto name = entry.path().filename().string();
                const auto dot = name.find(".bin");
                if ((dot == 3 || dot == 4) &&
                    name.find_first_not_of("0123456789abcdef", 0) == dot)
                    removed += std::filesystem::remove(entry.path());
            }
            if (directory_error) throw std::system_error(directory_error, "Read native compiler cache");
            std::ofstream(native_shader_cache / "development-runtime") << EDEN_DEV_GL_VERSION;
            std::printf("EDEN_DEV_CACHE removed_obsolete_files=%zu\n", size_t(removed));
        }
        for (unsigned i = 0; i < 8; ++i)
            std::filesystem::remove(std::filesystem::path{Eden::LogsDir()} /
                                    ("pass-" + std::to_string(i) + ".ppm"));
#endif
        std::error_code cache_error;
        std::filesystem::create_directories(native_shader_cache, cache_error);
        if (!cache_error && setenv("PS5_SHADER_CACHE_DIR", native_shader_cache.c_str(), 1) != 0)
            throw std::runtime_error("Cannot configure native shader cache");
#endif
        (void)argc;
        (void)argv;
        std::string launch_error;
        // A game that faulted early in its boot is restarted (at most four times per launch).
        std::string relaunch_game;
        unsigned guest_fault_retries = 0;
#ifdef EDEN_DEV_VULKAN
        std::string recovery_mode;
        std::ifstream(Eden::AppFile("backend-recovery.txt")) >> recovery_mode;
        const bool check_backend_recovery = !recovery_mode.empty();
        bool recovery_opengl = false;
#endif
#if defined(EDEN_DEV_PROFILE) || defined(EDEN_DEV_ROM_ID)
        bool autoboot_pending = true;
        // dev-settings rom=TITLEID boots another title with the same development package
        // (tests on two consoles); the build's development title is the default.
#ifdef EDEN_DEV_ROM_ID
        std::string development_id = EDEN_DEV_ROM_ID;
#else
        std::string development_id = EDEN_DEV_PROFILE_TITLE;
#endif
        {
            std::ifstream dev_settings(Eden::AppFile("dev-settings.txt"));
            for (std::string entry; dev_settings >> entry;) {
                if (entry.starts_with("rom=") && entry.size() == 20) development_id = entry.substr(4);
                // dev-settings launcher=first opens the launcher instead of the development title
                // (launcher work: its captures and file-driven input need a development build).
                if (entry == "launcher=first") autoboot_pending = false;
            }
        }
#endif
        for (;;) {
#ifdef EDEN_PS5_OPENGL
        std::error_code trim_error;
        const auto cache_entries = Eden::ReadNativeDirectory(native_shader_cache, trim_error);
        if (!trim_error) {
            const auto removed = Eden::TrimShaderCache(cache_entries);
            if (removed) Eden::Report("cache", "Removed old shader cache records to reserve writable storage");
        }
#endif
        std::string selected_game;
#ifdef EDEN_DEV_VULKAN
        if (check_backend_recovery && !recovery_opengl && !launch_error.empty()) {
            recovery_opengl = true;
            autoboot_pending = true;
            Eden::Report("recovery check", "Starting OpenGL after Vulkan session failure");
        }
        const bool automatic_launch = autoboot_pending;
#endif
        if (!relaunch_game.empty()) {
            selected_game = std::exchange(relaunch_game, {});
        } else {
        guest_fault_retries = 0;
#if defined(EDEN_DEV_PROFILE) || defined(EDEN_DEV_ROM_ID)
        if (std::exchange(autoboot_pending, false)) {
        // Match the title ID in the file name, else in the ROM's own metadata; the game files
        // folder first, then the pre-1.000.020 assets folder that still holds other test titles.
        const auto development_title = std::strtoull(development_id.c_str(), nullptr, 16);
        for (const std::string& folder : {Eden::AssetsPath("roms"), std::string{"/data/assets/roms"}}) {
            std::error_code rom_error;
            for (const auto& entry : Eden::ReadNativeDirectory(folder, rom_error)) {
                const auto filename = entry.path().filename().string();
                const auto extension = entry.path().extension();
                if (extension != ".nsp" && extension != ".xci") continue;
                const auto path = folder + "/" + filename;
                if (filename.find(development_id) != std::string::npos ||
                    eden_game_title_id(path.c_str()) == development_title) {
                    selected_game = path;
                    break;
                }
            }
            if (!selected_game.empty()) break;
        }
        if (selected_game.empty())
            throw std::runtime_error("Development ROM not found");
        } else {
            selected_game = SelectProsperoEdenGame(launch_error);
        }
#else
        selected_game = SelectProsperoEdenGame(launch_error);
#endif
        }
        if (selected_game.empty()) {
            Eden::Report("exit", "Launcher closed");
            return 0;
        }
        try {
        launch_error.clear();
#ifdef EDEN_DEV_VULKAN
        Eden::Performance::vulkan_cost_enabled = std::filesystem::exists(Eden::AppFile("cost-run.txt"));
        const bool performance_run = std::filesystem::exists(Eden::AppFile("performance-run.txt"));
#ifdef EDEN_DEV_PROFILE
        // Optional 20 Hz GPU-thread PC samples. The handler must exist before the
        // GPU thread registers, which unblocks SIGUSR2 only while sampling is on.
        const bool pc_sample_run = std::filesystem::exists(Eden::AppFile("pc-sample.txt"));
        static bool pc_sampler_installed = false;
        if (pc_sample_run && !pc_sampler_installed) {
            Eden::Performance::BeginPcSampling();
            pc_sampler_installed = true;
        }
#endif
        setenv("PS5VK_QUIET_LOG", performance_run ? "1" : "0", 1);
        // Bounded development cost breakdowns; never enable per-draw tracing.
        setenv("PS5VK_COST_LOG", std::filesystem::exists(Eden::AppFile("cost-run.txt")) ? "1" : "0", 1);
        if (performance_run) unsetenv("PS5VK_CAPTURE_SCANOUT");
        else setenv("PS5VK_CAPTURE_SCANOUT", "1", 1);
        std::printf("EDEN_VULKAN_MEASUREMENT quiet=%d captures=%d\n",
                    performance_run, !performance_run);
        const auto game_video = Eden::LoadGameSettings(eden_game_title_id(selected_game.c_str()));
        const auto backend = automatic_launch ?
            (recovery_opengl ? Eden::GraphicsBackend::OpenGL : Eden::GraphicsBackend::Vulkan) :
            game_video.renderer >= 0 ?
            (game_video.renderer == 0 ? Eden::GraphicsBackend::OpenGL : Eden::GraphicsBackend::Vulkan) :
            Eden::LoadPreferences().backend;
#else
        // Library > Game settings take precedence over Settings > Video.
        const auto game_video = Eden::LoadGameSettings(eden_game_title_id(selected_game.c_str()));
        const auto backend = game_video.renderer >= 0 ?
            (game_video.renderer == 0 ? Eden::GraphicsBackend::OpenGL : Eden::GraphicsBackend::Vulkan) :
            Eden::LoadPreferences().backend;
#ifdef EDEN_PS5_VULKAN
        // The user-facing diagnostics option controls Eden logs, not synchronous
        // driver traces for every draw. Keep those in explicit development probes.
        setenv("PS5VK_QUIET_LOG", "1", 1);
#endif
#endif
#ifndef EDEN_PS5_VULKAN
        if (backend == Eden::GraphicsBackend::Vulkan)
            throw std::runtime_error("Vulkan is not available in this build yet. Select OpenGL in Settings > Video to play.");
#endif
        Eden::Report("launch", Eden::BackendName(backend));
        const bool game = std::filesystem::is_regular_file(selected_game);
        if (!game) throw std::runtime_error("Selected ROM is no longer available");
        const char* guest = selected_game.c_str();
        const unsigned cycles = game ? 1 : 3;
        const bool devices = EDEN_DEVICE_FRONTEND;
        if (game) {
            for (const char* capture : {"game-frame.ppm", "scanout.ppm", "game-hud.ppm", "game-hud-steady.ppm",
                                        "game-hud-late.ppm"})
                std::filesystem::remove(std::filesystem::path{Eden::LogsDir()} / capture);
            // Firmware and keys stay in the user-provided read-only assets tree.
            std::puts("EDEN_GAME_ASSETS_READY");
        }
        constexpr bool cpu_pressure = false;
        constexpr bool shutdown_sweep = false;
#else
        const char* guest = argc >= 2 ? argv[1] : nullptr;
        unsigned cycles = 1;
        bool devices = false;
        bool game = false;
        bool cpu_pressure = false;
        bool shutdown_sweep = false;
        for (int i = 2; i < argc; ++i) {
            if (std::string_view{argv[i]} == "--repeat" && cycles == 1) cycles = 3;
            else if (std::string_view{argv[i]} == "--soak" && cycles == 1) cycles = 20;
            else if (std::string_view{argv[i]} == "--devices" && !devices) devices = true;
            else if (std::string_view{argv[i]} == "--game" && !game) game = true;
            else if (std::string_view{argv[i]} == "--cpu-pressure" && !cpu_pressure) cpu_pressure = true;
            else if (std::string_view{argv[i]} == "--shutdown-sweep" && !shutdown_sweep) shutdown_sweep = true;
            else return 2;
        }
        // Device mock assertions describe the homebrew fixture, not arbitrary games.
        if (game && devices) return 2;
        if (cpu_pressure && (game || devices || cycles == 20)) return 2;
        if (shutdown_sweep) {
            if (game || devices || cpu_pressure || cycles != 1) return 2;
            game = true;
            cycles = 6;
        }
        const char* user_dir = "user";
#endif
        const auto passed = [&](const char* name) {
#if defined(EDEN_DEV_PROFILE) && defined(PS5_NATIVE)
            Eden::Stall::Progress(name);
#endif
            std::fprintf(report, "%s\tPASS\n", name);
#ifdef PS5_NATIVE
            const auto mono_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            std::printf("EDEN_STAGE backend=%s phase=%s mono_ns=%lld\n",
                        Eden::BackendName(backend), name, static_cast<long long>(mono_ns));
            ps5_opengl_heap_snapshot(name, 0);
            std::fflush(stdout);
#else
            const auto heap = mallinfo2();
            std::fprintf(stderr, "host_heap phase=%s allocated_bytes=%zu arena_bytes=%zu mapped_bytes=%zu free_bytes=%zu\n",
                         name, heap.uordblks + heap.hblkhd, heap.arena, heap.hblkhd, heap.fordblks);
#endif
        };
#if defined(EDEN_DEV_PROFILE) && defined(PS5_NATIVE)
        Eden::Stall::Arm();
#endif
        passed("session_start");
        if (!std::filesystem::is_directory(user_dir)) {
            std::fputs("Run from the isolated directory containing user/.\n", stderr);
            return 2;
        }
        Common::FS::CreateEdenPaths();
#ifdef PS5_NATIVE
        Common::FS::SetEdenPath(Common::FS::EdenPath::KeysDir, Eden::AssetsPath("keys"));
#endif
#ifdef PS5_NATIVE
        if (Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir) != user_dir) return 2;
        std::puts("[headless-startup] absolute_paths_ready");
#endif
#ifdef PS5_NATIVE
        if (game) Eden::Performance::PlatformChecks();
#endif
        Common::Log::Initialize();
        if (game) {
            Common::Log::Filter filter;
            filter.SetClassLevel(Common::Log::Class::Service_FS, Common::Log::Level::Info);
#if defined(PS5_NATIVE) && !defined(EDEN_DEV_PROFILE) && !defined(EDEN_DEV_ROM_ID)
            if (Eden::LoadPreferences().detailed_logging) filter.ParseFilterString("*:Debug");
#endif
            Common::Log::SetGlobalFilter(filter);
        }
        Common::Log::Start();
        SCOPE_EXIT { Common::Log::Stop(); };
        #ifdef EDEN_PS5_OPENGL
        Settings::values.renderer_backend = backend == Eden::GraphicsBackend::Vulkan ?
            Settings::RendererBackend::Vulkan : Settings::RendererBackend::OpenGL_GLSL;
        // Keep emulator threads off guest cores 0-2 and their SMT siblings (Vulkan):
        // +17% in the heaviest measured window of a racing game.
        Eden::Performance::SetSecondaryPlacement(backend == Eden::GraphicsBackend::Vulkan);
        // A32 fastmem window (direct JIT accesses to aliased guest memory): opt-in with dev-settings
        // fastmem=on; on the console it is slower than the page-table path.
        Eden::Fastmem::Request(false);
#ifdef EDEN_DEV_VULKAN
        Settings::values.async_presentation = backend == Eden::GraphicsBackend::Vulkan;
        // Exercise upstream's checked initialization error, never an invalid pointer.
        Settings::values.vulkan_device = recovery_mode == "init-failure" && !recovery_opengl ?
            0xffffffffu : 0u;
#endif
        Settings::values.use_asynchronous_shaders = false;
        Settings::values.renderer_debug = false;
        // RADV presents the console's 12 GiB of direct memory as an integrated GPU, for which
        // Eden budgets 4 GiB: a game using ~4.4 GB of Vulkan memory then ran the texture GC
        // every frame (20-25 FPS). Eden's larger integrated budget (6 GiB) holds it at 30 FPS
        // with ~0.9 GB of direct memory to spare.
        Settings::values.vram_usage_mode.SetValue(Settings::VramUsageMode::Aggressive);
#else
        Settings::values.renderer_backend = Settings::RendererBackend::Null;
#endif
        Settings::values.cpu_accuracy = Settings::CpuAccuracy::Auto;
        Settings::values.memory_layout_mode = Settings::MemoryLayout::Memory_4Gb;
#ifdef EDEN_DEV_PROFILE
        // One-run A/B switches written by the development runner; absent = defaults.
        {
            std::ifstream dev_settings(Eden::AppFile("dev-settings.txt"));
            for (std::string entry; dev_settings >> entry;) {
                if (entry == "dma_accuracy=unsafe") {
                    Settings::values.dma_accuracy.SetValue(Settings::DmaAccuracy::Unsafe);
                } else if (entry == "placement=off") {
                    Eden::Performance::SetSecondaryPlacement(false);
                } else if (entry == "fastmem=off" || entry == "fastmem=on") {
                    Eden::Fastmem::Request(entry.ends_with("on"));
                } else if (entry.starts_with("idle_spin_us=")) {
                    // Guest cores spin this long on their interrupt flag before sleeping (W1).
                    Eden::Performance::idle_spin_iterations = static_cast<unsigned>(std::stoul(entry.substr(13)) * 50);
                } else if (entry.starts_with("pc_core=") && entry.size() == 9 && entry[8] >= '0' && entry[8] <= '3') {
                    // Host PC samples from this guest core instead of core 0 (with --pc-sample).
                    Eden::Performance::pc_sample_core = static_cast<unsigned>(entry[8] - '0');
                } else if (entry == "pc_fast=on") {
                    Eden::Performance::pc_fast = true;
                } else if (entry == "capture=early") {
                    Eden::Performance::capture_early = true;
                } else if (entry == "jit_dups=on") {
                    // Which cores compiled each A64 block, and when (EDEN_PERF_DUPLICATES).
                    Eden::Performance::jit_duplicate_tracking = true;
                } else if (entry == "jit_shared=off") {
                    // Every guest core keeps its own compiled blocks (headless/dynarmic/jit_group.h).
                    eden_jit_shared = false;
                } else if (entry == "cpu_accuracy=unsafe") {
                    // Dynarmic's unsafe FP shortcuts on top of Auto (reduced-error estimates, inaccurate NaN).
                    Settings::values.cpu_accuracy = Settings::CpuAccuracy::Unsafe;
                } else if (entry == "replay=off") {
                    // The profile title takes controller and runner input instead of the timed replay.
                } else if (entry == "large_pages=off") {
                    // Read directly by the page allocator (src/memory_pages.cpp) before this parse.
                } else if (entry == "gpu_accuracy=low") {
                    // Nothing calls UpdateGPUAccuracy() here; set the live value too.
                    Settings::values.gpu_accuracy.SetValue(Settings::GpuAccuracy::Low);
                    Settings::values.current_gpu_accuracy = Settings::GpuAccuracy::Low;
                } else if (entry == "null_descriptor=off") {
                    Eden::DevVulkan::disable_null_descriptor = true;
                } else if (entry == "descriptor_buffer=off") {
                    Eden::DevVulkan::disable_descriptor_buffer = true;
                } else if (entry == "robustness2=on") {
                    Eden::DevVulkan::robustness2 = true;
                } else if (entry == "vertex_input_dynamic=off") {
                    Settings::values.vertex_input_dynamic_state.SetValue(false);
                } else if (entry == "pipeline_trace=on") {
                    Eden::DevVulkan::trace_pipelines = true;
                } else if (entry.starts_with("radv_debug=")) {
                    // Read by RADV at instance creation, which happens after this parse.
                    setenv("RADV_DEBUG", entry.c_str() + 11, 1);
                } else if (entry.starts_with("env=") && entry.find('=', 4) != std::string::npos) {
                    // Other driver switches read at device creation, e.g. env=RADV_PS5_BORDER_REBIND=1.
                    const auto split = entry.find('=', 4);
                    setenv(entry.substr(4, split - 4).c_str(), entry.c_str() + split + 1, 1);
                } else if (entry == "sparse=off") {
                    Eden::DevVulkan::disable_sparse = true;
                } else if (entry == "multirange=off") {
                    Eden::DevVulkan::disable_multi_range = true;
                } else if (entry == "custom_border=off") {
                    Eden::DevVulkan::disable_custom_border = true;
                } else if (entry == "custom_border=on") {
                    Eden::DevVulkan::disable_custom_border = false;
                } else if (entry == "vram=aggressive") {
                    // Eden's own larger integrated-GPU budget (6 GiB instead of 4 GiB), the default.
                    Settings::values.vram_usage_mode.SetValue(Settings::VramUsageMode::Aggressive);
                } else if (entry == "vram=conservative") {
                    Settings::values.vram_usage_mode.SetValue(Settings::VramUsageMode::Conservative);
                } else if (entry == "conditional_rendering=off") {
                    Eden::DevVulkan::disable_conditional_rendering = true;
                } else if (entry == "conditional_rendering=on") {
                    Eden::DevVulkan::disable_conditional_rendering = false;
                } else if (entry == "gpu_time=on") {
                    Eden::DevVulkan::gpu_time = true;
                } else if (entry == "submit_sync=on") {
                    Eden::DevVulkan::sync_submissions = true;
                } else if (entry == "astc=cpu") {
                    Settings::values.accelerate_astc.SetValue(Settings::AstcDecodeMode::Cpu);
                } else if (entry == "log=render") {
                    // Renderer debug logging for crash probes (fetched every few seconds).
                    Common::Log::Filter filter;
                    filter.ParseFilterString("*:Info Render:Debug Render.Vulkan:Debug HW.GPU:Debug Shader:Debug");
                    Common::Log::SetGlobalFilter(filter);
                } else if (entry.starts_with("boot_trace=") && entry.find(':') != std::string::npos) {
                    // Individual guest GPU submissions/dispatches from START to END ms after now.
                    const auto colon = entry.find(':');
                    const long long now = Eden::Performance::NowNs();
                    Eden::Performance::boot_trace_begin = now + std::stoll(entry.substr(11, colon - 11)) * 1000000;
                    Eden::Performance::boot_trace_end = now + std::stoll(entry.substr(colon + 1)) * 1000000;
                } else if (entry == "hcr=eden" || entry == "hcr=exact" || entry == "hcr=cpu") {
                    Eden::DevVulkan::hcr_mode = entry == "hcr=eden" ? 0 : entry == "hcr=exact" ? 1 : 2;
                } else if (entry == "compute_barriers=off") {
                    Eden::DevVulkan::compute_barriers = false;
                } else if (entry == "gpu_clock=normal" || entry == "gpu_clock=boost" || entry == "gpu_clock=overclock") {
                    // Guest-visible GPU timestamp scale (Eden's fast_gpu_time; Boost is the default).
                    Settings::values.gpu_clock.SetValue(entry.ends_with("normal") ? Settings::GpuClock::Normal :
                        entry.ends_with("boost") ? Settings::GpuClock::Boost : Settings::GpuClock::Overclock);
                } else if (entry.starts_with("rom=")) {
                    // Development title override, applied when the title is selected.
                } else if (entry == "fs_callers=on") {
                    Eden::Performance::trace_fs_callers = true;
                } else if (entry.starts_with("log_filter=")) {
                    // Any Eden log filter for crash probes, '+' for spaces,
                    // e.g. log_filter=*:Info+Service:Debug+Service.HID:Info.
                    std::string text = entry.substr(11);
                    std::replace(text.begin(), text.end(), '+', ' ');
                    Common::Log::Filter filter;
                    filter.ParseFilterString(text);
                    Common::Log::SetGlobalFilter(filter);
                } else if (Eden::Watch::Parse(entry)) {
                    // Write watch / code dump on the main module, armed after load (watch.h).
                } else if (entry.starts_with("dyna_state=") && entry.size() == 12 &&
                           entry[11] >= '0' && entry[11] <= '3') {
                    Settings::values.dyna_state.SetValue(
                        static_cast<Settings::ExtendedDynamicState>(entry[11] - '0'));
                } else {
                    std::printf("EDEN_DEV_SETTINGS unknown=%s\n", entry.c_str());
                }
            }
            std::printf("EDEN_DEV_SETTINGS dma_accuracy=%u gpu_accuracy=%u null_descriptor=%u "
                        "descriptor_buffer=%u robustness2=%u vertex_input_dynamic=%u dyna_state=%u "
                        "sparse=%u multirange=%u custom_border=%u submit_sync=%u fastmem=%u\n",
                        unsigned(Settings::values.dma_accuracy.GetValue()),
                        unsigned(Settings::values.current_gpu_accuracy),
                        unsigned(!Eden::DevVulkan::disable_null_descriptor),
                        unsigned(!Eden::DevVulkan::disable_descriptor_buffer),
                        unsigned(Eden::DevVulkan::robustness2),
                        unsigned(Settings::values.vertex_input_dynamic_state.GetValue()),
                        unsigned(Settings::values.dyna_state.GetValue()),
                        unsigned(!Eden::DevVulkan::disable_sparse),
                        unsigned(!Eden::DevVulkan::disable_multi_range),
                        unsigned(!Eden::DevVulkan::disable_custom_border),
                        unsigned(Eden::DevVulkan::sync_submissions),
                        unsigned(Eden::Fastmem::Requested()));
            std::printf("EDEN_DEV_MEMORY direct_memory=%lld vram_mode=%u\n",
                        static_cast<long long>(sceKernelGetDirectMemorySize()),
                        unsigned(Settings::values.vram_usage_mode.GetValue()));
        }
        Settings::values.use_docked_mode.SetValue(Settings::ConsoleMode::Docked);
        std::puts("[headless-startup] console_mode=docked");
#else
        Settings::values.use_docked_mode.SetValue(Settings::ConsoleMode::Handheld);
#if defined(PS5_NATIVE) && defined(EDEN_PS5_OPENGL)
        const bool docked = Eden::LoadGameDocked(eden_game_title_id(guest));
        Settings::values.use_docked_mode.SetValue(docked ? Settings::ConsoleMode::Docked
                                                       : Settings::ConsoleMode::Handheld);
        Eden::Report("launch", docked ? "Console mode: Docked" : "Console mode: Handheld");
#endif
#endif
#if defined(PS5_NATIVE) && defined(EDEN_PS5_OPENGL)
        {
            // Settings > Video: internal resolution and the filter scaling it to the output.
            static constexpr Settings::ResolutionSetup resolutions[] = {
                Settings::ResolutionSetup::Res1_2X, Settings::ResolutionSetup::Res3_4X, Settings::ResolutionSetup::Res1X,
                Settings::ResolutionSetup::Res3_2X, Settings::ResolutionSetup::Res2X};
            static constexpr Settings::ScalingFilter filters[] = {
                Settings::ScalingFilter::Bilinear, Settings::ScalingFilter::Fsr, Settings::ScalingFilter::Bicubic,
                Settings::ScalingFilter::NearestNeighbor};
            const auto video = Eden::LoadPreferences();
            const int resolution = game_video.resolution >= 0 ? game_video.resolution : video.resolution;
            const int filter = game_video.upscaling_filter >= 0 ? game_video.upscaling_filter : video.upscaling_filter;
            Settings::values.resolution_setup.SetValue(resolutions[resolution]);
            Settings::values.scaling_filter.SetValue(filters[filter]);
            Settings::UpdateRescalingInfo();
            Eden::Report("launch", (std::string("Resolution ") + Eden::kResolutionKeys[resolution] + ", " +
                                    Eden::kUpscalingFilterLabels[filter]).c_str());
        }
#endif
        Settings::values.sink_id = Settings::AudioEngine::Null;
        Settings::values.use_multi_core = true;
#ifdef EDEN_DEV_PROFILE
        // Use Eden's existing Mii creation frontend for unattended first-run setup.
        Settings::values.mii_edit_applet_mode = Settings::AppletMode::HLE;
#endif
        // The PS5 build bounds Eden's GPU command queue below, so the CPU and renderer can run in
        // parallel without accumulating the multi-frame controller lag of the upstream queue.
        Settings::values.use_asynchronous_gpu_emulation = true;
#ifdef EDEN_PS5_OPENGL
        Settings::values.use_disk_shader_cache = game;
#else
        Settings::values.use_disk_shader_cache = false;
#endif
        // The PS5 port has software FFmpeg decoders but no FFmpeg hardware-device adapter.
        Settings::values.nvdec_emulation = game ? Settings::NvdecEmulation::Cpu
                                                : Settings::NvdecEmulation::Off;
        Settings::values.use_gdbstub = false;
        // Settings > Language: the system language games see, with its console region.
        static_assert(static_cast<int>(Settings::Language::EnglishBritish) == 12 &&
                      static_cast<int>(Settings::Language::ChineseTraditional) == 16 &&
                      static_cast<int>(Settings::Language::Thai) == 19 &&
                      static_cast<int>(Settings::Region::Taiwan) == 6, "Eden's language or region order changed");
        {
            // Eden only finds a game's supported languages when an update supplies its control data.
            // Without them it hands the game the chosen language as it is, and a game that does not
            // know it (an older one given pt-BR) falls back to Japanese. So the game gets its own
            // closest language, the one the launcher shows for it (eden_game_language).
            const int choice = Eden::LoadPreferences().language;
            int language = Eden::kLanguageSettings[choice];
#ifdef PS5_NATIVE
            if (game && guest)
                language = eden_game_language(guest, Eden::AssetsPath("keys").c_str(), eden_game_title_id(guest), language);
#endif
            Settings::values.language_index.SetValue(static_cast<Settings::Language>(language));
            Settings::values.region_index.SetValue(static_cast<Settings::Region>(Eden::kLanguageRegions[choice]));
            const std::string code(reinterpret_cast<const char*>(
                &Service::Set::available_language_codes[static_cast<std::size_t>(language)]), 8);
            Eden::Report("launch", ("Language: " + std::string(code.c_str()) + " (chosen " +
                                    Eden::kLanguageKeys[choice] + ")").c_str());
        }
        std::unique_ptr<Eden::Pad> pad;
        bool return_to_menu = false;
        if (devices || game) {
            pad = std::make_unique<Eden::Pad>();
            if (!pad->Open()) throw std::runtime_error("PS5 controller initialization failed");
            Settings::values.audio_output_device_id = "ps5";
            Settings::values.vibration_enabled.SetValue(Eden::LoadPreferences().vibration);
            // One Pro Controller per signed-in user's DualSense; later changes apply mid-game.
            const unsigned connected = pad->ConnectedPlayers();
            (void)pad->TakeConnectionChanges();
            for (std::size_t index = 0; index < Eden::Pad::kMaxPlayers; ++index) {
                auto& player = Settings::values.players.GetValue()[index];
                player.connected = index == 0 || (connected & (1u << index)) != 0;
                player.controller_type = Settings::ControllerType::ProController;
                // DualSense rumble (headless/pad.cpp) at Eden's full strength.
                player.vibration_enabled = true;
                player.vibration_strength = 100;
            }
        }
        {
#ifdef EDEN_PS5_OPENGL
            Eden::GraphicsWindow window(backend == Eden::GraphicsBackend::Vulkan);
#ifdef EDEN_GPU_PROBE
            window.RunGpuProbe();
            passed("GPU_PROBE_COMPLETE");
            return 0;
#endif
#else
            HeadlessWindow window;
#endif
            Core::System system;
            passed("core_constructed");
#ifdef EDEN_PS5_OPENGL
            window.SetSystem(system);
#endif
            system.Initialize();
            if (system.IsPoweredOn()) return 1;
            passed("core_initialized");
            for (unsigned cycle = 0; guest && cycle < cycles; ++cycle) {
                if (game) LOG_INFO(Frontend, "EDEN_GAME_SESSION_BEGIN {}", cycle + 1);
                struct Completion {
                    std::mutex mutex;
                    std::condition_variable wake;
                    bool exited = false;
                    bool captured = false;
                    bool return_to_menu = false;
                    std::exception_ptr failure;
                    std::string guest_fault;
                };
                auto completion = std::make_shared<Completion>();
                system.RegisterExitCallback([completion] {
                    std::lock_guard lock(completion->mutex);
                    completion->exited = true;
                    completion->wake.notify_one();
                });
                SCOPE_EXIT {
                    if (system.IsPoweredOn()) system.ShutdownMainProcess();
                    system.RegisterExitCallback({});
                };
                // Like Eden's Qt/Android frontends, reset shutdown state for each load.
                system.SetShuttingDown(false);
                // Update and DLC files (NSP or XCI) in the game files' updates folder apply to their
                // base game: Eden's ExternalContentProvider, which the patch manager checks first.
                Settings::values.external_content_dirs = {Eden::AssetsPath("updates")};
                system.GetFileSystemController().CreateFactories(*system.GetFilesystem());
                // The game's own contents, registered as Eden's desktop frontends do before booting a
                // game (ConfigureFilesystemProvider). An update's data is a patch on the base game's:
                // the patch manager looks the base up here, and without it applied only the update's
                // code, which then ran against the old data. It also gives Eden the game's control data.
                static FileSys::ManualContentProvider game_contents;
                game_contents.ClearAllEntries();
                if (game && guest && !game_contents.AddEntriesFromContainer(
                        system.GetFilesystem()->OpenFile(guest, FileSys::OpenMode::Read)))
                    Eden::Report("loader", "Game contents not registered; an update's data will not apply");
                system.RegisterContentProvider(FileSys::ContentProviderUnionSlot::FrontendManual, &game_contents);
                if (pad) {
                    // A game's "connect controllers" screen: one player per PS5 controller in use.
                    Service::AM::Frontend::FrontendAppletSet applets;
                    applets.controller = std::make_unique<Eden::PadControllerApplet>(system.HIDCore(), *pad);
                    system.GetFrontendAppletHolder().SetFrontendAppletSet(std::move(applets));
                }
                Service::AM::FrontendAppletParameters params{
                    .applet_id = Service::AM::AppletId::Application,
                };
                Core::SystemResultStatus loaded;
                try {
                    loaded = system.Load(window, guest, params);
                } catch (const std::exception& error) {
                    // Preserve the original error before partial core teardown.
                    std::fprintf(stderr, "Game load failed: %s\n", error.what());
                    std::fflush(stdout);
                    throw;
                }
                if (loaded != Core::SystemResultStatus::Success) {
                    std::fprintf(stderr, "Core load failed: %u\n", static_cast<unsigned>(loaded));
                    if (loaded == Core::SystemResultStatus::ErrorVideoCore) {
                        throw std::runtime_error("Graphics backend initialization failed. Try another backend in Settings; see stderr.log and eden_log.txt for driver details.");
                    }
                    throw std::runtime_error("Loader status " + std::to_string(static_cast<unsigned>(loaded)) +
                        ". Check this ROM, its keys and firmware; see stderr.log.");
                }
#ifdef PS5_NATIVE
                if (game) {
                    const auto filename = std::filesystem::path(guest).filename().string();
                    const bool saved_last = Eden::SaveLastGame(filename);
                    const bool saved_recent = Eden::SaveRecentGame(filename);
                    if (!saved_last || !saved_recent)
                        Eden::Report("history", "Could not save recent game");
                }
#endif
                passed(game ? "game_loaded" : "nro_loaded");
                Eden::Report("loader", "Game loaded; initializing renderer");
#ifdef EDEN_PS5_OPENGL
                // Retain the failure, then release CPU readiness and complete normal
                // shutdown before reporting it. Unwinding before OnGpuReady can hang.
                std::exception_ptr graphics_error;
#ifndef PS5_NATIVE
                std::shared_future<bool> game_capture;
                try { window.CheckPresentation(system.GPU().Renderer().Context()); }
                catch (...) { graphics_error = std::current_exception(); }
                if (game && !graphics_error)
                    game_capture = window.CaptureNextFrame(system.GPU().Renderer(), [completion] {
                        std::lock_guard lock(completion->mutex);
                        completion->captured = true;
                        completion->wake.notify_one();
                    });
#endif
#endif
#if defined(EDEN_DEV_PROFILE) && defined(PS5_NATIVE)
                Eden::Stall::Trace("main gpu_start");
#endif
                system.GPU().Start();
#if defined(EDEN_DEV_PROFILE) && defined(PS5_NATIVE)
                Eden::Stall::Trace("main gpu_started");
#endif
                system.GetCpuManager().OnGpuReady();
                passed("cpu_manager_ready");
#if defined(EDEN_DEV_PROFILE) && defined(PS5_NATIVE)
                Eden::Stall::Trace("main shader_cache");
#endif
#ifdef EDEN_PS5_OPENGL
                // Match yuzu_cmd's ordering: GPU/context ready, cache load, guest Run.
                if (Settings::values.use_disk_shader_cache.GetValue() && !graphics_error) {
                    // Every cached pipeline is built before the game starts; report progress
                    // every five seconds so a slow build can be told apart from a stalled one.
                    std::atomic<size_t> built{0}, total{0};
                    std::atomic<bool> counted{false};
                    const auto load_start = std::chrono::steady_clock::now();
                    std::jthread reporter([&](std::stop_token stop) {
#if defined(EDEN_DEV_PROFILE) && defined(PS5_NATIVE)
                        Eden::Stall::Trace("reporter start");
#endif
                        for (unsigned tick = 1; !stop.stop_requested(); ++tick) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            if (tick % 50 != 0 || stop.stop_requested()) continue;
                            const std::string line = counted
                                ? fmt::format("Shader cache: {} of {} pipelines built ({} s)",
                                              built.load(), total.load(), tick / 10)
                                : fmt::format("Shader cache: reading ({} s)", tick / 10);
                            Eden::Report("loader", line.c_str());
                        }
                    });
#if defined(EDEN_DEV_PROFILE) && defined(PS5_NATIVE)
                    Eden::Stall::Trace("main cache_load");
#endif
                    system.Renderer().ReadRasterizer()->LoadDiskResources(
                        system.GetApplicationProcessProgramID(), std::stop_token{},
                        [&](VideoCore::LoadCallbackStage stage, size_t value, size_t count) {
                            if (stage != VideoCore::LoadCallbackStage::Build) return;
                            built = value;
                            total = count;
                            counted = true;
#if defined(EDEN_DEV_PROFILE) && defined(PS5_NATIVE)
                            Eden::Stall::Tick();
#endif
                        });
                    reporter.request_stop();
                    reporter.join();
                    const auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - load_start).count();
                    Eden::Report("loader", fmt::format("Shader cache ready: {} pipelines in {} ms",
                                                       total.load(), load_ms).c_str());
                    std::puts("EDEN_SHADER_CACHE_LOADED");
                    std::fflush(stdout);
                }
#endif
#ifndef PS5_NATIVE
                if (devices) Eden::Mock::BeginGuestCycle();
#endif
#ifdef EDEN_DEV_PROFILE
                if (auto* process = system.ApplicationProcess()) {
                    // P1: guest code reads for the core-0 profile's block dumps.
                    static Core::Memory::Memory* dev_guest_memory = nullptr;
                    dev_guest_memory = &process->GetMemory();
                    Eden::Performance::guest_read32 = [](unsigned long long address, unsigned& value) {
                        if (!dev_guest_memory || !dev_guest_memory->IsValidVirtualAddressRange(address, 4)) return false;
                        value = dev_guest_memory->Read32(address);
                        return true;
                    };
                    std::printf("EDEN_MAIN_BASE main=%llx\n",
                                static_cast<unsigned long long>(GetInteger(Core::FindMainModuleEntrypoint(process))));
                }
                if (auto* process = system.ApplicationProcess();
                    process && (Eden::Watch::watch_range.size || Eden::Watch::dump_range.size)) {
                    // Resolve dev-settings watch=/dump= against this boot's main module.
                    const u64 main_base = GetInteger(Core::FindMainModuleEntrypoint(process));
                    auto& memory = process->GetMemory();
                    if (const auto range = Eden::Watch::dump_range; range.size) {
                        for (u64 at = main_base + range.offset; at < main_base + range.offset + range.size; at += 0x40) {
                            std::string line = fmt::format("EDEN_CRASH_CODE dump ret={:016X} from={:016X}:", at, at);
                            for (u64 word = at; word < at + 0x40; word += 4)
                                line += fmt::format(" {:08X}", memory.IsValidVirtualAddressRange(word, 4) ? memory.Read32(word) : 0u);
                            LOG_CRITICAL(Core_ARM, "{}", line);
                        }
                    }
                    if (const auto range = Eden::Watch::watch_range; range.size) {
                        const u64 first = main_base + range.offset;
                        Eden::Watch::end = first + range.size;
                        Eden::Watch::begin = first;
                        memory.MarkRegionDebug(first, range.size, true);
                    }
                    LOG_CRITICAL(Core_ARM, "EDEN_WATCH main={:016X} watch=+{:#x}:{:#x} dump=+{:#x}:{:#x}", main_base,
                                 Eden::Watch::watch_range.offset, Eden::Watch::watch_range.size,
                                 Eden::Watch::dump_range.offset, Eden::Watch::dump_range.size);
                }
#endif
                Eden::TakeGuestFault(); // Nothing from an earlier session belongs to this one.
                system.Run();
#if defined(EDEN_DEV_PROFILE) && defined(PS5_NATIVE)
                Eden::Stall::Trace("main running");
                Eden::Stall::Disarm();
#endif
                const auto session_start = std::chrono::steady_clock::now();
                [[maybe_unused]] double session_seconds = 0;  // guest-fault relaunch (PS5 only)
                std::jthread input_worker;
                if (pad) input_worker = std::jthread([&](std::stop_token stop) {
#ifdef EDEN_DEV_PROFILE
                    // The timed input replay drives the profile's own title (EDEN_DEV_PROFILE_TITLE);
                    // other development titles take runner commands (compat-input.txt).
                    bool replay_off = false;
                    {
                        std::ifstream dev_settings(Eden::AppFile("dev-settings.txt"));
                        for (std::string entry; dev_settings >> entry;) replay_off |= entry == "replay=off";
                    }
                    const bool timed_replay = development_id == EDEN_DEV_PROFILE_TITLE && !replay_off;
                    const auto replay_start = std::chrono::steady_clock::now();
#endif
#ifdef EDEN_DEV_ROM_ID
                    Eden::DevelopmentInput development_input;
                    unsigned command_poll = 0;
#endif
                    while (!stop.stop_requested()) {
                        if (auto error = Eden::TakeGpuFailure()) {
                            std::lock_guard lock(completion->mutex);
                            completion->failure = std::move(error);
                            completion->wake.notify_one();
                            break;
                        }
                        if (auto fault = Eden::TakeGuestFault(); !fault.empty()) {
                            std::lock_guard lock(completion->mutex);
                            completion->guest_fault = std::move(fault);
                            completion->wake.notify_one();
                            break;
                        }
#ifdef EDEN_DEV_PROFILE
                        if (!timed_replay) {
#endif
#ifdef EDEN_DEV_ROM_ID
                        if (!development_input.active) pad->Poll();
#else
                        pad->Poll();
#endif
#ifdef EDEN_DEV_ROM_ID
                        const auto command_now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();
                        if (++command_poll >= 25) {
                            command_poll = 0;
                            std::ifstream command(Eden::AppFile("compat-input.txt"));
                            if (development_input.Read(command, command_now))
                                std::printf("EDEN_DEV_INPUT sequence=%llu buttons=%x\n",
                                    static_cast<unsigned long long>(development_input.sequence), development_input.buttons);
                        }
                        if (const auto sample = development_input.Sample(command_now))
                            pad->Consume({&*sample, 1});
#endif
#ifdef EDEN_DEV_PROFILE
                        } else {
                        using Button = InputCommon::VirtualGamepad::VirtualButton;
                        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - replay_start).count();
                        // Development replay: enter Single Player/Grand Prix using defaults.
                        pad->Engine().SetButtonState(0, Button::TriggerL, seconds == 45);
                        pad->Engine().SetButtonState(0, Button::TriggerR, seconds == 45);
                        pad->Engine().SetButtonState(0, Button::ButtonA,
                            (seconds >= 50 && seconds < 200 && seconds % 5 == 0) || seconds >= 200);
                        }
#endif
                        if (const unsigned changed = pad->TakeConnectionChanges()) {
                            // A controller that came or went connects or disconnects its player.
                            const unsigned connected = pad->ConnectedPlayers();
                            for (std::size_t index = 1; index < Eden::Pad::kMaxPlayers; ++index) {
                                if (!(changed & (1u << index))) continue;
                                const bool present = (connected & (1u << index)) != 0;
                                Settings::values.players.GetValue()[index].connected = present;
                                auto* controller = system.HIDCore().GetEmulatedControllerByIndex(index);
                                if (present) {
                                    controller->SetNpadStyleIndex(Core::HID::NpadStyleIndex::Fullkey);
                                    controller->Connect();
                                } else {
                                    controller->Disconnect();
                                }
                                LOG_INFO(Input, "EDEN_PLAYER player={} connected={}", index + 1, present);
                            }
                        }
#ifdef EDEN_PS5_OPENGL
                        if (pad->TakeHudToggle()) Eden::ToggleHud();
#else
                        (void)pad->TakeHudToggle();
#endif
                        if (pad->TakeReturnToMenu()) {
                            std::lock_guard lock(completion->mutex);
                            completion->return_to_menu = true;
                            completion->wake.notify_one();
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(4));
                    }
                });
                {
                    std::unique_lock lock(completion->mutex);
                    constexpr unsigned stop_delays_ms[] = {0, 1, 10, 100, 1000, 30000};
                    const auto wait_ms = shutdown_sweep ? stop_delays_ms[cycle] :
                        1000u * (cpu_pressure ? 180u : game ? 600u : devices ? 20u : 5u);
                    if (shutdown_sweep) LOG_INFO(Frontend, "EDEN_SHUTDOWN_DELAY_MS {}", wait_ms);
                    const auto completed = [&] {
                        return completion->failure || completion->exited || completion->captured ||
                            completion->return_to_menu || !completion->guest_fault.empty();
                    };
#ifdef PS5_NATIVE
                    if (game) {
#ifdef EDEN_DEV_PROFILE
                        for (unsigned segment = 0; segment < 12; ++segment) {
                            bool finished = false;
                            for (unsigned poll = 0; poll < 600; ++poll) {
                                if (completion->wake.wait_for(lock, std::chrono::milliseconds(50), completed)) {
                                    finished = true;
                                    break;
                                }
#ifndef EDEN_DEV_VULKAN
                                if (segment >= 5) {
                                    lock.unlock();
                                    Eden::Performance::PollGpuPc();
                                    lock.lock();
                                }
#else
                                // Cover the heavy phase (wall ~90-180 s) when requested.
                                if (pc_sample_run && segment >= 3) {
                                    lock.unlock();
                                    Eden::Performance::PollGpuPc();
                                    lock.lock();
                                }
#endif
                            }
                            if (finished) break;
                            if (segment == 11) {
                                completion->wake.wait(lock, completed);
                                break;
                            }
                            lock.unlock();
#ifndef EDEN_DEV_VULKAN
                            Eden::Performance::Snapshot();
                            window.CaptureNextFrame(system.GPU().Renderer(), [] {});
#else
                            std::error_code capture_error;
                            if ((segment >= 4 || Eden::Performance::capture_early) &&
                                std::filesystem::remove(Eden::AppFile("capture-once.txt"), capture_error))
                                window.CaptureNextFrame(system.GPU().Renderer(), [] {});
#endif
                            lock.lock();
                        }
#elif defined(EDEN_DEV_ROM_ID)
                        // Bounded compatibility probe (10 minutes; the runner usually closes
                        // the title first); use the ordinary teardown path.
                        constexpr unsigned observation_samples = 120;
                        for (unsigned sample = 0; sample < observation_samples; ++sample) {
                            if (completion->wake.wait_for(lock, std::chrono::seconds(5), completed)) break;
                            if (sample == observation_samples - 1) {
                                completion->return_to_menu = true;
                                break;
                            }
                            lock.unlock();
#ifdef EDEN_DEV_VULKAN
                            std::error_code capture_error;
                            const bool capture_requested = performance_run && sample >= 11 &&
                                std::filesystem::remove(Eden::AppFile("capture-once.txt"), capture_error);
                            if (!performance_run || capture_requested)
#endif
                            window.CaptureNextFrame(system.GPU().Renderer(), [] {});
                            lock.lock();
                        }
#else
                        completion->wake.wait(lock, completed);
#endif
                    } else
#endif
                    completion->wake.wait_for(lock, std::chrono::milliseconds(wait_ms), completed);
                    const bool guest_exited = completion->exited;
                    return_to_menu = completion->return_to_menu;
                    session_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - session_start).count();
                    if (!guest_exited && !game) {
                        std::fputs("Guest exit timed out\n", stderr);
                        return 1;
                    }
                    passed(return_to_menu ? "shortcut_return_to_menu" :
                        guest_exited ? "guest_exit_callback" : !completion->guest_fault.empty() ? "guest_fault" :
                        completion->captured ? "game_capture_shutdown" : "game_observation_complete");
                }
                input_worker.request_stop();
                if (input_worker.joinable()) input_worker.join();
                if (pad) {
                    const auto neutral = ps5::pad::neutral_data();
                    pad->Consume({&neutral, 1});
                }
                // Shutdown requests cancellation before suspending cores; Pause can
                // block while a CPU producer is waiting on a full GPU queue.
                Eden::ReportStep("shutdown", "Stopping the game");
                system.ShutdownMainProcess();
                Eden::Report("shutdown", "Game stopped; releasing renderer");
#ifndef PS5_NATIVE
                if (devices) {
                    Eden::Mock::CheckGuestCycle();
                    LOG_INFO(Frontend, "EDEN_GUEST_AUDIO_OUTPUT_PASS");
                    LOG_INFO(Frontend, "EDEN_GUEST_RENDERER_PCM_PASS");
                }
#endif
                if (system.IsPoweredOn()) return 1;
                passed("core_shutdown");
                if (completion->failure) {
                    try { std::rethrow_exception(completion->failure); }
                    catch (const std::exception& error) {
                        throw std::runtime_error(std::string("Rendering failed: ") + error.what() +
                            ". Try another graphics backend in Settings, then reopen the game.");
                    }
                }
                if (!completion->guest_fault.empty()) {
#ifdef PS5_NATIVE
                    // A game can run its save-load completion before its own callback exists
                    // (a boot race between two guest threads); a fresh boot normally passes. Retry early faults, report others.
                    // Four retries: two faults in a row were seen with slower GPU synchronization
                    // (RADV_DEBUG=syncshaders), so the per-boot rate can exceed the usual ~1 in 5.
                    if (game && session_seconds < 60 && guest_fault_retries < 4) {
                        ++guest_fault_retries;
                        relaunch_game = selected_game;
                        return_to_menu = true;
                        LOG_WARNING(Frontend, "EDEN_GUEST_FAULT_RETRY {} after {:.1f} s: {}", guest_fault_retries,
                                    session_seconds, completion->guest_fault);
                        Eden::Report("guest fault", ("Restarting the game: " + completion->guest_fault).c_str());
                    } else
#endif
                    {
                        throw std::runtime_error("The game stopped: " + completion->guest_fault +
                            ". Reopen it from the launcher.");
                    }
                }
#ifdef EDEN_PS5_OPENGL
                if (graphics_error) std::rethrow_exception(graphics_error);
#ifndef PS5_NATIVE
                if (game) {
                    bool captured = false;
                    try {
                        captured = game_capture.wait_for(std::chrono::seconds(2)) ==
                            std::future_status::ready && game_capture.get();
                    } catch (const std::future_error& error) {
                        if (error.code() != std::make_error_code(std::future_errc::broken_promise)) throw;
                    }
                    if (!captured) throw std::runtime_error("No completed game frame capture");
                }
#endif
#endif
                if (game) LOG_INFO(Frontend, "EDEN_GAME_SESSION_END {}", cycle + 1);
            }
        }
        passed("core_destroyed");
        if (devices && pad) {
            pad.reset();
            LOG_INFO(Frontend, "EDEN_DEVICE_FRONTEND_PASS");
        }
#ifdef PS5_NATIVE
        if (return_to_menu) continue;
#endif
        passed("HEADLESS_COMPLETE");
#ifdef EDEN_DEV_PROFILE
        // Keep the sandbox mounted long enough to collect final driver counters.
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::seconds(20));
#endif
        return 0;
#ifdef PS5_NATIVE
        } catch (const std::exception& error) {
            launch_error = error.what();
            Eden::Report("session failed", error.what());
            std::fflush(stderr);
            std::fflush(stdout);
        }
        }
#endif
    } catch (const std::exception& error) {
        std::fflush(stdout);
        Eden::Report("fatal startup failure", error.what());
        return 1;
    }
}
