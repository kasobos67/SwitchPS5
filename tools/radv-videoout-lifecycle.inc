/* Eden switches between SDL, OpenGL and Vulkan within one process. Keep the
 * output shared between live WSI owners, but release it after the last owner. */
static void
videoout_ref(void)
{
   struct wsi_videoout_output *out = &videoout_output;
   mtx_lock(&out->lock);
   while (out->closing)
      cnd_wait(&out->changed, &out->lock);
   out->owners++;
   mtx_unlock(&out->lock);
}

static void
videoout_unref(void)
{
   struct wsi_videoout_output *out = &videoout_output;
   mtx_lock(&out->lock);
   assert(out->owners > 0);
   if (--out->owners != 0) {
      mtx_unlock(&out->lock);
      return;
   }
   /* Vulkan requires swapchains to be destroyed before their device. Their
    * destruction drains in_flight, so no worker references remain here. */
   assert(out->queue_count == 0 && out->current == NULL);
   out->closing = true;
   out->stop = true;
   cnd_broadcast(&out->changed);
   const bool join = out->thread_started;
   mtx_unlock(&out->lock);
   if (join)
      thrd_join(out->thread, NULL);
   mtx_lock(&out->lock);
   out->thread_started = false;
   out->stop = false;

   const int closed = out->handle >= 0 ? sceVideoOutClose(out->handle) : 0;
   if (closed == 0) {
      /* Successful close ends display ownership, including pending flips.
       * Never release scanout memory when close fails. */
      if (out->buffers) {
         const size_t bytes = VIDEOOUT_BUFFERS * VIDEOOUT_BUFFER_BYTES;
#if defined(__PROSPERO__)
         if (sceKernelMunmap(out->buffers, bytes) == 0) {
            if (sceKernelReleaseDirectMemory(out->physical, bytes) != 0)
               fprintf(stderr, "wsi/videoout: scanout direct-memory release failed\n");
         } else {
            fprintf(stderr, "wsi/videoout: scanout unmap failed; retaining allocation\n");
         }
#else
         munmap(out->buffers, bytes);
#endif
      }
      out->buffers = NULL;
      out->physical = -1;
      out->registered = false;
      out->handle = -1;
      out->opened = false;
      out->high_frame_rate = false;
      out->modes_settled = false;
      out->high_mode_offered = false;
      out->flip_argument = 0;
      out->queue_head = 0;
      memset(out->buffer, 0, sizeof(out->buffer));
   } else {
      fprintf(stderr, "wsi/videoout: close failed: 0x%08x; retaining scanout memory\n", (unsigned)closed);
   }
   out->closing = false;
   cnd_broadcast(&out->changed);
   mtx_unlock(&out->lock);
}
