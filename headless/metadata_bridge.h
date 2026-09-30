#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EDEN_METADATA_TITLE = 1,
    EDEN_METADATA_COVER = 2,
};

// Cached for this process; restart after replacing setup files. Empty means ready.
const char* eden_startup_error(void);
uint64_t eden_game_title_id(const char* rom_path);

int eden_extract_game_metadata(const char* rom_path, const char* keys_dir,
                               const char* cover_tga_path, char* title,
                               size_t title_capacity);

#ifdef __cplusplus
}
#endif
