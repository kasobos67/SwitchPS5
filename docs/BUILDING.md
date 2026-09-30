# Building ProsperoEden

ProsperoEden builds on Linux (Ubuntu 26.04; WSL works) with the PS5 Payload SDK.
The release build is `tools/ci/build-release.sh`. It produces
`dist/ProsperoEden-vX.Y.Z.zip`, the `PPSA99008` folder to copy to
`/data/homebrew/PPSA99008`, plus `SHA256SUMS` and the release notes.

## What the build uses

- **Eden** at commit `5f142c7926d0c7fcbbd0ce30794d72f638a43b2a`, as the archive
  pinned in `UPSTREAM.json`, with Eden's own hash-pinned CPM dependencies.
  ProsperoEden does not modify Eden's files. The PS5 frontend in `headless/`
  replaces and derives sources at configure time (`headless/inject.cmake`).
- **PS5 Native App Boilerplate**, which provides the Payload SDK v0.42, the
  runtime `libc.prx` and the native packaging tool.
- **ps5-opengl**, a prebuilt OpenGL 4.6 SDK (checked by hash in
  `headless/CMakeLists.txt`) plus its `libSceAgc` link stubs.
- **Mihawk's PS5 Mesa (RADV) and PS5 Vulkan**, a pinned RADV release archive
  isolated with `tools/isolate-radv.py`. It is built by
  `tools/build-radv-dependencies.sh`, and PS5 Vulkan supplies the link recipe
  and the platform libraries.
- **ps5-vulkan**, for its `libSceAgcDriver` link stub.
- **SDL2, RmlUi and FreeType**, the prebuilt PS5 libraries vendored in
  ProsperoRadio.
- **OpenSSL and zlib** from pacbrew v0.40.2.
- Small contracts from our research repositories, in `third_party/`.

These inputs sit beside this repository, as sibling folders and in `.deps/`,
the way a development checkout lays them out. `build-release.sh` links them in
from the checkout named by `EDEN_DEV_CHECKOUT`.

Host tools: `clang-18`, `lld-18` and `llvm-18` (including `llvm-readobj-18`),
`clang` with its compiler-rt builtins, `cmake`, `ninja`, `ccache`, `make`,
`nasm`, `glslangValidator`, `spirv-val`, binutils, and Python 3.11 or later.

## Local build

```bash
EDEN_DEV_CHECKOUT=/path/to/development/checkout bash tools/ci/build-release.sh
```

The first build takes a while. Later builds reuse the cache in
`~/.cache/ps5-eden-headless.*` and ccache.

## Release workflow

`.github/workflows/release.yml` runs on a self-hosted runner labelled
`prosperoeden`, because the prebuilt inputs are not published. Set
`EDEN_DEV_CHECKOUT` in the runner's `.env` file.

- **Manual run** (Actions > Release build > Run workflow): builds the ZIP and
  keeps it as a 7-day artifact.
- **Tag `vX.Y.Z`**: builds the ZIP, checks that the tag matches the package
  version, and publishes a pre-release. The release notes come from the
  README's "Changes in vX.Y.Z" section.

To cut a release:

1. Bump the version in `tools/package-headless-native.sh` and
   `headless/prosperoeden/ui/main.rml`.
2. Add the "Changes in" section to the README.
3. Test the build on a console.
4. Push a `vX.Y.Z` tag.
