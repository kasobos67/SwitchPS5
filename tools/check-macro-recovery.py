"""Exercise the transformed native callback and C++ rethrow boundary."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    derived = tmp / 'macro.cpp'
    subprocess.run(['cmake', '-DMACRO_INPUT=' + sys.argv[1],
                    '-DMACRO_OUTPUT=' + str(derived), '-P',
                    str(root / 'headless/macro_recovery.cmake')], check=True)
    source = derived.read_text()
    thunk = source[source.index('static void MacroJIT_SendThunk('):
                   source.index('void MacroJITx64Impl::Compile_Send(')]
    assert 'cmp(byte[STATE + offsetof(JITState, failed)], 0);\n    jne(end_of_code, T_NEAR);' in source
    assert 'if (state.failure) std::rethrow_exception(state.failure);' in source
    harness = r'''
#include <cassert>
#include <exception>
#include <stdexcept>
using u32 = unsigned;
namespace Core { struct System {}; }
namespace Macro { struct MethodAddress {u32 address;}; }
namespace Engines { struct Maxwell3D {
 bool fail{}; unsigned calls{};
 void CallMethod(Core::System&, u32 address, u32 value, bool last) {
  ++calls; assert(address==7 && value==19 && last);
  if(fail) throw std::runtime_error("image creation rejected");
 }
}; }
struct MacroJITx64Impl { struct JITState {
 Engines::Maxwell3D* maxwell3d; Core::System* system;
 std::exception_ptr failure; bool failed{};
}; };
THUNK
int main() {
 Core::System system; Engines::Maxwell3D engine;
 MacroJITx64Impl::JITState state{&engine,&system,{}};
 MacroJIT_SendThunk(&state,{7},19);
 assert(engine.calls==1 && !state.failed && !state.failure);
 engine.fail=true;
 MacroJIT_SendThunk(&state,{7},19); // Must return, never unwind through JIT.
 assert(engine.calls==2 && state.failed && state.failure);
 bool caught=false;
 try { std::rethrow_exception(state.failure); }
 catch(const std::runtime_error& e) {caught=std::string(e.what())=="image creation rejected";}
 assert(caught);
}
'''.replace('THUNK', thunk)
    (tmp / 'check.cpp').write_text(harness)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(tmp / 'check.cpp'),
                    '-o', str(tmp / 'check')], check=True)
    subprocess.run([str(tmp / 'check')], check=True)
print('Macro callback exception containment: ASan/UBSan PASS; native JIT gate still required')
