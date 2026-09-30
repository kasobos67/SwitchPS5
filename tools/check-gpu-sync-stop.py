from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];c=Path((r/'.local/headless-cache').read_text().strip())
t=(c/'native-local/headless/gpu.cpp').read_text()
def method(start,end): return t[t.index(start):t.index(end,t.index(start))]
methods=method('    template <typename Func>','    [[nodiscard]] u64 GetTicks()')+method('    void NotifyShutdown()', '    /// Obtain the CPU Context')
code=r'''
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <list>
#include <mutex>
#include <semaphore>
#include <thread>
using u64=unsigned long long;
struct Worker {std::thread t;std::atomic_bool stopping{};void Stop(){stopping=true;if(t.joinable())t.join();}void TickGPU(bool){}};
struct GPU {
 std::mutex sync_request_mutex,sync_mutex;
 std::condition_variable sync_request_cv,sync_cv;
 std::list<std::function<void()>> sync_requests;
 bool sync_requests_stopped{},is_async=true;
 std::atomic_bool shutting_down{};
 std::atomic<u64> current_sync_fence{};u64 last_sync_fence{};
 Worker gpu_thread;
'''+methods+r'''
};
int main(){
 GPU g;int value=0;
 auto f=g.RequestSyncOperation([&]{value=42;});g.TickWork();g.WaitForSyncOperation(f);assert(value==42);
 // Abandoned requests must not hold a caller forever or execute after it returns.
 std::binary_semaphore entered{0},release{0};std::atomic_bool returned=false;
 g.gpu_thread.t=std::thread([&]{entered.release();release.acquire();});entered.acquire();
 f=g.RequestSyncOperation([&]{assert(false);});
 std::thread waiter([&]{g.WaitForSyncOperation(f);returned=true;});
 std::thread stop([&]{g.NotifyShutdown();});
 while(!g.gpu_thread.stopping)std::this_thread::yield();
 std::this_thread::sleep_for(std::chrono::milliseconds(20));assert(!returned);
 release.release();stop.join();waiter.join();assert(returned && g.sync_requests.empty());
 assert(g.CurrentSyncRequestFence()==1); // Cancellation never invents completed work.
 auto later=g.RequestSyncOperation([&]{assert(false);});g.WaitForSyncOperation(later);assert(g.sync_requests.empty());
 g.NotifyShutdown(); // Idempotent destruction path.
 // A callback already running must finish before its borrowed stack can disappear.
 GPU running;std::binary_semaphore active{0},finish{0};returned=false;
 f=running.RequestSyncOperation([&]{active.release();finish.acquire();value=99;});
 running.gpu_thread.t=std::thread([&]{running.TickWork();});active.acquire();
 std::thread w([&]{running.WaitForSyncOperation(f);assert(value==99);returned=true;});
 std::thread s([&]{running.NotifyShutdown();});
 while(!running.gpu_thread.stopping)std::this_thread::yield();
 assert(!returned);finish.release();s.join();w.join();assert(returned);
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++20','-pthread','-fsanitize=address,undefined','-g',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,timeout=10)
print('Actual GPU sync methods: cancelled/pending/active requests, callback lifetime, repeated shutdown PASS')# Execute the actual in-flight wait code with absent signals and shutdown.
dma=(c/'native-local/headless/dma_pusher.cpp').read_text()
puller=(c/'native-local/headless/engines_puller.cpp').read_text()
wait=dma.split('    if (signal_sync && !synced) {',1)[1].split('        signal_sync = false;',1)[0]
acquire=puller.split('void Puller::ProcessSemaphoreAcquire(DmaPusher& dma_pusher) {',1)[1].split('\n}\n',1)[0]
code=r'''
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <thread>
using u32=unsigned;
struct System{std::atomic_bool stop{};bool IsShuttingDown(){return stop;}};
struct DMA {System system;std::mutex sync_mutex;std::condition_variable sync_cv;bool synced{};
bool Wait(){
'''+wait+r'''
 return true;}
};
struct Memory {std::atomic<u32> value{};template<class T>T Read(unsigned){return value.load();}};
struct Raster {std::atomic_uint calls{};void ReleaseFences(){++calls;}};
struct DmaPusher {System system;Memory memory_manager;Raster raster;Raster*rasterizer=&raster;};
struct Puller {struct Regs {struct Address{unsigned SemaphoreAddress(){return 0;}}semaphore_address;u32 semaphore_acquire=4,acquire_value{};bool acquire_active{},acquire_mode{},acquire_source{};}regs;
void ProcessSemaphoreAcquire(DmaPusher&dma_pusher){
'''+acquire+r'''
}};
int main(){
 DMA d;std::thread waiter([&]{assert(!d.Wait());});d.system.stop=true;waiter.join();
 DMA normal;normal.synced=true;assert(normal.Wait());
 DmaPusher p;Puller pull;std::thread worker([&]{pull.ProcessSemaphoreAcquire(p);});
 while(!p.raster.calls)std::this_thread::yield();p.system.stop=true;worker.join();
 p.system.stop=false;p.memory_manager.value=4;pull.ProcessSemaphoreAcquire(p);
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'wait.cpp').write_text(code)
 subprocess.run(['g++','-std=c++20','-pthread','-fsanitize=address,undefined','-g',str(p/'wait.cpp'),'-o',str(p/'wait')],check=True)
 subprocess.run([str(p/'wait')],check=True,timeout=10)
print('Actual DMA fence and semaphore waits: normal completion and unsignalled shutdown PASS')
