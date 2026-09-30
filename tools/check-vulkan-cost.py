from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'common').mkdir()
 (p/'common/cpu_features.h').write_text('#include <chrono>\nnamespace Common { struct Clock { std::chrono::nanoseconds GetTimeNS() { return std::chrono::steady_clock::now().time_since_epoch(); } }; inline Clock clock; inline Clock& g_wall_clock=clock; }\n')
 (p/'test.cpp').write_text('''#include "performance.h"
#include <cassert>
#include <thread>
int main(){
 using namespace Eden::Performance;
 {auto timer=VulkanTimer(0); assert(!timer);}
 assert(vulkan_api[0].calls==0);
 vulkan_cost_enabled=true;
 {auto timer=VulkanTimer(0); assert(timer); std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(vulkan_api[0].calls==1 && vulkan_api[0].nanoseconds>0);
 try {auto timer=VulkanTimer(1); throw 1;} catch(int){}
 assert(vulkan_api[1].calls==1);
 for(unsigned i=2;i<vulkan_api.size();++i){ {auto timer=VulkanTimer(i);assert(timer);} assert(vulkan_api[i].calls==1); }
 ReportVulkan();
 vulkan_cost_enabled=false;
 {auto timer=VulkanTimer(0);assert(!timer);}
 assert(vulkan_api[0].calls==1);
}''')
 subprocess.run(['c++','-std=c++20','-fsanitize=address,undefined','-I'+str(p),'-I'+str(root/'headless'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 output=subprocess.check_output([str(p/'test')],text=True)
 assert len(output.splitlines())==24
 for name in ('pipeline_ready_wait','shader_prepare','pipeline_cache_save',
              'shader_pool_reset','shader_cfg','shader_ir','shader_spirv','shader_module'):
  assert f'api={name} calls=1 ' in output
print('Vulkan optional timers: disabled, enabled, exception cleanup PASS')
