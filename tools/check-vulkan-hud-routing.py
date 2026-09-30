#!/usr/bin/env python3
"""Check filter ABI against upstream and explicit display/capture HUD routing."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
cache = Path((root / '.local/headless-cache').read_text().strip())
header = cache / 'native-local/headless/include/video_core/present.h'
original = cache / 'source/src/video_core/present.h'
assert header.read_bytes() == original.read_bytes(), 'Do not alter the shared filter ABI'
generated = cache / 'native-local/headless'
renderer = (generated / 'vulkan_renderer.cpp').read_text()
assert renderer.count('swapchain.GetImageViewFormat(), true);') == 2 # Display + loading.
assert 'layout, 1, format, true);' in renderer # Frontend screenshot.
applet = renderer.split('void RendererVulkan::RenderAppletCaptureLayer(', 1)[1]
assert 'true);' not in applet.split('\n}', 1)[0] # Guest capture defaults to no HUD.
assert 'bool draw_hud = false' in (generated / 'include/video_core/renderer_vulkan/vk_blit_screen.h').read_text()
assert 'frame, draw_hud);' in (generated / 'vulkan_blit_screen.cpp').read_text()
with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    (work / 'common').mkdir()
    (work / 'common/settings.h').write_text('''#pragma once
namespace Settings {
enum class ScalingFilter { Bilinear }; enum class AntiAliasing { None };
template<class T> struct Value { T GetValue() const { return T{}; } };
inline struct { Value<ScalingFilter> scaling_filter; Value<AntiAliasing> anti_aliasing; } values;
}
''')
    (work / 'present.h').write_text(header.read_text())
    (work / 'original.h').write_text(original.read_text())
    (work / 'factory.cpp').write_text('''#include "present.h"
const PresentFilters& Display() { return PresentFiltersForDisplay; }
const PresentFilters& Capture() { return PresentFiltersForAppletCapture; }
''')
    (work / 'check.cpp').write_text('''#include "original.h"
#include <cassert>
const PresentFilters& Display(); const PresentFilters& Capture();
int main() {
    assert(&Display() != &PresentFiltersForDisplay); // Original address test failed.
    static_assert(sizeof(PresentFilters) == 2 * sizeof(void(*)()));
    assert(Display().get_scaling_filter() == Settings::ScalingFilter::Bilinear);
    assert(Capture().get_anti_aliasing() == Settings::AntiAliasing::None);
}
''')
    binary = work / 'check'
    subprocess.run(['c++', '-std=c++20', '-I'+str(work), str(work/'factory.cpp'),
                    str(work/'check.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Vulkan filter ABI and explicit display/capture HUD routing PASS')
symbols = subprocess.check_output(['nm', '-S', '-C', str(cache/'native-local/bin/eden-headless')], text=True)
filters = [line.split() for line in symbols.splitlines() if ' d PresentFiltersFor' in line]
assert len(filters) >= 4 and all(int(parts[1], 16) == 16 for parts in filters), filters
print('Linked OpenGL/Vulkan display and applet filter layouts match upstream PASS')
