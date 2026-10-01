<p align="center">
  <img src="sce_sys/icon0.png" width="128" alt="ProsperoEden icon">
</p>

<h1 align="center">ProsperoEden</h1>

<p align="center">
  <strong>An unofficial Eden emulator port for PlayStation 5 homebrew</strong>
</p>

**ProsperoEden is an unofficial PlayStation 5 port of [Eden](https://github.com/eden-emulator/mirror)** - an accurate, high-performance emulator. All credit for the emulator core belongs to the Eden project and its contributors. ProsperoEden is not affiliated with or endorsed by the Eden team or Sony.

This is an early alpha. Video, audio, controller input, and saves have been confirmed working. Compatibility and performance will vary between games. The current release is **v1.000.030**.

## Source code

The complete ProsperoEden source is in this repository: the PS5 frontend and launcher in `headless/`, and the build and packaging tools in `tools/`. To build it yourself, run `make` on Linux (Ubuntu 26.04; WSL works). It fetches every dependency at its pinned revision and writes the release files to `dist/`; `make help` lists the other targets. See [docs/BUILDING.md](docs/BUILDING.md).

## Project foundation

> [!IMPORTANT]
> **Built on the [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate), the same native foundation used by ProsperoLight.**
> It provides the native PS5 application structure, runtime, packaging, and homebrew deployment foundation.

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
- **Resolution and upscaling** - render at 0.5x to 4x of the game's resolution and choose the filter that scales it to your TV (Bilinear, AMD FSR, Bicubic or Nearest) in **Settings > Video**.
- **Output resolution** - the picture is made at 1080p, 1440p or 2160p: **Output resolution** in **Settings > Video**. The menu is drawn at that size too, and the PS5 scales it to your TV.
- **120 Hz output** - on a display that shows 120 Hz, games can run on a 120 Hz output: **Refresh rate** in **Settings > Video**, or in one game's settings. A frame that is a little late is then shown 8 ms later instead of 17 ms, and patches for more than 60 FPS need it. The menu stays at 60 Hz.
- **Game files anywhere** - keys, firmware, and games can live in any folder the PS5 can read: internal storage, an M.2 or external drive, or a USB device.
- **Folder browser** - pick the game files folder in **Settings > Game files**. It shows how many keys, firmware files, and games each folder holds. Hold L1/R1 to page quickly.
- **Library** - game covers, **Continue Playing**, and **Recently Played**, which keep working after you move your files.
- **Launcher** - an animated interface drawn with OpenGL, with sound effects (their level is in **Settings > Audio**) and a loading screen while a game starts. The home screen shows which controllers are connected.
- **Per-game settings** - renderer, resolution, upscaling filter and Handheld / Docked mode for each game (Triangle in the Library).
- **Game updates and DLC** - put update and DLC files (NSP or XCI) in the `updates` folder next to `roms`. They apply when the game starts, and each game's details show the update version and DLC count.
- **Shader cache** - shaders compiled in earlier sessions are loaded when a game starts, so an effect stutters only the first time it appears.
- **In-game shortcuts** - a performance overlay (Select + R1), and Select + L1 to end the game and return to the library.
- **Settings in one place** - a single JSON file under `/data/prosperoeden`, with game volume, mute, and detailed logging options. Logs keep the previous session.
- **Crash reports** - if ProsperoEden stops because of an error, it saves a report with that session's logs, starts again and shows where the report is.
- **Controllers, audio, and saves** - up to four DualSense controllers (one per signed-in PS5 user) with rumble and motion controls, game audio, and save data work out of the box.

## Install

1. Download and extract the release ZIP.
2. Copy the included `PPSA99008` folder to `/data/homebrew/PPSA99008` on the PS5.
3. Put your own legally dumped keys, firmware, and games in a **game files folder** (layout below). It can be anywhere the PS5 can read: internal storage, an M.2 or external drive, or a USB device.
4. Launch **ProsperoEden**, open **Settings > Game files**, browse to that folder and select it. The default is `/data/prosperoeden`.
5. Close and reopen ProsperoEden, then open **Library**. Setup is checked when the app opens, so reopen it after changing the folder or adding keys or firmware.

### Game files folder

Only these subfolders matter; the folder itself can have any name and location.

```text
<game files folder>/                    # e.g. /data/prosperoeden, /mnt/ext1/eden, /mnt/usb0/eden
├── keys/
│   ├── prod.keys
│   └── title.keys                      # optional
├── firmware/
│   └── *.nca                           # extracted firmware NCAs
├── roms/
│   ├── Game.nsp
│   └── Game.xci
├── updates/                            # optional: update and DLC files
│   └── Game update.nsp
├── mods/                               # optional: mods, one folder per title ID
├── save-import/                        # optional: saves to import, one folder per title ID
├── ryujinx/                            # optional: a Ryujinx data folder to import saves from
└── save-export/                        # written by "Export a copy"
```

The folder browser shows how many keys, firmware files, and games each folder holds, so you can check a folder before selecting it. Moving your files later only needs a new selection in Settings; saved settings, covers, and recently played games carry over.

### App data

ProsperoEden keeps its own data in `/data/prosperoeden`, separately from the game files folder:

```text
/data/prosperoeden/
├── config/prosperoeden.json            # settings, including the game files folder
├── covers/                             # cached game covers
├── logs/                               # current and previous session logs, crash reports
└── user/                               # saves and emulator user data
```

The app itself stays in `/data/homebrew/PPSA99008` and can be updated by copying a new release over it.

ProsperoEden does not include keys, firmware, games, or other copyrighted console data. Dump these files from hardware and software you own. Do not download or redistribute them.

### Moving save data

Emulators like Eden keep a save as the files the game wrote, so nothing is converted: folders are copied. In the Library, press Triangle on a game and pick **Save data**. The folders below are next to `roms/` in the game files folder.

- **Import from a folder.** Copy the contents of the game's save folder (what an emulator opens as the game's save directory) into `save-import/<title ID>/`. The title ID is the 16-character code in the game's file name; Save data also shows it when there is nothing to import.
- **Import from Ryujinx.** Copy Ryujinx's data folder (the one that holds `bis/`, or a portable folder around it) to `ryujinx/`. ProsperoEden finds the game's save in it: the first user's, and the device save when there is one.
- **Export.** Square writes `save-export/<title ID>-<date>-<time>/`, with `account/` and `device/` inside. An exported folder can be imported again: put it in `save-import/` under the title ID.

