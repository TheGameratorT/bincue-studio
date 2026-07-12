# Windows installer

One QtIFW setup exe ships both applications: `bincue-studio.exe` and
`cdlabel.exe`, each with a Start Menu shortcut. CI builds it on every push
(see `.github/workflows/build.yml`); releases attach it automatically when a
`vX.Y.Z` tag is pushed.

## Building locally (MSYS2 MINGW64 shell)

```bash
pacman -S mingw-w64-x86_64-{gcc,cmake,ninja,pkgconf,qt6-base,qt6-svg,qt6-tools,taglib,qt-installer-framework}
# hostkit must be checked out as a sibling of this repo (../hostkit)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                    # also populates build/dist via `deploy`
cmake --build build --target installer # -> build/BinCueStudio-<version>-Setup.exe
```

Outside MSYS2, install the Qt Installer Framework and point the `QTIFWDIR`
environment variable at it so `binarycreator` is found.

## How it fits together

- `windows.cmake` — included by the top-level CMakeLists on Windows. The
  `deploy` target copies both exes into `build/dist/` and resolves their Qt
  DLLs (windeployqt) and MinGW/third-party DLLs (ldd). The `installer` target
  runs `build_installer.cmake`.
- `build_installer.cmake` — script mode; stages config + package metadata +
  `dist/` into `build/installer-staging/` (never writes into the source tree)
  and runs `binarycreator`.
- `config/config.xml.in`, `packages/.../package.xml.in` — versioned from
  `project(VERSION)` in the top-level CMakeLists; `installscript.qs` creates
  the shortcuts.

## cdrdao on Windows

Unlike ffmpeg, `cdrdao` **is** bundled. `windows.cmake` fetches the official
prebuilt Windows bundle from the cdrdao project's release (pinned by URL and
SHA-256), and the `deploy` target stages `cdrdao.exe` and its `msys-*.dll`
runtime into `dist/` next to the app, so burning to a local drive works with no
extra setup. cdrdao is GPLv2+; `third-party/cdrdao-NOTICE.txt` ships alongside
it. To move to a newer cdrdao, update `CDRDAO_VERSION`/`CDRDAO_URL`/`CDRDAO_SHA256`
in `windows.cmake` and delete `build/cdrdao-<version>/` to force a re-fetch.

## ffmpeg on Windows

`ffmpeg` and `ffprobe` are bundled too, the same way as cdrdao: `windows.cmake`
fetches the gyan.dev "essentials" static build (pinned by URL and SHA-256) and
the `deploy` target stages `ffmpeg.exe`/`ffprobe.exe` into `dist/` next to the
app, so audio decoding works with nothing for the user to install. These are
large static exes (~100 MB each), which is the main size cost of the installer;
they are GPLv3, with `third-party/ffmpeg-NOTICE.txt` shipped alongside. To move
to a newer ffmpeg, update `FFMPEG_VERSION`/`FFMPEG_URL`/`FFMPEG_SHA256` in
`windows.cmake` and delete `build/ffmpeg-<version>/` to force a re-fetch.

(BinCue Studio still resolves `ffmpeg`/`ffprobe` by name and warns at startup if
they are somehow missing — Windows resolves subprocess names against the
application directory first, so the bundled copies win without touching PATH.)
