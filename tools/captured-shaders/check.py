#!/usr/bin/env python3
"""Offline partial compiler boundary: captured GLSL -> Mesa NIR -> PS5 driver/PSBC.

Uses existing local SDK host dependencies read-only. Does not create EGL, execute
GPU code, validate pixels, or replay the complete Mesa state-tracker pipeline.
"""
import json
from pathlib import Path
import subprocess
import shlex
import hashlib
import re
r = Path(__file__).resolve().parents[2]
a = r.parent / 'ps5-opengl-ballot-assist'
p = r.parent.parent / 'publish/ps5-opengl'
b = p / 'build/mesa-host-frontend'
ps = p / 'third_party/opengnm-psbc'
out = r / 'build/captured-shaders'
out.mkdir(exist_ok=True)
MESA=p/'third_party/mesa-26.2.0'; PSBC=ps; BUILD=b
assert 'caps->shader_ballot = PS5_ENABLE_GLSL_460_CANDIDATE;' in (a/'src/gallium/ps5/ps5_screen.c').read_text()
assert 'EXT_CAP(ARB_shader_ballot,                shader_ballot)' in (MESA/'src/mesa/state_tracker/st_extensions.c').read_text()
(out/'summary.json').unlink(missing_ok=True)
serialization=(MESA/'src/compiler/nir/nir_serialize.c').read_text()
for removed in ('   NIR_SERIALIZE_SHADER_SPEC = 1 << 3,\n',
                '   if (!strip && info.spec)\n      flags |= NIR_SERIALIZE_SHADER_SPEC;\n',
                '   if (!strip && info.spec)\n      blob_write_string(blob, info.spec);\n',
                '   char *spec = (flags & NIR_SERIALIZE_SHADER_SPEC) ? blob_read_string(blob) : NULL;\n',
                '   info.spec = spec ? ralloc_strdup(ctx.nir, spec) : NULL;\n'):
    assert serialization.count(removed)==1
    serialization=serialization.replace(removed,'')
assert serialization==(PSBC/'src/compiler/nir/nir_serialize.c').read_text()
for name in ('nir_intrinsics.h','nir_opcodes.h'):
    assert (BUILD/'src/compiler/nir'/name).read_bytes()==(PSBC/'src/compiler/nir'/name).read_bytes()
def tokens(path):
    return re.sub(r'\s+','',re.sub(r'/\*.*?\*/|//[^\n]*','',path.read_text(),flags=re.S))
for name in ('src/compiler/nir/nir_shader_compiler_options.h','src/compiler/shader_info.h'):
    assert tokens(MESA/name)==tokens(PSBC/name)


config=(a/'build/core33-native-runtime/runtime-config.txt').read_text().splitlines()
flags=shlex.split(config[3])
assert '-DPS5_ENABLE_GLSL_460_CANDIDATE=1' in flags
args=['clang-18','-std=gnu11','-O1','-g','-ffunction-sections','-fdata-sections','-DHAVE_FUNC_ATTRIBUTE_PACKED=1','-DHAVE_PTHREAD=1','-DHAVE_STRUCT_TIMESPEC=1','-DHAVE_ENDIAN_H=1','-D_GNU_SOURCE',*flags]
for path in [b/'src',b/'src/compiler',b/'src/compiler/nir',p/'third_party/mesa-26.2.0/src',p/'third_party/mesa-26.2.0/include',p/'third_party/mesa-26.2.0/src/gallium/include',p/'third_party/mesa-26.2.0/src/gallium/auxiliary',a/'src/gallium/ps5',a/'src/platform',ps/'libpsbc',ps/'src',p/'tests/ps5/glsl_handoff']: args+=['-I'+str(path)]
args+=['-c',str(Path(__file__).with_name('backend.c')),'-o',str(out/'ballot-back.o')]
subprocess.run(args,check=True)
# Independently compile the ABI contract against PSBC headers.
subprocess.run(['clang-18','-std=gnu11','-DHAVE_FUNC_ATTRIBUTE_PACKED=1','-DHAVE_ENDIAN_H=1','-DHAVE_PTHREAD=1','-DHAVE_STRUCT_TIMESPEC=1','-D_GNU_SOURCE','-DABI_FUNCTION=psbc_abi','-I'+str(ps/'include/mesa'),'-I'+str(ps/'include'),'-I'+str(ps/'src'),'-c',str(p/'tests/ps5/glsl_handoff/abi.c'),'-o',str(out/'psbc-abi.o')],check=True)
subprocess.run(['g++','-Wl,--gc-sections','-o',str(out/'ballot-back'),str(out/'ballot-back.o'),str(out/'psbc-abi.o'),str(ps/'libpsbc.a'),'-pthread','-lm'],check=True)
subprocess.run([str(out/'ballot-back'),'--contract'],cwd=out,check=True)


