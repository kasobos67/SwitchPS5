# Filesystem access (sandbox elevation)

From [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
`examples/sandbox-elevation` at dd44bbd (GPL-3.0-or-later): `elevation.hpp`, `protocol.hpp`,
`elevation.cpp` (app side), `helper/` (the elfldr helper) and `validate-helper.py`.

ProsperoEden changes: the helper's `target_title_id` is `PPSA99008`, and `elevation.cpp` includes
`elevation.hpp` from this folder. The packager builds the helper with the PS5 payload SDK and
ships it as `/app0/sandbox-elevator.elf`; `main.cpp` requests `Capability::filesystem` once at
startup so the Game files folder (default `/data/prosperoeden`, `assets_dir.h`) is readable.
Without an elfldr on the console the request fails and the app keeps its sandbox and `/app0/assets`.
