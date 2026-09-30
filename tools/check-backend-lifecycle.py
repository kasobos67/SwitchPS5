"""Exercise the actual generated destructor and splash callback without a GPU."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
cache = Path((root / '.local/headless-cache').read_text().strip())
main_source = (root/'headless/main.cpp').read_text()
selection_start = main_source.index('        const auto backend = automatic_launch ?')
development_selection = main_source[selection_start:main_source.index(';', selection_start)+1]

def body(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

destructor = body((cache / 'native-local/headless/gpu.cpp').read_text(), '~Impl()')
callback = body((root / 'headless/graphics.cpp').read_text(), 'void GraphicsWindow::OnFrameDisplayed()')
recovery = body((root / 'headless/main.cpp').read_text(),
                'if (check_backend_recovery && !recovery_opengl && !launch_error.empty())')
load_failure = body((root / 'headless/main.cpp').read_text(),
                    'if (loaded != Core::SystemResultStatus::Success)')
normal_policy = (root / 'headless/main.cpp').read_text().split(
    '        const auto backend = Eden::LoadPreferences().backend;', 1)[1].split(
    '#ifndef EDEN_PS5_VULKAN', 1)[0]
# The slice closes the enclosing development-mode conditional, outside this fixture.
assert normal_policy.endswith('#endif\n')
normal_policy = normal_policy[:-len('#endif\n')]
status = body((cache / 'source/src/core/core.h').read_text(), 'enum class SystemResultStatus')
harness = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <stdexcept>
#include <chrono>
#include <algorithm>
namespace Core { using u32 = unsigned; STATUS; }
namespace Settings {
enum class RendererBackend { OpenGL, Vulkan };
struct Selection { RendererBackend value; auto GetValue() { return value; } };
struct { Selection renderer_backend; } values;
bool IsOpenGL() { return values.renderer_backend.value == RendererBackend::OpenGL; }
}
int stops, currents, finishes, hides, hide_result;
void (*glFinish)();
struct Worker { void Stop() { ++stops; } };
struct Context { void MakeCurrent() { ++currents; } };
struct Renderer { struct Context context; auto& Context() { return context; } };
struct Impl { Worker gpu_thread; Renderer* renderer; DESTRUCTOR };
int sceSystemServiceHideSplashScreen() { ++hides; return hide_result; }
namespace Eden {
bool vulkan_loading = true;
enum class GraphicsBackend { OpenGL, Vulkan };
struct Preferences { bool detailed_logging; GraphicsBackend backend; };
Preferences test_preferences;
Preferences LoadPreferences() { return test_preferences; }
void Report(const char*, const char*) {}
struct GraphicsWindow { bool vulkan, splash_hidden = false, first_frame_reported = false;
double frame_sample_start=-1, frame_sample_last=0, frame_sample_worst=0;
unsigned frame_sample_count=0, frame_total=0; void OnFrameDisplayed(); };
CALLBACK
}
int main() {
    for (bool automatic_launch : {false, true}) {
        for (bool recovery_opengl : {false, true}) {
            for (auto saved : {Eden::GraphicsBackend::OpenGL, Eden::GraphicsBackend::Vulkan}) {
                Eden::test_preferences.backend = saved;
DEVELOPMENT_SELECTION
                assert(backend == (automatic_launch ? (recovery_opengl ?
                    Eden::GraphicsBackend::OpenGL : Eden::GraphicsBackend::Vulkan) : saved));
            }
        }
    }
    // A saved UI diagnostic preference must never re-enable per-draw driver I/O.
    for (bool detailed : {false, true}) {
        Eden::test_preferences.detailed_logging = detailed;
        setenv("PS5VK_QUIET_LOG", "0", 1);
#define EDEN_PS5_VULKAN 1
NORMAL_POLICY
#undef EDEN_PS5_VULKAN
        assert(std::string(getenv("PS5VK_QUIET_LOG")) == "1");
    }
    Renderer renderer;
    Settings::values.renderer_backend.value = Settings::RendererBackend::Vulkan;
    { Impl instance{{}, &renderer}; } // A GL call here would dereference NULL.
    assert(stops == 1 && currents == 0 && finishes == 0);
    Settings::values.renderer_backend.value = Settings::RendererBackend::OpenGL;
    glFinish = [] { ++finishes; };
    { Impl instance{{}, &renderer}; }
    { Impl instance{{}, nullptr}; }
    assert(stops == 3 && currents == 1 && finishes == 1);
    Eden::GraphicsWindow gl{false}, vk{true};
    gl.OnFrameDisplayed(); assert(hides == 0 && gl.first_frame_reported);
    hide_result = -1; vk.OnFrameDisplayed(); assert(!vk.splash_hidden && hides == 1);
    assert(!Eden::vulkan_loading); // First guest frame ends startup-only animation.
    hide_result = 0; vk.OnFrameDisplayed(); vk.OnFrameDisplayed();
    assert(vk.splash_hidden && hides == 2);
    assert(gl.frame_total == 0 && vk.frame_total == 3 && vk.frame_sample_count == 2);
    vk.frame_sample_start -= 6;
    vk.OnFrameDisplayed();
    assert(vk.frame_total == 4 && vk.frame_sample_count == 0 && vk.frame_sample_worst == 0);
    bool check_backend_recovery = false, recovery_opengl = false, autoboot_pending = false;
    std::string launch_error = "failure";
    auto retry = [&] { RECOVERY };
    retry(); assert(!autoboot_pending);
    check_backend_recovery = true; launch_error.clear();
    retry(); assert(!autoboot_pending);
    launch_error = "failure"; retry(); assert(autoboot_pending && recovery_opengl);
    autoboot_pending = false; retry(); assert(!autoboot_pending);
    auto load = [](Core::SystemResultStatus loaded) { LOAD_FAILURE };
    load(Core::SystemResultStatus::Success);
    try { load(Core::SystemResultStatus::ErrorVideoCore); assert(false); }
    catch (const std::runtime_error& e) {
        assert(std::string(e.what()).find("Graphics backend initialization failed") == 0);
        assert(std::string(e.what()).find("keys") == std::string::npos);
    }
    try { load(Core::SystemResultStatus::ErrorGetLoader); assert(false); }
    catch (const std::runtime_error& e) { assert(std::string(e.what()).find("Loader status") == 0); }
}
'''.replace('DESTRUCTOR', destructor).replace('CALLBACK', callback).replace('RECOVERY', recovery).replace('STATUS', status).replace('LOAD_FAILURE', load_failure).replace('NORMAL_POLICY', normal_policy).replace('DEVELOPMENT_SELECTION', development_selection)
with tempfile.TemporaryDirectory(prefix='eden-backend-') as directory:
    source = Path(directory) / 'check.cpp'
    source.write_text(harness)
    binary = Path(directory) / 'check'
    subprocess.run(['c++', '-std=c++20', '-DPS5_NATIVE', '-fsanitize=address,undefined',
                    str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Backend cleanup and splash lifecycle: ASan/UBSan PASS')