root=r; build=b; mesa=MESA; results=[]
commands=json.loads((build/'compile_commands.json').read_text())
def compile_like(suffix, source, dest):
    entry=next(c for c in commands if c['file'].endswith(suffix))
    args=shlex.split(entry['command']); clean=[args[0]]; i=1
    while i<len(args):
        if args[i] in ('-o','-c','-MF','-MQ'):
            i+=2
            continue
        if args[i] not in ('-MD','-MMD'):
            clean.append(args[i])
        i+=1
    clean+=['-I'+str(mesa/'src/compiler/glsl'),'-I'+str(mesa/'src/mesa/state_tracker'),'-I'+str(out),
            '-c',str(source),'-o',str(dest)]
    subprocess.run(clean,cwd=build,check=True)

# Same helper extraction as the SDK glsl_handoff test; rebuild locally rather
# than trust old object files or mutate the SDK owner's build directory.
st=(mesa/'src/mesa/state_tracker/st_glsl_to_nir.cpp').read_text()
(out/'finalize.cpp').write_text(st[:st.index('static bool\ndef_is_64bit(')]+
    st[st.index('void\nst_nir_lower_samplers('):st.index('/**\n * Link a GLSL shader program.')])
statevars=(mesa/'src/mesa/program/prog_statevars.c').read_text()
size_start=statevars.index('unsigned\n_mesa_program_state_value_size(')
size_end=statevars.index('\n}\n',size_start)+3
opt_start=statevars.index('void\n_mesa_optimize_state_parameters(')
(out/'statevars.c').write_text('#include "program/prog_statevars.h"\n#include "program/prog_parameter.h"\n#include "main/mtypes.h"\n'+statevars[size_start:size_end]+statevars[opt_start:])
ac=(ps/'src/amd/common/nir/ac_nir.c').read_text()
ac_start=ac.index('unsigned\nac_nir_varying_expression_max_cost(')
(out/'varying.inc').write_text(ac[ac_start:ac.index('\nbool\n',ac_start)])
(out/'abi.c').write_text('#define ABI_FUNCTION mesa_abi\n'+(p/'tests/ps5/glsl_handoff/abi.c').read_text())
compile_like('standalone.cpp', Path(__file__).with_name('frontend.cpp'), out/'ballot-front.o')
compile_like('standalone.cpp',out/'finalize.cpp',out/'finalize.o')
compile_like('gl_nir_linker.c',out/'statevars.c',out/'statevars.o')
compile_like('gl_nir_linker.c',out/'abi.c',out/'mesa-abi.o')
link=shlex.split(subprocess.check_output(['ninja','-t','commands','src/compiler/glsl/glsl_compiler'],cwd=build,text=True).splitlines()[-1])
link[link.index('-o')+1]=str(out/'ballot-front')
link[next(i for i,a in enumerate(link) if a.endswith('/main.cpp.o'))]=str(out/'ballot-front.o')
link[link.index('-Wl,--start-group'):link.index('-Wl,--start-group')] = [str(out/name) for name in ('finalize.o','statevars.o','mesa-abi.o')]
link += ['-Wl,--gc-sections']
subprocess.run(link,cwd=build,check=True)
inputs=[a/'src/gallium/ps5/ps5_screen.c',a/'src/platform/ps5_agc_package.c',ps/'libpsbc.a',ps/'libpsbc.ps5.a',build/'compile_commands.json']
inputs += [out/name for name in ('finalize.cpp','statevars.c','abi.c','varying.inc')]
input_hashes={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs}
fixtures=sorted((root/'results/headless-fw602-20260919-201509/failed-shaders').glob('fragment-*.glsl'))
assert len(fixtures)==6
for f in fixtures:
    for mode in ('--ballot','--no-ballot'):
        p=subprocess.run([str(out/'ballot-front'),mode,str(f)],text=True,capture_output=True,cwd=out,timeout=60)
        (out/(f.stem+mode+'.log')).write_text(p.stdout+p.stderr)
        print(mode,f.name,'rc='+str(p.returncode),flush=True)
        if mode=='--ballot':
            assert p.returncode==0
            backend=subprocess.run([str(out/'ballot-back'),str(out/'ballot-fragment.nir')],cwd=out,text=True,capture_output=True,timeout=60)
            (out/(f.stem+'-backend.log')).write_text(backend.stdout+backend.stderr)
            assert backend.returncode==0, backend.stdout+backend.stderr
            print(backend.stdout,flush=True)
            result=dict(shader=f.name,sha256=hashlib.sha256(f.read_bytes()).hexdigest(),frontend=True,backend=True)
        else:
            assert p.returncode==1 and 'readInvocationARB' in p.stderr
            result['ballot_disabled_rejected']=True
            results.append(result)


(out/'summary.json').write_text(json.dumps(dict(compiler_only=True,inputs=input_hashes,results=results),indent=2)+'\n')
print('PASS six captured fragments; six ballot-disabled controls rejected')
