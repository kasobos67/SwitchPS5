#!/usr/bin/env python3
"""Execute the actual adapter enumeration/selection with changing mode lists."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'headless/vulkan_surface.cpp').read_text()
body = source.split('    std::vector<VkDisplayModePropertiesKHR> modes;', 1)[1].split('    const VkDisplaySurfaceCreateInfoKHR info', 1)[0]
code = r'''
#include <vulkan/vulkan.h>
#include <vector>
#include <cassert>
namespace vk { struct Exception { VkResult result; Exception(VkResult r):result(r){} }; }
static unsigned scenario, reads;
static VkResult get_modes(VkPhysicalDevice, VkDisplayKHR, uint32_t* count, VkDisplayModePropertiesKHR* modes) {
 if (scenario == 4) return VK_ERROR_DEVICE_LOST;
 if (!modes) { *count = scenario == 2 ? 0 : (scenario == 0 ? 1 : 2); return VK_SUCCESS; }
 if (scenario == 3 && reads++ == 0) return VK_INCOMPLETE;
 modes[0].parameters.refreshRate = scenario == 0 ? 59940 : 119880;
 if (*count > 1) modes[1].parameters.refreshRate = 59940;
 return VK_SUCCESS;
}
static uint32_t select_mode() {
 uint32_t count=0; VkPhysicalDevice physical{}; VkDisplayPropertiesKHR display{};
 auto check=[](VkResult r){ if(r!=VK_SUCCESS)throw vk::Exception(r); };
 std::vector<VkDisplayModePropertiesKHR> modes;
BODY
 return mode.parameters.refreshRate;
}
int main() {
 for (scenario=0;scenario<5;++scenario) {
  reads=0;
  try { assert(select_mode()==59940); assert(scenario!=2 && scenario!=4); }
  catch(vk::Exception e) { assert((scenario==2 && e.result==VK_ERROR_INITIALIZATION_FAILED) ||
    (scenario==4 && e.result==VK_ERROR_DEVICE_LOST)); }
 }
}
'''.replace('BODY', body)
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp)/'modes.cpp'
    cpp.write_text(code)
    exe = Path(tmp)/'modes'
    subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I'+str(root.parent/'ps5-vulkan-eden/third_party/vulkan-headers/include'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=10)
print('Display modes: single, multiple, retry, empty and device failure PASS')
