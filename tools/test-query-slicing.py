#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare actual legacy query classes with PS5-only no-slicing prototype.

Usage: test-query-slicing.py <upstream source directory>. No production edits.
GPU reads are simulated; this cannot establish driver correctness or FPS.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(sys.argv[1])
header = (root / 'src/video_core/query_cache.h').read_text()
stream = header[header.index('template <class QueryCache, class HostCounter>\nclass CounterStreamBase'):header.index('template <class QueryCache, class CachedQuery,')]
counter = header[header.index('template <class QueryCache, class HostCounter>\nclass HostCounterBase'):header.rindex('} // namespace VideoCommon')]
cpp = (root / 'src/video_core/renderer_opengl/gl_query_cache.cpp').read_text()
a = cpp.index('u64 CachedQuery::Flush(')
flush = cpp[a:cpp.index('\n}', a) + 2]
anchor = 'const bool slice_counter = WaitPending() && stream.IsEnabled();'
assert flush.count(anchor) == 1
fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <iostream>
using u64=uint64_t; using u8=uint8_t; using u32=uint32_t;
using VAddr=uint64_t; using AsyncJobId=unsigned;
namespace VideoCore { enum class QueryType { SamplesPassed }; }
namespace VideoCommon {
CLASSES
}
struct QueryCache;
struct HostCounter : VideoCommon::HostCounterBase<QueryCache,HostCounter> {
 static inline unsigned created=0, reads=0, ends=0, live=0;
 u64 value=0;
 bool active=true;
 HostCounter(std::shared_ptr<HostCounter> d):HostCounterBase(std::move(d)) {
  ++created; ++live;
 }
 ~HostCounter() { --live; }
 void EndQuery() { assert(active); active=false; ++ends; }
 u64 BlockingQuery(bool) const override { assert(!active); ++reads; return value; }
};
struct QueryCache {
 using StreamType=VideoCommon::CounterStreamBase<QueryCache,HostCounter>;
 StreamType stream{*this,VideoCore::QueryType::SamplesPassed};
 std::weak_ptr<HostCounter> active;
 auto Counter(std::shared_ptr<HostCounter> d, VideoCore::QueryType) {
  auto c=std::make_shared<HostCounter>(std::move(d)); active=c; return c;
 }
 auto& Stream(VideoCore::QueryType) { return stream; }
 void Draw(u64 count) { auto c=active.lock(); assert(c && c->active); c->value+=count; }
};
struct CachedQuery : VideoCommon::CachedQueryBase<HostCounter> {
 QueryCache* cache;
 VideoCore::QueryType type=VideoCore::QueryType::SamplesPassed;
 CachedQuery(QueryCache& c,u8* p):CachedQueryBase(0,p),cache(&c) {}
 u64 Flush(bool async=false) override;
};
FLUSH
int main() {
 {
  QueryCache cache;
  u64 memory[2]={999,999};
  CachedQuery query(cache,reinterpret_cast<u8*>(memory));
  cache.stream.Enable(); cache.Draw(5);
  query.BindCounter(cache.stream.Current(),123);
  assert(query.Flush()==5 && memory[0]==5 && memory[1]==123);
  unsigned before=HostCounter::created;
  for(unsigned i=0;i<1000;++i) {
   cache.Draw(1); memory[0]=999; memory[1]=999;
   assert(query.Flush()==5 && memory[0]==5 && memory[1]==123);
  }
  unsigned restarts=HostCounter::created-before;
  std::cout << "cached-read restarts=" << restarts
            << " backend reads=" << HostCounter::reads << '\n';
  assert(restarts==(CANDIDATE?0:1000));
  auto total=cache.stream.Current();
  assert(total->Query()==1005);
  // Rebinding flushes the old query, preserves timestamp and cumulative value.
  assert(query.BindCounter(total,456)==5);
  assert(query.Flush()==1005 && memory[0]==1005 && memory[1]==456);
  cache.stream.Reset(); cache.Draw(7);
  query.BindCounter(cache.stream.Current(),std::nullopt);
  assert(query.Flush()==7 && memory[0]==7 && memory[1]==456);
  cache.stream.Disable();
  before=HostCounter::created;
  assert(query.Flush()==7 && HostCounter::created==before);
  query.BindCounter(nullptr,std::nullopt);
  assert(query.Flush()==0 && memory[0]==0);
 }
 assert(HostCounter::live==0);
 std::cout << "PASS exact values/timestamps/cache/rebind/reset/disabled/null/lifetime\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
    for name, method in [('original', flush), ('ps5-prototype', flush.replace(anchor, 'const bool slice_counter = false;'))]:
        path = Path(directory) / (name + '.cpp')
        path.write_text(fixture.replace('CLASSES', stream + counter).replace('FLUSH', method).replace('CANDIDATE', str(int(name != 'original'))))
        exe = path.with_suffix('')
        subprocess.run(['clang++-18', '-std=c++20', '-O1', '-g', '-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all', str(path), '-o', str(exe)], check=True)
        print(name, flush=True)
        subprocess.run([str(exe)], check=True, timeout=30)