Cross imports, and asks before it replaces a save. The save it replaces is first moved to `/data/prosperoeden/backup/save-import`, so nothing is lost.

### Mods

A mod changes a game: a patch to its code (`.pchtxt` or `.ips` files in an `exefs` folder), replacement game files (a `romfs` folder), or cheats (a `cheats` folder). Mods made for other emulators of the same console come in this layout.

- **Add a mod.** Each mod is a folder. Copy it to `mods/<title ID>/`, next to `roms/` in the game files folder, so that a patch ends up at `mods/<title ID>/<mod name>/exefs/<file>.pchtxt`. The title ID is the 16-character code in the game's file name. The Mods screen names the exact folder, and Square creates it. The About screen shows where the `mods` folder is.
- **Switch it on or off.** In the Library, press Triangle on the game and pick **Mods**. Every mod found is listed with a switch. A mod is on unless you switch it off, and a change applies the next time the game starts.
- **Switch all of a game's mods off or on.** In the Library, a game that has mods shows a **Mods** switch under its console mode; Square flips it. Off, the game starts without any of its mods, and each mod keeps its own switch for when you turn it back on.
- **See what a game has.** The home screen and the Library count a game's mods next to its update and DLC (`Update 1.2.0, 2 DLC, 1 mod`), and say so when some are switched off (`1 of 2 mods on`).
- **Match the game's version.** A patch is made for one version of a game. One made for another version is ignored without a message, so check that the mod matches the update you have in `updates/`.
- **Frame rate.** A patch that makes a 30 FPS game run at 60 FPS works on the 60 Hz output ProsperoEden uses. A patch for more than 60 FPS needs the 120 Hz output: set **Refresh rate** to 120 Hz in the game's settings (Triangle in the Library) or in **Settings > Video**. That takes a display that shows 120 Hz and the PS5's own 120 Hz output setting; without them the game runs at 60 Hz. A 60 FPS patch can gain from it too: a game that misses some frames at 60 Hz has twice as many chances to show them. A patch for more frames than the output shows (240 FPS on the 120 Hz output, 120 FPS on the 60 Hz one) still runs at its own pace: the frames the display has no refresh for are left out.

ProsperoEden does not include or download mods.

### Language and accessibility

The launcher follows the language the PS5 is set to: Arabic, Chinese (simplified and traditional), Czech, Danish, Dutch, Finnish, French, German, Greek, Hungarian, Indonesian, Italian, Japanese, Korean, Norwegian, Polish, Portuguese, Romanian, Russian, Spanish, Swedish, Thai, Turkish, Ukrainian and Vietnamese (with the regional variants the PS5 has for French, Portuguese and Spanish), and English otherwise. Arabic, Chinese, Greek, Japanese, Korean and Thai are drawn with the PS5's own system fonts. Arabic text runs right to left; the screens themselves are not mirrored. To use another one, put a file named `language.txt` holding its tag (for example `en-US` or `pt-BR`) in `/data/homebrew/PPSA99008`. The language *games* use is a separate setting, **Settings > Language**.

**Settings > Accessibility** has three switches. **Larger text** draws the menu's small text about a third larger. **High contrast** uses solid panels, brighter text and an outlined highlight. **Reduce motion** stops the background drifting and the screens sliding, in the menu and on the loading screen. There is no screen reader.

### Upgrading from an earlier alpha

