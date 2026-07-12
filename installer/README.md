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

## ffmpeg on Windows

BinCue Studio invokes `ffmpeg`/`ffprobe` at runtime and warns at startup when
they are missing. They are deliberately not bundled (size/licensing); users
can either put them on PATH or drop `ffmpeg.exe`/`ffprobe.exe` next to
`bincue-studio.exe` — Windows resolves subprocess names against the
application directory first.
