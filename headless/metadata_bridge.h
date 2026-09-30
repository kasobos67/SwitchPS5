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

// The languages the game declares in its own control data (NACP flags: bit n is NS
// ApplicationLanguage n), or 0 when they cannot be read.
uint32_t eden_game_supported_languages(const char* rom_path, const char* keys_dir);

// Update and DLC files (NSP or XCI, any depth) in updates_dir, read with the provider that also
// applies them to a running game. Replaces the previous scan; eden_game_addons queries it.
void eden_scan_addons(const char* updates_dir, const char* keys_dir);
// For a base game: the newest update's display version (empty without one) and its DLC count.
// Returns nonzero when either exists.
int eden_game_addons(uint64_t title_id, char* update_version, size_t capacity, unsigned* dlc_count);

#ifdef __cplusplus
}
#endif
