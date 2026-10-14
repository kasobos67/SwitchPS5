# Game files, app data and updates

SwitchPS5 installs as Switch under title ID PPSA99009. It uses the existing ProsperoEden data folder and can be installed alongside ProsperoEden.

## Game files folder

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
│   └── <title ID>/                     # the 16-character code in the game's file name
│       ├── <mod name>/
│       │   ├── exefs/                  # code patches: *.pchtxt, *.ips
│       │   ├── romfs/                  # replacement game files
│       │   └── cheats/
│       │       └── <build ID>.txt      # cheats for one version of the game
│       └── cheat_<name>.txt            # optional: cheats without a mod folder
├── save-import/                        # optional: saves to import, one folder per title ID
├── ryujinx/                            # optional: a Ryujinx data folder to import saves from
├── save-export/                        # written by "Export a copy"
└── .remote-downloads/                  # downloads that are not finished yet (see Download sources)
```

The folder browser shows how many keys, firmware files, and games each folder holds, so you can check a folder before selecting it. Moving your files later only needs a new selection through the Game Files shortcut on HOME; saved settings, covers, and recently played games carry over.

## App data

ProsperoEden keeps its own data in `/data/prosperoeden`, separately from the game files folder:

```text
/data/prosperoeden/
├── config/prosperoeden.json            # every profile's settings, and the game files folder
├── config/remote/                      # download sources and save sync, when you use them:
│   ├── sources.json                    #   the sources and how to reach them (written by you)
│   ├── queue.json                      #   the download queue
│   ├── <source>/catalog.json           #   each source's game list, kept for the next start
│   ├── save-sync.json                  #   where each profile syncs its save data (filled in by you)
│   └── save-sync/                      #   what the save sync keeps: the console as a device of
│                                       #   each profile's server, the games still to be synced
├── backup/save-sync/                   # save data the save sync replaced, the last three per game
├── covers/                             # cached game covers
├── logs/                               # current and previous session logs, crash reports
└── user/                               # every profile's saves, and emulator user data
    └── lossless/Lossless.dll           # optional: your copy, for frame generation
```

The app itself stays in `/data/homebrew/PPSA99009`; see [Updating](#updating).

No complete firmware, encryption keys, game dumps or user save data are bundled. Supply the files you have dumped from your own hardware and software.

This is the only place it keeps them. If ProsperoEden cannot get access to the console's storage when it starts, the home screen says so and no game can be started; your data is untouched, and closing ProsperoEden and opening it again is the first thing to try. Earlier versions kept a second set of settings and saves in the app's own sandbox in that case, which made saves and settings seem to disappear on consoles where access came and went.

Keys, firmware and games that an early version kept inside the app's folder (`/data/homebrew/PPSA99009/assets`) are moved to `/data/prosperoeden` the first time this version starts, so that replacing the app cannot remove them.

## Updating

Automatic updates are disabled to preserve the SwitchPS5 interface. Install a new
SwitchPS5 release manually; an upstream ProsperoEden release is not a replacement
for this custom app.

1. Close Switch and back up the currently installed PPSA99009 folder.
2. Extract the install ZIP and replace the installed PPSA99009 folder with its
   PPSA99009 folder using your existing homebrew installation method.
3. Keep /data/prosperoeden intact. It holds games, keys, firmware, settings,
   profiles, covers and saves shared with ProsperoEden.
4. Open Switch. About shows the installed version.

The default game-files folder remains /data/prosperoeden. To select another one,
open Game Files from its HOME shortcut, select the folder and reopen Switch.

## Going back to the previous version

Close Switch and restore the PPSA99009 folder you backed up before updating.
Keep /data/prosperoeden in place. Manual updates do not create an automatic app
backup; back up the install folder yourself before replacing it.