Earlier versions read everything from `/data/homebrew/PPSA99008/assets/`. That folder keeps working until you choose a game files folder, and settings are migrated automatically on first launch. To move to the new layout, move `assets/keys`, `assets/firmware` and `assets/roms` into any folder, then select it in **Settings > Game files**. The release ZIP contains no user files, so copy its app files over your installation without deleting your own data.

## Changes in v1.000.030

- **New launcher.** Every screen is redrawn with OpenGL: smooth transitions between screens, text that stays sharp, panels that blur the artwork behind them, and sound effects for moving, selecting and going back. **Settings > Audio > Menu sounds** sets their level.
- **Controllers on the home screen.** Four controller icons show which controllers are connected, and change as one joins or leaves.
- **Loading screen.** An animated scene shows while a game starts, on both renderers.
- **The Library opens at once.** The game list is read in the background, and the home screen shows each game's own name instead of its file name.
- **New music** on the PS5 home screen.
- **Smoother first-time gameplay.** The emulated CPU cores now share the code they compile, so each part of a game is compiled once instead of once per core. In large open-world games this halves the compile work and removes most of the stutter when gameplay starts or a new area loads.
- **Faster compiling.** Compiling a game's code now takes about 40% less CPU time, which shortens the remaining stutter when gameplay starts or a new area loads.
- **Startup hang fixed.** A game could stop for good right after starting, because the emulator's memory allocator could leave high-priority threads waiting on each other forever. Development builds also report where a slow start is stuck.
- **Games close in about a second.** Select + L1 used to take 10-25 seconds to return to the library; logging no longer waits on the console's storage, and the emulator skips needless teardown work.
- **Up to four controllers.** Each signed-in PS5 user's controller becomes the next player (player 1 is whoever launched the game); controllers can join or leave during a game, and games that ask for controllers connect every one in use.
- The Select + L1 and Select + R1 shortcuts work from any controller.
- **Vibration and motion.** DualSense rumble for games that use it (turn it off in **Settings > Controls**), and the controller's gyro and accelerometer for motion controls.
- **Resolution and upscaling.** **Settings > Video** now sets the internal rendering resolution (0.5x to 2x) and the filter that scales it to the TV: Bilinear, AMD FSR, Bicubic or Nearest.
- **Per-game settings.** Press Triangle on a game in the Library to give it its own renderer, resolution and upscaling filter.
- **Language setting.** **Settings > Language** picks the language games use (18 languages), with the console region that goes with it. It applies when a game starts.
- **Game updates and DLC.** Put update and DLC files (NSP or XCI) in the `updates` folder next to `roms`; the newest update and all DLC apply when the game starts, and the game details show them.
- **Build it yourself.** `make` fetches every dependency at its pinned revision and builds the release. The OpenGL renderer now uses the published PS5 OpenGL 4.6 SDK 0.6.0.

## Changes in v1.000.020

- **Vulkan is now the recommended and default renderer.** OpenGL remains available in Settings.
- **Choose where your game files live.** The new **Settings > Game files** browser selects any folder, including external drives and USB devices. The old `assets/` folder keeps working until you choose one.
- **App data moved to `/data/prosperoeden`.** Config, logs, covers, and saves now live outside the app folder, so updating the app never touches them.
- **Settings are now stored in one JSON file.** Earlier text settings are migrated automatically on first launch.
- **Faster Library navigation**, and covers and recent games that survive folder moves.
- **New icons** for controller actions, pages, and the Handheld / Docked mode.

**Known issues:** Ending a game with Select + L1 can take several seconds. Some games can still hang on the loading screen, and some demanding games remain slow. This is a testing pre-release, not a compatibility guarantee.

## In-game shortcuts

“Select” means pressing the DualSense touchpad itself, as in ProsperoLight. On its own, a tap of the touchpad is the game's Select (Minus) button, and a longer press holds it.

| Shortcut | Action |
|---|---|
| Select + R1 | Toggle the performance HUD |
| Select + L1 | End the running game and return to the library |

## Roadmap

- **FPKG support** - install ProsperoEden as a fake package, alongside the current homebrew folder install. Each release now includes a ShadowMountPlus package image (`.ffpfsc`); installing it still needs testing.
- **More performance** - CPU and GPU work to keep demanding games at their target frame rate, including the short stutter when a game starts, heavy cutscenes, and games that run slower in Docked mode than in Handheld.
- **More reliable game loading** - fix the remaining hangs on the loading screen.
- **Faster exit in every game** - a few games still take up to several minutes to close.
- **Touchpad button in every game** - in some games the touchpad (Select) does not respond and only the Create (Share) button works.
- **Controller selection screen** - some games wait forever on the screen that asks you to choose a controller.
- **Import saves from Ryujinx** - copy a game's save from a Ryujinx data folder into ProsperoEden. Eden's desktop app can already link Ryujinx saves, and the code that finds them is in the shared code ProsperoEden builds; ProsperoEden needs its own import step in the launcher.
- **Launcher in your language** - show the launcher's own text in the language the PS5 is set to.
- **More game compatibility** - validate more games on the PS5, and fix what keeps them from running well, such as games that crash at launch.

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
