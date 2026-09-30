#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""C79 offline prototype: exercise actual legacy counter code and iterative replacement.

Does not modify Eden or deploy anything. Pass the upstream query_cache.h path.
"""
import pathlib
import subprocess
import sys
import tempfile

source = pathlib.Path(sys.argv[1]).read_text()
start = source.index('template <class QueryCache, class HostCounter>\nclass HostCounterBase')
end = source.index('\ntemplate <class HostCounter>\nclass CachedQueryBase', start)
original = source[start:end]
candidate = original
a = candidate.index('        // Avoid nesting')
b = candidate.index('    /// Returns the current value', a)
candidate = candidate[:a] + '''    }
    virtual ~HostCounterBase() {
        // Release only uniquely owned tails; shared branches retain their dependency.
        while (dependency && dependency.use_count() == 1) {
            auto tail = std::move(dependency->dependency);
            dependency.reset();
            dependency = std::move(tail);
        }
    }

''' + candidate[b:]
a = candidate.index('        u64 value = BlockingQuery')
b = candidate.index('\n    /// Returns true', a)
candidate = candidate[:a] + '''        std::vector<HostCounterBase*> chain;
        auto* node = this;
        while (node && !node->result) {
            chain.push_back(node);
            node = node->dependency.get();
        }
        u64 sum = node ? *node->result : 0;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            auto* item = *it;
            // Commit each successfully read prefix before releasing ownership.
            sum += item->BlockingQuery(item == this ? async : false);
            item->result = sum;
            item->dependency.reset();
        }
        return *result;
    }
''' + candidate[b:]
candidate = candidate.replace('    u64 base_result = 0;                     ///< Equivalent to nested dependencies value.\n', '')
test = r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include <stdexcept>
#include <iostream>
using u64 = uint64_t;
COUNTER
struct Counter : HostCounterBase<int, Counter> {
    static inline int reads=0, live=0, fail=-1;
    int id;
    Counter(std::shared_ptr<Counter> dep, int n) : HostCounterBase(std::move(dep)), id(n) { ++live; }
    ~Counter() override { --live; }
    u64 BlockingQuery(bool) const override {
        if (id==fail) throw std::runtime_error("read failure");
        ++reads; return 1;
    }
};
auto make_chain(int count) {
    std::shared_ptr<Counter> head;
    for(int i=0;i<count;++i) head=std::make_shared<Counter>(head,i);
    return head;
}
int main() {
    { auto c=make_chain(10000);
      std::cout << "construction reads=" << Counter::reads << " live=" << Counter::live << '\n';
      if (ITERATIVE) assert(Counter::reads==0);
      else assert(Counter::reads>0);
      assert(c->Query()==10000); int calls=Counter::reads;
      assert(c->Query(true)==10000 && Counter::reads==calls);
    } assert(Counter::live==0);
    { auto root=make_chain(12); auto a=std::make_shared<Counter>(root,20);
      auto b=std::make_shared<Counter>(root,21); a.reset();
      assert(root->Query()==12 && b->Query()==13);
    } assert(Counter::live==0);
    { auto root=make_chain(12); auto a=std::make_shared<Counter>(root,20);
      Counter::fail=6; bool caught=false;
      try { a->Query(); } catch (const std::runtime_error&) { caught=true; }
      assert(caught); Counter::fail=-1;
      assert(a->Query()==13 && root->Query()==12);
    } assert(Counter::live==0);
    if (ITERATIVE) { auto c=make_chain(100000); int calls=Counter::reads;
      c.reset(); assert(Counter::reads==calls); }
    assert(Counter::live==0);
    std::cout << "exact sums/cache/shared branches/retry/destruction PASS\n";
}
'''
with tempfile.TemporaryDirectory(prefix='eden-query-chain-') as directory:
    work = pathlib.Path(directory)
    for name, counter in [('original', original), ('iterative', candidate)]:
        cpp = work / (name + '.cpp')
        cpp.write_text(test.replace('COUNTER', counter).replace('ITERATIVE', str(int(name == 'iterative'))))
        exe = work / name
        subprocess.run(['g++', '-std=c++20', '-O1', '-g', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', str(cpp), '-o', str(exe)], check=True)
        print(name, flush=True)
        subprocess.run([str(exe)], check=True, timeout=30)
