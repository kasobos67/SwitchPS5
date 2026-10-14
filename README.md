# SwitchPS5

Switch-style PS5 UI based on [ProsperoEden](https://github.com/blackbearreloaded/ProsperoEden). The complete modified source is browseable in this repository. Download the installable [`Switch-PPSA99009-v1.001.000.zip`](https://github.com/kasobos67/SwitchPS5/releases/tag/v1.001.000) from the release page; it contains the `PPSA99009/` folder.

## Screenshots

These launcher previews use placeholder games and profiles.

![Switch-style HOME screen](screenshots/home.png)

![All Software library](screenshots/library.png)

![System Settings](screenshots/settings.png)

![Multiplayer menu](screenshots/multiplayer.png)

The PS5 app is named **Switch** and uses title ID **PPSA99009**, changed from the upstream app. You can install it on its own or alongside ProsperoEden. Both use the established `/data/prosperoeden` folder for games, settings, keys, firmware, and saves, so switching between the apps does not require moving that data.

The **emulation engine and game execution core are unchanged from ProsperoEden v1.001.00**. This project changes the UI, its launcher integration, branding, package identity, and update behavior. The UI uses selected art, fonts, motion data, and sounds extracted from decrypted Switch firmware. No complete firmware, keys, game dumps, or saves are included.

I tested the app on a PS5 and the UI and most other tested functions work. **Frame generation remains unconfirmed** on that console; no error was observed, but im not sure it has any effect. Automatic updates are disabled in this custom build. Future ProsperoEden versions need a tested merge and manual app-folder replacement, while `/data/prosperoeden` stays in place.

ProsperoEden creator: **[BlackBearReloaded](https://github.com/blackbearreloaded/ProsperoEden)**. Emulator core: [Eden and contributors](https://github.com/eden-emulator/mirror). See the [release notes](https://github.com/kasobos67/SwitchPS5/releases/tag/v1.000.110), the upstream credits, and the source licenses. This is unofficial and is not affiliated with Nintendo or Sony.
