#!/usr/bin/env python3
"""Exercise the actual derived slab helpers' host-object lifetime contract."""
from pathlib import Path
import subprocess
import resource
import sys
import tempfile

source = Path(sys.argv[1]).read_text()
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
with tempfile.TemporaryDirectory(prefix='eden-slab-lifetime-') as temp:
    folder = Path(temp)
    headers = folder / 'core/hle/kernel'
    headers.mkdir(parents=True)
    for name in ('k_auto_object.h', 'k_auto_object_container.h', 'kernel.h'):
        (headers / name).write_text('')
    (headers / 'slab_helpers.h').write_text(source)
    (folder / 'check.cpp').write_text(r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <type_traits>
namespace Kernel {
class KernelCore;
struct KAutoObject {
    explicit KAutoObject(KernelCore&) {}
    virtual ~KAutoObject() = default;
    virtual void Destroy(KernelCore&) = 0;
    virtual void Finalize(KernelCore&) = 0;
    static void Create(KAutoObject*) {}
};
struct KAutoObjectWithList : KAutoObject { using KAutoObject::KAutoObject; };
template<class T> struct Heap { void Free(T* p) { delete p; } };
struct Container {
    void Initialize() {}
    template<class T> void Unregister(T*) {}
    template<class T> void Register(T*) {}
};
class KernelCore {
public:
    template<class T> Heap<T>& SlabHeap() { static Heap<T> heap; return heap; }
    Container& ObjectListContainer() { static Container list; return list; }
};
template<class T> Heap<T>& SlabHeap(KernelCore& k) { return k.SlabHeap<T>(); }
}
#include "core/hle/kernel/slab_helpers.h"
struct State { bool live = true, finalized = false, post = false; };
template<template<class, class> class Helper, class Base>
struct Object : Helper<Object<Helper, Base>, Base> {
    State& state;
    bool initialized;
    Object(Kernel::KernelCore& k, State& s, bool init)
        : Helper<Object, Base>(k), state(s), initialized(init) {}
    ~Object() override { assert(state.post == initialized); state.live = false; }
    bool IsInitialized() const override { return initialized; }
    uintptr_t GetPostDestroyArgument() const override {
        return reinterpret_cast<uintptr_t>(&state);
    }
    void Finalize(Kernel::KernelCore&) override { state.finalized = true; }
    static void PostDestroy(Kernel::KernelCore&, uintptr_t arg) {
        auto& s = *reinterpret_cast<State*>(arg);
        assert(s.live && s.finalized); s.post = true;
    }
};
template<class T> void check(Kernel::KernelCore& kernel) {
    for (bool initialized : {false, true}) {
        State state;
        (new T(kernel, state, initialized))->Destroy(kernel);
        assert(!state.live && state.finalized == initialized && state.post == initialized);
    }
}
int main() {
    Kernel::KernelCore kernel;
    check<Object<Kernel::KAutoObjectWithSlabHeap, Kernel::KAutoObject>>(kernel);
    check<Object<Kernel::KAutoObjectWithSlabHeapAndContainer, Kernel::KAutoObjectWithList>>(kernel);
}
''')
    binary = folder / 'check'
    subprocess.run(['clang++-18', '-std=c++20', '-I', str(folder),
                    str(folder / 'check.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    if len(sys.argv) == 3:
        (headers / 'slab_helpers.h').write_text(Path(sys.argv[2]).read_text())
        subprocess.run(['clang++-18', '-std=c++20', '-I', str(folder),
                        str(folder / 'check.cpp'), '-o', str(binary)], check=True)
        old = subprocess.run([str(binary)], capture_output=True, text=True)
        assert old.returncode == -6 and 'state.post == initialized' in old.stderr, old
        print('Original cleanup order rejected by lifetime check PASS')
print('Both slab helpers preserve host lifetime through cleanup PASS')
