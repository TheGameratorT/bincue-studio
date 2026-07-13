# Windows-only: build a minimal, audio-only FFmpeg from source and expose it to
# pkg-config so the app links libavformat/libavcodec/libavutil/libswresample
# against it. Included from the top-level CMakeLists (inside if(WIN32)) BEFORE
# add_subdirectory(src), because src's pkg_check_modules(LIBAV REQUIRED) resolves
# at configure time and must find these .pc files.
#
# Why build from source instead of bundling a prebuilt: the gyan/BtbN binaries
# carry every video + audio codec (tens of MB of avcodec), and we only ever
# decode audio. Configuring FFmpeg with --disable-everything and re-enabling just
# the audio demuxers/decoders we accept yields a handful of small LGPL DLLs (no
# GPL components, no external codec libs — every decoder here is native), which
# installer/windows.cmake stages next to the app. This is the "only audio codecs"
# build. The supported Windows build environment is MSYS2/MinGW, which provides
# the bash + make + nasm that FFmpeg's configure needs.

set(FFMPEG_VERSION "8.1.2")
set(FFMPEG_URL "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz")
set(FFMPEG_SHA256 "464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c")
set(FFMPEG_SRC_ARCHIVE "${CMAKE_BINARY_DIR}/ffmpeg-${FFMPEG_VERSION}.tar.xz")
set(FFMPEG_SRC_DIR "${CMAKE_BINARY_DIR}/ffmpeg-${FFMPEG_VERSION}")
# Where the built libraries + headers + pkg-config files land. Exported so
# installer/windows.cmake can stage the DLLs from ${FFMPEG_AUDIO_PREFIX}/bin.
set(FFMPEG_AUDIO_PREFIX "${CMAKE_BINARY_DIR}/ffmpeg-audio" CACHE INTERNAL
    "Install prefix of the audio-only FFmpeg build")

# Formats the app accepts (see audioFileFilter): FLAC, WAV, MP3, Ogg
# Vorbis/Opus, M4A/AAC/ALAC, AIFF — plus the raw PCM flavours WAV/AIFF carry.
# Every decoder below is native to FFmpeg, so --disable-autodetect keeps the
# build free of external codec libraries and their DLLs.
set(FFMPEG_DEMUXERS  "flac,wav,w64,aiff,mp3,ogg,mov,aac")
set(FFMPEG_DECODERS  "flac,alac,mp3,mp3float,vorbis,opus,aac,aac_latm,\
pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_s32be,pcm_u8,\
pcm_f32le,pcm_f64le")
set(FFMPEG_PARSERS   "flac,mpegaudio,vorbis,opus,aac,aac_latm")

# The pkg-config files that prove the build finished; skip everything if present.
set(FFMPEG_PC_DIR "${FFMPEG_AUDIO_PREFIX}/lib/pkgconfig")

if(NOT EXISTS "${FFMPEG_PC_DIR}/libavcodec.pc")
    if(NOT EXISTS "${FFMPEG_SRC_DIR}/configure")
        message(STATUS "Fetching FFmpeg ${FFMPEG_VERSION} source for an audio-only build…")
        file(DOWNLOAD "${FFMPEG_URL}" "${FFMPEG_SRC_ARCHIVE}"
            EXPECTED_HASH SHA256=${FFMPEG_SHA256}
            SHOW_PROGRESS STATUS _ff_dl)
        list(GET _ff_dl 0 _ff_dl_code)
        if(NOT _ff_dl_code EQUAL 0)
            list(GET _ff_dl 1 _ff_dl_msg)
            message(FATAL_ERROR "Could not download FFmpeg source: ${_ff_dl_msg}")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${FFMPEG_SRC_ARCHIVE}"
            DESTINATION "${CMAKE_BINARY_DIR}")
    endif()

    # FFmpeg's configure is a POSIX shell script; run it through MSYS2's bash.
    # A build/ subdir keeps the source tree clean and lets configure be re-run.
    find_program(BASH_EXECUTABLE NAMES bash REQUIRED)
    include(ProcessorCount)
    ProcessorCount(_ff_jobs)
    if(_ff_jobs EQUAL 0)
        set(_ff_jobs 2)
    endif()

    message(STATUS "Configuring audio-only FFmpeg (this runs once)…")
    execute_process(
        COMMAND "${BASH_EXECUTABLE}" -c
            "cd '${FFMPEG_SRC_DIR}' && ./configure \
                --prefix='${FFMPEG_AUDIO_PREFIX}' \
                --enable-shared --disable-static \
                --disable-programs --disable-doc --disable-autodetect \
                --disable-avdevice --disable-avfilter \
                --disable-swscale --disable-network \
                --disable-everything --enable-small \
                --enable-protocol=file,pipe \
                --enable-demuxer=${FFMPEG_DEMUXERS} \
                --enable-decoder=${FFMPEG_DECODERS} \
                --enable-parser=${FFMPEG_PARSERS}"
        RESULT_VARIABLE _ff_cfg)
    if(NOT _ff_cfg EQUAL 0)
        message(FATAL_ERROR "FFmpeg configure failed (see ffbuild/config.log)")
    endif()

    message(STATUS "Building + installing audio-only FFmpeg…")
    execute_process(
        COMMAND "${BASH_EXECUTABLE}" -c
            "cd '${FFMPEG_SRC_DIR}' && make -j${_ff_jobs} && make install"
        RESULT_VARIABLE _ff_make)
    if(NOT _ff_make EQUAL 0)
        message(FATAL_ERROR "FFmpeg build/install failed")
    endif()
endif()

# Put our freshly built .pc files first on pkg-config's search path so src's
# pkg_check_modules(LIBAV) picks these up rather than any system FFmpeg. The
# mingw64 pkg-config is a native Windows build, so it splits PKG_CONFIG_PATH on
# ';' — using ':' would break the 'D:/...' drive letter in these paths.
if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
    set(ENV{PKG_CONFIG_PATH} "${FFMPEG_PC_DIR};$ENV{PKG_CONFIG_PATH}")
else()
    set(ENV{PKG_CONFIG_PATH} "${FFMPEG_PC_DIR}")
endif()
message(STATUS "Audio-only FFmpeg ready at ${FFMPEG_AUDIO_PREFIX}")
