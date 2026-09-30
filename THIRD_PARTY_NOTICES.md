# Third-party notices

ProsperoEden is licensed under GPL-3.0-or-later (see [LICENSE](LICENSE)). It
builds on the following projects, each under its own license.

## Emulator

- **[Eden](https://github.com/eden-emulator/mirror)**, GPL-3.0-or-later. The emulator core, built
  from the commit pinned in `UPSTREAM.json`, together with the dependencies Eden
  fetches and pins itself: Dynarmic, Boost, fmt, xbyak, zstd, lz4, Opus, SDL3,
  simpleini, sirit, SPIRV-Headers, Vulkan-Headers, Vulkan-Utility-Libraries,
  Vulkan Memory Allocator, ENet, frozen, cpp-httplib, nlohmann/json, and the time
  zone data. Each keeps its upstream license.
- **[FFmpeg](https://ffmpeg.org)**, LGPL-2.1-or-later. Built from Eden's pinned
  source with only the H.264, VP8 and VP9 decoders.
- **[OpenSSL](https://www.openssl.org)**, Apache-2.0, and
  **[zlib](https://zlib.net)**, zlib license. Taken from the
  [pacbrew](https://github.com/ps5-payload-dev/pacbrew-repo) PS5 packages.
- **LLVM compiler-rt** `emutls.c`, Apache-2.0 WITH LLVM-exception.

## Graphics

- **[PS5 Mesa](https://github.com/mihawk-99/PS5_Mesa)** and
  **[PS5 Vulkan](https://github.com/mihawk-99/PS5_Vulkan)** by Mihawk. The
  RADV Vulkan driver (Mesa, mainly MIT) and its PS5 link recipe and platform
  layer (GPL-3.0-or-later).
- **[ps5-vulkan](https://github.com/mpereiraesaa/ps5-vulkan)** by mpereiraesaa,
  an experimental Vulkan graphics and compute API for native PS5 homebrew.
- **[ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl)**,
  GPL-3.0-or-later, which includes Mesa components under their own licenses.
  `third_party/app_heap.c` comes from it.

## Platform and interface

- **[PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk)** by John
  Törnblom (ps5-payload-dev).
- **[SDL2 PS5 port](https://github.com/ps5-payload-dev/SDL)** by John Törnblom,
  zlib license.
- **[PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)**,
  GPL-3.0-or-later. The native app runtime, packaging tool and sandbox
  elevation helper (`headless/elevation`). Its packaging tool translates parts
  of SvenGDK's [SharpProspero](https://github.com/SvenGDK/SharpProspero).
- **[RmlUi](https://github.com/mikke89/RmlUi)**, MIT, and
  **[FreeType](https://freetype.org)**, FreeType License. Both come from the
  prebuilt libraries in ProsperoRadio.
- **Montserrat** by Julieta Ulanovsky and contributors, SIL Open Font License
  1.1. The bitmap font sizes were converted with LVGL's font tools.
- `third_party/ps5_pad.hpp` comes from
  [ps5-native-gamepad-input-research](https://github.com/blackbearreloaded/ps5-native-gamepad-input-research),
  and `third_party/native_audio.hpp` from
  [ps5-audio-decoding-research](https://github.com/blackbearreloaded/ps5-audio-decoding-research).
  Both are GPL-3.0-or-later.

## Thanks

ProsperoEden exists thanks to the Eden maintainers and contributors, Mihawk,
mpereiraesaa, John Törnblom, SvenGDK, and the whole PS5 homebrew community.
