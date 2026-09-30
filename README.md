<p align="center">
  <img src="sce_sys/icon0.png" width="128" alt="ProsperoEden icon">
</p>

<h1 align="center">ProsperoEden</h1>

<p align="center">
  <strong>An unofficial Eden emulator port for PlayStation 5 homebrew</strong>
</p>

**ProsperoEden is an unofficial PlayStation 5 port of [Eden](https://github.com/eden-emulator/mirror)** - an accurate, high-performance emulator. All credit for the emulator core belongs to the Eden project and its contributors. ProsperoEden is not affiliated with or endorsed by the Eden team or Sony.

This is an early alpha. Video, audio, controller input, and saves have been confirmed working. Compatibility and performance will vary between games. The current release is **v1.000.020**.

## Source code

The complete ProsperoEden source is in this repository: the PS5 frontend and launcher in `headless/`, and the build and packaging tools in `tools/`. To build it yourself, run `make` on Linux (Ubuntu 26.04; WSL works). It fetches every dependency at its pinned revision and writes the release files to `dist/`; `make help` lists the other targets. See [docs/BUILDING.md](docs/BUILDING.md).

## Project foundation

> [!IMPORTANT]
> **Built on the [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate), the same native foundation used by ProsperoLight.**
> It provides the native PS5 application structure, runtime, packaging, RmlUi shell, and homebrew deployment foundation.

> [!IMPORTANT]
> **Graphics are powered by [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl).**
> This OpenGL implementation provides the native PS5 rendering layer used by the Eden graphics backend.

> [!IMPORTANT]
> **Vulkan is powered by Mihawk's [PS5 Mesa](https://github.com/mihawk-99/PS5_Mesa) and [PS5 Vulkan](https://github.com/mihawk-99/PS5_Vulkan).**
> Mihawk's Mesa/RADV driver for the PS5 runs ProsperoEden's Vulkan renderer. Many thanks to Mihawk for this work and for [all of the PS5 projects](https://github.com/mihawk-99) behind it.

> [!IMPORTANT]
> **Thanks to [ps5-vulkan](https://github.com/mpereiraesaa/ps5-vulkan) by mpereiraesaa**, an experimental Vulkan graphics and compute API for native PS5 homebrew.

## Features

- **Vulkan renderer (recommended)** - the default backend, running on Mihawk's PS5 Mesa (RADV) driver.
- **OpenGL renderer** - still available through ps5-opengl. Switch between them in **Settings > Video**.
- **Game files anywhere** - keys, firmware, and games can live in any folder the PS5 can read: internal storage, an M.2 or external drive, or a USB device.
- **Folder browser** - pick the game files folder in **Settings > Game files**. It shows how many keys, firmware files, and games each folder holds. Hold L1/R1 to page quickly.
- **Library** - game covers, **Continue Playing**, and **Recently Played**, which keep working after you move your files.
- **Per-game Handheld / Docked mode** - set from each game's details.
- **In-game shortcuts** - a performance overlay (Select + R1), and Select + L1 to end the game and return to the library.
- **Settings in one place** - a single JSON file under `/data/prosperoeden`, with game volume, mute, and detailed logging options. Logs keep the previous session.
- **Controller, audio, and saves** - DualSense input, game audio, and save data work out of the box.

## Install

1. Download and extract the release ZIP.
2. Copy the included `PPSA99008` folder to `/data/homebrew/PPSA99008` on the PS5.
3. Put your own legally dumped keys, firmware, and games in a **game files folder** (layout below). It can be anywhere the PS5 can read: internal storage, an M.2 or external drive, or a USB device.
4. Launch **ProsperoEden**, open **Settings > Game files**, browse to that folder and select it. The default is `/data/prosperoeden`.
5. Close and reopen ProsperoEden, then open **Library**. Setup is checked when the app opens, so reopen it after changing the folder or adding keys or firmware.

### Game files folder

Only the three subfolders matter; the folder itself can have any name and location.

```text
<game files folder>/                    # e.g. /data/prosperoeden, /mnt/ext1/eden, /mnt/usb0/eden
├── keys/
│   ├── prod.keys
│   └── title.keys                      # optional
├── firmware/
│   └── *.nca                           # extracted firmware NCAs
└── roms/
    ├── Game.nsp
    └── Game.xci
```

The folder browser shows how many keys, firmware files, and games each folder holds, so you can check a folder before selecting it. Moving your files later only needs a new selection in Settings; saved settings, covers, and recently played games carry over.

### App data

ProsperoEden keeps its own data in `/data/prosperoeden`, separately from the game files folder:

```text
/data/prosperoeden/
├── config/prosperoeden.json            # settings, including the game files folder
├── covers/                             # cached game covers
├── logs/                               # current and previous session logs
└── user/                               # saves and emulator user data
```

The app itself stays in `/data/homebrew/PPSA99008` and can be updated by copying a new release over it.

ProsperoEden does not include keys, firmware, games, or other copyrighted console data. Dump these files from hardware and software you own. Do not download or redistribute them.

### Upgrading from an earlier alpha

Earlier versions read everything from `/data/homebrew/PPSA99008/assets/`. That folder keeps working until you choose a game files folder, and settings are migrated automatically on first launch. To move to the new layout, move `assets/keys`, `assets/firmware` and `assets/roms` into any folder, then select it in **Settings > Game files**. The release ZIP contains no user files, so copy its app files over your installation without deleting your own data.

## Changes in v1.000.020

- **Vulkan is now the recommended and default renderer.** OpenGL remains available in Settings.
- **Choose where your game files live.** The new **Settings > Game files** browser selects any folder, including external drives and USB devices. The old `assets/` folder keeps working until you choose one.
- **App data moved to `/data/prosperoeden`.** Config, logs, covers, and saves now live outside the app folder, so updating the app never touches them.
- **Settings are now stored in one JSON file.** Earlier text settings are migrated automatically on first launch.
- **Faster Library navigation**, and covers and recent games that survive folder moves.
- **New icons** for controller actions, pages, and the Handheld / Docked mode.

**Known issues:** Ending a game with Select + L1 can take several seconds. Some games can still hang on the loading screen, and some demanding games remain slow. This is a testing pre-release, not a compatibility guarantee.

## In-game shortcuts

“Select” means pressing the DualSense touchpad itself, as in ProsperoLight.

| Shortcut | Action |
|---|---|
| Select + R1 | Toggle the performance HUD |
| Select + L1 | End the running game and return to the library |

## Roadmap

- **More controllers** - local multiplayer with a second DualSense and more, one per signed-in PS5 user.
- **FPKG support** - install ProsperoEden as a fake package, alongside the current homebrew folder install.
- **More performance** - CPU and GPU work to keep demanding games at their target frame rate.
- **Upscaling** - render below native resolution and upscale to the TV output, for smoother play in heavy games.
- **Faster, more reliable game exit** - bring Select + L1 down to about a second and fix the remaining hangs on the loading screen.
- **Persistent shader cache** - keep compiled shaders between sessions to remove stutter the first time an effect appears.
- **Per-game settings** - renderer, resolution and performance options saved for each game.
- **Vibration and motion** - DualSense rumble and gyro for games that use them.

## Issues are disabled

GitHub issues are turned off for this repository on purpose. ProsperoEden is a general-purpose emulator port, and the project does not host discussion of console makers, specific commercial games, compatibility reports, or where to find game files. Issue threads tend to fill up with exactly that, so there are none.

Please do not use pull requests or other channels to post that kind of content either.

<!-- bbr-footer:start -->
<!-- Generated by ps5-homebrew-dev-protocol/scripts/readme-footer. Edit the template there, not here. -->

## Credits

Built with the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) by John Törnblom (ps5-payload-dev).
Third-party components, authors and licenses are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License

Copyright © 2026 BlackBearReloaded. Licensed under GPL-3.0-or-later; see [LICENSE](LICENSE). Third-party components keep their own licenses. Binary releases are built from the tagged source in this repository.

## Disclaimer

- **No affiliation.** This is an independent homebrew project. It is not
  affiliated with, endorsed by, or sponsored by Sony Interactive Entertainment.
  "PlayStation", "PS5" and related marks are trademarks of Sony Interactive
  Entertainment Inc. This project is not affiliated with or endorsed by the Eden project.
- **No proprietary material.** No Sony SDK, firmware, encryption keys or
  decrypted system modules are included.
- **No warranty.** This project is provided "as is", without warranty of any
  kind, to the extent permitted by law. See sections 15 and 16 of the GPL.
- **Use at your own risk.** Running homebrew requires a modified console, which
  may void its warranty, breach the platform's terms of service, or cause data
  loss.
- **Legal use only.** Use it only with hardware, accounts and content you own.
  This project does not support or enable piracy.

## AI assistance

This project was developed with AI assistance from OpenAI and/or Anthropic tools.
<!-- bbr-footer:end -->
