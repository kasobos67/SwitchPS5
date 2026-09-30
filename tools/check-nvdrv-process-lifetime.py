"""Execute generated session ownership paths; guest heap discovery is outside this check."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];cache=Path((r/'.local/headless-cache').read_text().strip())
s=(cache/'native-local/headless/nvdrv_container.cpp').read_text()
opening=s[s.index('SessionId Container::OpenSession'):s.index('    // Optimization')]+ '    return SessionId{new_id};\n}\n'
closing=s[s.index('void Container::CloseSession'):s.index('Session* Container::GetSession')]
code=r'''
#include <cassert>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
using DAddr=unsigned long;using VAddr=unsigned long;
namespace Common::Literals {}
namespace Core {struct Asid {size_t id;};}
namespace Kernel {struct KProcess {int refs=1,memory=7; bool registered=false; int& GetMemory(){assert(refs);return memory;} void Open(int){assert(refs);++refs;} void Close(int){assert(refs);if(!--refs){assert(!registered);memory=0;}}};}
struct SessionId {size_t id;};
struct Mapper {DAddr GetRegionStart(){return 4096;}size_t GetRegionSize(){return 4096;}};
struct Session {SessionId id;Kernel::KProcess* process;Core::Asid asid;bool has_preallocated_area{};std::unique_ptr<Mapper> mapper;bool is_active{};int ref_count{};Session(SessionId i,Kernel::KProcess*p,Core::Asid a):id(i),process(p),asid(a){}};
struct Smmu {std::vector<Kernel::KProcess*> owners;Core::Asid RegisterProcess(int*p){for(size_t i=0;i<owners.size();++i)if(&owners[i]->memory==p){owners[i]->registered=true;return {i};}assert(false);return{};}void UnregisterProcess(Core::Asid a){assert(owners[a.id]->refs);owners[a.id]->registered=false;}void Free(DAddr,size_t){}};
struct SystemType {int Kernel(){return 0;}};
struct Host {Smmu smmu;SystemType sys;Smmu& MemoryManager(){return smmu;}SystemType& System(){return sys;}};
struct Impl {std::mutex session_guard;std::deque<Session> sessions;std::deque<size_t> id_pool;size_t new_ids=0;Host host1x;struct File{Impl*p;void UnmapAllHandles(SessionId id){assert(p->sessions[id.id].process->refs>0);assert(p->sessions[id.id].process->memory==7);}} file{this};};
struct Container {Impl storage;Impl*impl=&storage;SessionId OpenSession(Kernel::KProcess*);void CloseSession(SessionId);};
'''+opening+closing+r'''
int main(){
 Container c;Kernel::KProcess p,q;c.impl->host1x.smmu.owners={&p,&q};
 auto a=c.OpenSession(&p),b=c.OpenSession(&p);assert(a.id==b.id && p.refs==2);
 p.Close(0);assert(p.refs==1);c.CloseSession(a);assert(p.refs==1 && p.registered);
 c.CloseSession(b);assert(p.refs==0 && !p.registered);
 auto d=c.OpenSession(&q);assert(d.id==a.id && q.refs==2);
 c.impl->sessions[d.id].mapper=std::make_unique<Mapper>();c.impl->sessions[d.id].has_preallocated_area=true;
 c.CloseSession(d);assert(q.refs==1 && !q.registered);q.Close(0);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++-18','-std=c++20','-fsanitize=address,undefined','-g',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Generated nvdrv session ownership: shared sessions, dead external owner, final unmap before release, ID reuse PASS')