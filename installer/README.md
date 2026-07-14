# Windows installer

One NSIS setup exe ships both applications: `bincue-studio.exe` and
`cdlabel.exe`, with Start Menu shortcuts for both and a desktop shortcut for
the studio. CI builds it on every push (see `.github/workflows/build.yml`);
releases attach it automatically when a `vX.Y.Z` tag is pushed.

NSIS replaced the Qt Installer Framework here: QtIFW's uninstaller
(`maintenancetool.exe`) was a statically-linked Qt app costing ~50 MB — a third
of the install. NSIS's setup stub and uninstaller together are ~100 KB.

## Building locally (MSYS2 MINGW64 shell)

```bash
pacman -S mingw-w64-x86_64-{gcc,cmake,ninja,pkgconf,qt6-base,qt6-svg,qt6-tools,taglib,nsis}
# hostkit must be checked out as a sibling of this repo (../hostkit)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                    # also populates build/dist via `deploy`
cmake --build build --target installer # -> build/BinCueStudio-<version>-Setup.exe
```

Outside MSYS2, install NSIS so `makensis` is on `PATH`.

## How it fits together

- `windows.cmake` — included by the top-level CMakeLists on Windows. The
  `deploy` target copies both exes into `build/dist/` and resolves their Qt
  DLLs (windeployqt) and MinGW/third-party DLLs (ldd). The `installer` target
  runs `build_installer.cmake`.
- `build_installer.cmake` — script mode; finds `makensis` and compiles
  `bincue-studio.nsi`, passing the version and every path (`dist/`, the LICENSE,
  the icon, the output exe) as `/D` defines. Writes only into the build dir.
- `bincue-studio.nsi` — the installer script: installs `dist/` into
  `Program Files\BinCue Studio`, shows the GPLv3 license, creates the
  shortcuts, sets the system `BINCUE_STUDIO_HOME` env var, registers an
  Add/Remove Programs entry, and writes the uninstaller.

## cdrdao on Windows

`cdrdao` is bundled as a prebuilt binary. `windows.cmake` fetches the official
prebuilt Windows bundle from the cdrdao project's release (pinned by URL and
SHA-256), and the `deploy` target stages `cdrdao.exe` and its `msys-*.dll`
runtime into `dist/` next to the app, so burning to a local drive works with no
extra setup. cdrdao is GPLv2+; `third-party/cdrdao-NOTICE.txt` ships alongside
it. To move to a newer cdrdao, update `CDRDAO_VERSION`/`CDRDAO_URL`/`CDRDAO_SHA256`
in `windows.cmake` and delete `build/cdrdao-<version>/` to force a re-fetch.

## FFmpeg (libav\*) on Windows

The app links FFmpeg's `libavformat`/`libavcodec`/`libavutil`/`libswresample`
directly — it no longer runs `ffmpeg.exe`/`ffprobe.exe`. Because Windows has no
system FFmpeg to link, `installer/ffmpeg-audio.cmake` **builds one from source**
at configure time (before the `src` subdirectory, so pkg-config finds it): it
downloads the official FFmpeg source (pinned by URL and SHA-256) and runs
`./configure --disable-everything` re-enabling only the audio demuxers/decoders
the app accepts (FLAC/WAV/AIFF, MP3, Ogg Vorbis/Opus, M4A/AAC/ALAC), then
`make install` into `build/ffmpeg-audio/`. The `deploy` target stages the
resulting `avcodec-*.dll`/`avformat-*.dll`/`avutil-*.dll`/`swresample-*.dll`
into `dist/` next to the app.

Because only audio codecs are enabled and no GPL/nonfree components are pulled
in, the DLLs are a few MB total (versus the ~200 MB the old bundled exes cost)
and are **LGPL** — `third-party/ffmpeg-NOTICE.txt` ships alongside. This needs
`nasm` and `make` in the MSYS2 build environment (added to the CI job). To move
to a newer FFmpeg, update `FFMPEG_VERSION`/`FFMPEG_URL`/`FFMPEG_SHA256` in
`ffmpeg-audio.cmake` and delete `build/ffmpeg-audio/` (and `build/ffmpeg-<ver>/`)
to force a rebuild.
