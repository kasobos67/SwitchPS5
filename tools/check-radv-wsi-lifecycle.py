#!/usr/bin/env python3
"""Run the patched WSI ownership and worker code with a mocked display/kernel."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root.parent/'mihawk-vulkan-review/.deps/work/radv-src/src/vulkan/wsi/wsi_common_videoout.c').read_text()
def function(name):
    start = source.index('\n'+name+'(')
    start = source.rfind('static ', 0, start)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

structs = source[source.index('struct wsi_videoout_swapchain;'):source.index('static once_flag videoout_once')]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#define VIDEOOUT_BUFFERS 5
#define VIDEOOUT_BUFFER_BYTES 4096
#define PS5_VIDEO_OUT_FLIP_VSYNC 1
#define PS5_VIDEO_OUT_MODE_RESTORE 1u
#define VK_TRUE 1
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef uint64_t VkFence;
struct wsi_device { int (*WaitForFences)(int, int, const VkFence*, int, uint64_t); };
struct wsi_videoout_swapchain {
  struct { const struct wsi_device *wsi; int device; } base;
  unsigned in_flight;
};
static int close_error, closes, unmaps, releases, flips, unmap_error, restores, closes_at_restore;
static int sceVideoOutClose(int handle) { assert(handle == 7); closes++; return close_error; }
static int sceVideoOutConfigureOutput(int handle, unsigned mode, const void *a, const void *b, const void *c) {
  assert(handle == 7 && mode == PS5_VIDEO_OUT_MODE_RESTORE);
  restores++; closes_at_restore = closes; return 0;
}
static int sceKernelMunmap(void *p, size_t n) {
  assert(closes > 0 && !close_error && n == VIDEOOUT_BUFFERS * VIDEOOUT_BUFFER_BYTES);
  unmaps++; if (!unmap_error) free(p); return unmap_error;
}
static int sceKernelReleaseDirectMemory(int64_t p, size_t n) {
  assert(p == 123 && !unmap_error); releases++; return 0;
}
static int sceVideoOutSubmitFlip(int h, int b, int m, int64_t a) { flips++; return 0; }
static int fences(int d, int n, const VkFence *f, int all, uint64_t t) { return 0; }
'''
harness += structs + '\n' + function('videoout_flip_thread') + '\n'
harness += (root/'tools/radv-videoout-lifecycle.inc').read_text()
harness += r'''
static void opened(void) {
  struct wsi_videoout_output *o = &videoout_output;
  o->handle = 7; o->opened = true; o->physical = 123;
  o->buffers = malloc(VIDEOOUT_BUFFERS * VIDEOOUT_BUFFER_BYTES);
  assert(o->buffers); o->registered = true;
}
int main(void) {
  struct wsi_videoout_output *o = &videoout_output;
  assert(mtx_init(&o->lock, mtx_plain) == thrd_success);
  assert(cnd_init(&o->changed) == thrd_success);
  // No surface/open, then a failed open: both must allow a later retry.
  videoout_ref(); videoout_unref(); assert(!o->opened && closes == 0);
  videoout_ref(); o->opened = true; videoout_unref(); assert(!o->opened);
  // Shared WSI owners do not close each other's display.
  videoout_ref(); videoout_ref(); opened();
  videoout_unref(); assert(closes == 0 && o->buffers);
  videoout_unref(); assert(closes == 1 && unmaps == 1 && releases == 1 && !o->buffers);
  // Registration failure still owns its allocation until successful close.
  videoout_ref(); opened(); o->registered = false;
  videoout_unref(); assert(unmaps == 2 && releases == 2 && !o->registered);
  // Failed close must retain all display-owned memory, and allow later retry.
  videoout_ref(); opened(); close_error = -1; void *saved = o->buffers;
  videoout_unref(); assert(o->buffers == saved && unmaps == 2 && o->handle == 7);
  close_error = 0; videoout_ref(); videoout_unref(); assert(unmaps == 3 && !o->buffers);
  // Actual worker drains a queued frame, then can be joined while idle.
  videoout_ref(); opened();
  const struct wsi_device device = {fences};
  struct wsi_videoout_swapchain chain = {{&device, 1}, 1};
  o->queue[0] = (struct wsi_videoout_present){&chain, 0, 1}; o->queue_count = 1;
  assert(thrd_create(&o->thread, videoout_flip_thread, NULL) == thrd_success);
  o->thread_started = true;
  mtx_lock(&o->lock);
  while (chain.in_flight) cnd_wait(&o->changed, &o->lock);
  mtx_unlock(&o->lock);
  videoout_unref(); assert(flips == 1 && !o->thread_started && !o->stop && !o->closing);
  // A new owner can create and close a fresh output after the worker joined.
  videoout_ref(); opened(); videoout_unref(); assert(unmaps == 5 && releases == 5);
  // Only a session at 119.88 Hz restores 59.94 Hz, once, before its close.
  assert(restores == 0);
  videoout_ref(); opened(); o->high_frame_rate = true; const int closed_before = closes;
  videoout_unref();
  assert(restores == 1 && closes_at_restore == closed_before && closes == closed_before + 1 &&
         !o->high_frame_rate && unmaps == 6);
  videoout_ref(); opened(); videoout_unref(); assert(restores == 1);
  cnd_destroy(&o->changed); mtx_destroy(&o->lock);
  puts("RADV WSI owners, partial init, close failure, worker drain/join, reopen and 59.94 Hz restore PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='eden-radv-wsi-') as directory:
    path = Path(directory)
    (path/'test.c').write_text(harness)
    subprocess.run(['cc', '-std=c11', '-D__PROSPERO__', '-g', '-O1', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(path/'test.c'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True, timeout=20)
