    // Images the GPU wrote are copied back to guest memory before they go, behind a wait for the
    // GPU. Eden evicts them from its "expected" mark on; here they stay, as they do below that
    // mark, until memory is really short (performance.h, KeepDirtyTextures).
    const auto DirtyEvictions = [&] {
        return high_priority_mode &&
               (total_used_memory >= critical_memory || !::Eden::Performance::KeepDirtyTextures());
    };
    // One batch of copies behind a single wait for the GPU: this many images and bytes at most.
    // A pass frees up to 40 images at a time (num_iterations), and at a resolution above 1x most
    // of what the GPU wrote is a few MiB each.
    static constexpr size_t kBatchImages = 64;
    static constexpr size_t kBatchBytes = 64_MiB;
    using DownloadMap = decltype(runtime.DownloadStagingBuffer(size_t{}));
    boost::container::small_vector<std::pair<ImageId, DownloadMap>, 16> gc_downloads;
    const auto ReleaseDownloads = [&] {
        if constexpr (std::is_same_v<Runtime, Vulkan::TextureCacheRuntime>) {
            for (auto& [id, map] : gc_downloads)
                runtime.FreeDeferredStagingBuffer(map);
            gc_downloads.clear();
        }
    };
    SCOPE_EXIT { ReleaseDownloads(); };
    const auto PrefetchDownloads = [&] {
        if constexpr (std::is_same_v<Runtime, Vulkan::TextureCacheRuntime>) {
            ReleaseDownloads();
            size_t remaining = num_iterations;
            size_t bytes = 0;
            u64 usage = total_used_memory;
            // Only look ahead through independent dirty evictions. Leave all
            // deletion, alias updates and pressure-policy decisions to Cleanup.
            // ForEachItemBelow stops when the callback returns true (common/lru_cache.h).
            lru_cache.ForEachItemBelow(frame_tick - ticks_to_destroy, [&](ImageId id) {
                if (remaining == 0) return true;
                --remaining;
                auto& image = slot_images[id];
                if (True(image.flags & ImageFlagBits::IsDecoding)) return false;
                const bool dirty = IsDownloadable(image) && False(image.flags & ImageFlagBits::BadOverlap);
                if ((!aggressive_mode && True(image.flags & ImageFlagBits::CostlyLoad)) ||
                    (!DirtyEvictions() && dirty)) return false;
                // A clean image has nothing to copy back, and an image that does not fit the
                // remaining byte budget keeps the drain it already had in Cleanup. Neither may
                // end the lookahead: stopping here left every later dirty image to Cleanup's
                // per-image drain, one GPU pipeline wait each, where this loop would have spent
                // a single wait on all of them. Skip them and keep looking.
                if (!dirty || image.unswizzled_size_bytes > kBatchBytes - bytes) return false;
                // Aliased, overlapping and MSAA images stay with Cleanup: their copies are not
                // independent of the state that eviction updates. A scaled image is copied here
                // as Cleanup copies it, by the same call (it is scaled down for the copy): at a
                // resolution above 1x nearly every image the GPU wrote is scaled, and leaving
                // them out left a pass of sixty images with sixty waits, half a second in which
                // no frame was shown.
                if (!image.aliased_images.empty() || !image.overlapping_images.empty() ||
                    image.info.num_samples != 1 || gc_downloads.size() == kBatchImages)
                    return true;
                auto map = runtime.DownloadStagingBuffer(image.unswizzled_size_bytes, true);
                // Pin until CPU publication, even if later recording submits work.
                gc_downloads.emplace_back(id, map);
                const auto copies = FixSmallVectorADL(FullDownloadCopies(image.info));
                image.DownloadMemory(map, copies);
                bytes += image.unswizzled_size_bytes;
                usage -= ReclaimedBytes(image);
                // The original policy changes at these thresholds. Stop looking
                // ahead here so we never copy a texture that it would retain.
                return usage < (aggressive_mode || ::Eden::Performance::KeepDirtyTextures() ? critical_memory
                                                                                           : expected_memory);
            });
            if (!gc_downloads.empty()) {
                // One drain for the whole batch; the image count is the reported payload.
                const ::Eden::Performance::Timer drain(::Eden::Performance::gc_batched_drain,
                                                       gc_downloads.size());
                runtime.Finish();
            }
        }
    };
    const auto UsePrefetched = [&](ImageId id, Image& image) {
        if constexpr (std::is_same_v<Runtime, Vulkan::TextureCacheRuntime>) {
            const auto it = std::ranges::find_if(gc_downloads, [id](const auto& entry) {
                return entry.first == id;
            });
            if (it != gc_downloads.end()) {
                const auto copies = FixSmallVectorADL(FullDownloadCopies(image.info));
                SwizzleImage(*gpu_memory, image.gpu_addr, image.info, copies,
                             it->second.mapped_span, swizzle_data_buffer);
                runtime.FreeDeferredStagingBuffer(it->second);
                gc_downloads.erase(it);
                return true;
            }
        }
        return false;
    };
    // The copy of an image Cleanup is about to free: from the batch, and when the batch is used
    // up and this image came after it (the batch was full, or the lookahead had stopped), from a
    // new batch that starts here. Without that every image after the first batch waited alone.
    const auto TakePrefetched = [&](ImageId id, Image& image) {
        if (UsePrefetched(id, image)) return true;
        if constexpr (std::is_same_v<Runtime, Vulkan::TextureCacheRuntime>) {
            if (!gc_downloads.empty()) return false;
            PrefetchDownloads();
            return UsePrefetched(id, image);
        }
        return false;
    };
