#!/usr/bin/env python3
"""Run generated upstream presentation lifecycle with an injected copy failure."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
cache=Path((root/'.local/headless-cache').read_text().strip())
s=(cache/'native-local/headless/vulkan_present_manager.cpp').read_text()
header=(cache/'native-local/headless/include/video_core/renderer_vulkan/vk_present_manager.h').read_text()
assert header.index('std::exception_ptr present_failure;') < header.index('std::jthread present_thread;')
def function(name):
    start=s.index(name); begin=s.index('{',start); depth=1; end=begin+1
    while depth:
        depth += (s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]
assignment=s.split('present_thread = std::jthread(',1)[1].split('\n    }\n}',1)[0]
methods='\n'.join(function(n) for n in ['Frame* PresentManager::GetRenderFrame()', 'void PresentManager::WaitPresent()', 'void PresentManager::PresentThread(std::stop_token token)'])
code=r'''
#include <condition_variable>
#include <mutex>
#include <thread>
#include <deque>
#include <future>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <cassert>
#include <utility>
#include "gpu_failure.h"
namespace Common { enum class ThreadPriority{High};void SetCurrentThreadName(const char*){} void SetCurrentThreadPriority(ThreadPriority){} void SetCurrentThreadToPerformanceCores(){} }
struct Fence { void Wait(){} void Reset(){} };
struct Frame { Fence present_done; };
class PresentManager {
public:
 std::deque<Frame*> present_queue,free_queue;
 std::condition_variable_any frame_cv;std::condition_variable free_cv;
 std::mutex swapchain_mutex,queue_mutex,free_mutex;
 std::exception_ptr present_failure;
 std::jthread present_thread;
 bool use_present_thread=true,fail=true;
 void start(){present_thread=std::jthread(ASSIGNMENT
 }
 Frame* GetRenderFrame();void WaitPresent();void PresentThread(std::stop_token);
 void CopyToSwapchain(Frame*) {if(fail)throw std::runtime_error("injected copy failure");}
};
METHODS
int main(){
 for(bool fail:{true,false})for(int repeat=0;repeat<20;++repeat){
  PresentManager p;p.fail=fail;Frame f;
  auto get=std::async(std::launch::async,[&]{try{return p.GetRenderFrame()==&f?1:0;}catch(const std::runtime_error&){return -1;}});
  {std::lock_guard lock(p.queue_mutex);p.present_queue.push_back(&f);}
  auto drain=std::async(std::launch::async,[&]{p.WaitPresent();});
  p.start();
  assert(get.wait_for(std::chrono::seconds(2))==std::future_status::ready);
  assert(get.get()==(fail?-1:1));
  assert(drain.wait_for(std::chrono::seconds(2))==std::future_status::ready);drain.get();
  p.present_thread.request_stop();p.present_thread.join();
  assert(bool(Eden::TakeGpuFailure())==fail);
 }
}
'''.replace('ASSIGNMENT',assignment).replace('METHODS',methods)
with tempfile.TemporaryDirectory() as temp:
    c=Path(temp)/'test.cpp';exe=Path(temp)/'test';c.write_text(code)
    subprocess.run(['c++','-std=c++20','-pthread','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'headless'),str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=15)
print('Generated presentation worker: success/failure, blocked frame/drain waiters, mailbox and join PASS')
swapchain = (cache/'native-local/headless/vulkan_swapchain.cpp').read_text()
errors = []
for marker in ('vkAcquireNextImageKHR returned {}', 'Failed to present with error {}'):
    start = swapchain.rfind('    default:', 0, swapchain.index(marker))
    end = swapchain.index('        break;', start) + len('        break;')
    errors.append(swapchain[start:end])
code = r'''
#include <cassert>
#include <stdexcept>
#define LOG_ERROR(...) ((void)0)
#define LOG_CRITICAL(...) ((void)0)
namespace vk { void Check(int result) { if (result != 0) throw std::runtime_error("Vulkan failure"); } }
int main() {
  for (int result : {-1, -2, -4, -1000000000}) {
    for (auto operation : {+[](int result) { switch (result) { ACQUIRE } },
                           +[](int result) { switch (result) { PRESENT } }}) {
      bool failed = false;
      try { operation(result); } catch (const std::runtime_error&) { failed = true; }
      assert(failed);
    }
  }
}
'''.replace('ACQUIRE', errors[0]).replace('PRESENT', errors[1])
with tempfile.TemporaryDirectory() as temp:
    path = Path(temp); (path/'check.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-fsanitize=address,undefined',
                    str(path/'check.cpp'), '-o', str(path/'check')], check=True)
    subprocess.run([str(path/'check')], check=True)
print('Actual swapchain acquire/present fatal-error arms propagate to recovery PASS')
