# Windows-only deployment: gathers both executables plus their Qt and MinGW
# runtime DLLs into build/dist, and adds the `installer` target that packs
# dist into a QtIFW setup exe. Included from the top-level CMakeLists inside
# if(WIN32).

set(DIST_DIR "${CMAKE_BINARY_DIR}/dist")

# Bundle cdrdao (the CD writer) so burning works out of the box. cdrdao has
# shipped native-Windows support since 1.2.6 — it talks to the drive by letter
# (--device D) over SPTI, not the dead ASPI layer — so a local burn needs no
# extra setup. We fetch its official prebuilt bundle, pinned by hash, and stage
# cdrdao.exe plus its MinGW runtime DLLs next to bincue-studio.exe; Windows
# resolves the subprocess name against the application directory first, so the
# app finds this cdrdao without touching PATH.
set(CDRDAO_VERSION "1.2.6")
set(CDRDAO_URL "https://github.com/cdrdao/cdrdao/releases/download/rel_1_2_6/cdrdao126.zip")
set(CDRDAO_SHA256 "653e56c49f94771f87cd9f4ceefc255329714ba5066bd78e30edaa464c322a3a")
set(CDRDAO_ZIP "${CMAKE_BINARY_DIR}/cdrdao-${CDRDAO_VERSION}.zip")
set(CDRDAO_DIR "${CMAKE_BINARY_DIR}/cdrdao-${CDRDAO_VERSION}")

# The app only ever invokes `cdrdao`; the bundle's other exes (cue2toc, toc2cue,
# toc2cddb) are unused, so we stage just the writer and its runtime DLLs.
set(CDRDAO_FILES
    cdrdao.exe
    msys-2.0.dll
    msys-gcc_s-seh-1.dll
    msys-iconv-2.dll
    msys-stdc++-6.dll)

if(NOT EXISTS "${CDRDAO_DIR}/cdrdao.exe")
    message(STATUS "Fetching cdrdao ${CDRDAO_VERSION} for bundling…")
    file(DOWNLOAD "${CDRDAO_URL}" "${CDRDAO_ZIP}"
        EXPECTED_HASH SHA256=${CDRDAO_SHA256}
        SHOW_PROGRESS STATUS _cdrdao_dl)
    list(GET _cdrdao_dl 0 _cdrdao_dl_code)
    if(NOT _cdrdao_dl_code EQUAL 0)
        list(GET _cdrdao_dl 1 _cdrdao_dl_msg)
        message(FATAL_ERROR "Could not download cdrdao: ${_cdrdao_dl_msg}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${CDRDAO_ZIP}" DESTINATION "${CDRDAO_DIR}")
endif()

set(CDRDAO_STAGE "")
foreach(_f ${CDRDAO_FILES})
    if(NOT EXISTS "${CDRDAO_DIR}/${_f}")
        message(FATAL_ERROR "cdrdao bundle is missing ${_f} — delete "
            "${CDRDAO_DIR} to re-fetch.")
    endif()
    list(APPEND CDRDAO_STAGE "${CDRDAO_DIR}/${_f}")
endforeach()

# Bundle ffmpeg/ffprobe (audio decoding + probing) the same way. We use the
# gyan.dev "essentials" static build — two self-contained exes, no DLLs — which
# already covers FLAC and the other formats the app hands to it. It is large
# (~100 MB per exe), which is why these were historically left out; bundling
# them means a working install with nothing for the user to fetch.
set(FFMPEG_VERSION "8.1.2")
set(FFMPEG_URL "https://github.com/GyanD/codexffmpeg/releases/download/8.1.2/ffmpeg-8.1.2-essentials_build.7z")
set(FFMPEG_SHA256 "e25b682664025d49034c981afb4bae36238a40f29a3cc1c713ad9a8b5b3528f6")
set(FFMPEG_ARCHIVE "${CMAKE_BINARY_DIR}/ffmpeg-${FFMPEG_VERSION}.7z")
set(FFMPEG_DIR "${CMAKE_BINARY_DIR}/ffmpeg-${FFMPEG_VERSION}")
# The archive unpacks into a versioned top folder with the exes under bin/.
set(FFMPEG_BIN "${FFMPEG_DIR}/ffmpeg-${FFMPEG_VERSION}-essentials_build/bin")
set(FFMPEG_FILES ffmpeg.exe ffprobe.exe)

if(NOT EXISTS "${FFMPEG_BIN}/ffmpeg.exe")
    message(STATUS "Fetching ffmpeg ${FFMPEG_VERSION} for bundling…")
    file(DOWNLOAD "${FFMPEG_URL}" "${FFMPEG_ARCHIVE}"
        EXPECTED_HASH SHA256=${FFMPEG_SHA256}
        SHOW_PROGRESS STATUS _ffmpeg_dl)
    list(GET _ffmpeg_dl 0 _ffmpeg_dl_code)
    if(NOT _ffmpeg_dl_code EQUAL 0)
        list(GET _ffmpeg_dl 1 _ffmpeg_dl_msg)
        message(FATAL_ERROR "Could not download ffmpeg: ${_ffmpeg_dl_msg}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${FFMPEG_ARCHIVE}" DESTINATION "${FFMPEG_DIR}")
endif()

set(FFMPEG_STAGE "")
foreach(_f ${FFMPEG_FILES})
    if(NOT EXISTS "${FFMPEG_BIN}/${_f}")
        message(FATAL_ERROR "ffmpeg bundle is missing ${_f} — delete "
            "${FFMPEG_DIR} to re-fetch.")
    endif()
    list(APPEND FFMPEG_STAGE "${FFMPEG_BIN}/${_f}")
endforeach()

get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(QT_BIN_DIR "${_qmake}" DIRECTORY)
find_program(WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt6 windeployqt
    HINTS "${QT_BIN_DIR}"
    REQUIRED)

add_custom_target(deploy ALL
    DEPENDS bincue-studio cdlabel
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:bincue-studio> $<TARGET_FILE:cdlabel> "${DIST_DIR}/"
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
        --no-translations --no-system-d3d-compiler --no-opengl-sw
        "${DIST_DIR}/bincue-studio.exe" "${DIST_DIR}/cdlabel.exe"
    # Bundled cdrdao + ffmpeg + their GPL notices, next to the app so burning
    # and audio decoding both work with nothing for the user to install.
    COMMAND ${CMAKE_COMMAND} -E copy ${CDRDAO_STAGE} ${FFMPEG_STAGE} "${DIST_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/installer/third-party/cdrdao-NOTICE.txt"
        "${CMAKE_SOURCE_DIR}/installer/third-party/ffmpeg-NOTICE.txt" "${DIST_DIR}/"
    # Host-setup helper for turning this machine into a remote burning host
    # (see docs/remote-burning.md); dropped next to the exes so it is one
    # `cd "$env:BINCUE_STUDIO_HOME"` away.
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/packaging/bincue-host-setup.ps1" "${DIST_DIR}/"
    COMMENT "Deploying executables and Qt DLLs to dist/"
    VERBATIM)

# windeployqt only covers Qt itself; the MinGW runtime and third-party DLLs
# (libstdc++, taglib, ...) are resolved through ldd. MSYS2-only, which is the
# supported Windows build environment.
#
# The sweep must include the Qt plugins windeployqt staged under dist/, not just
# the two exes: image-format plugins like imageformats/qjpeg.dll pull in
# libjpeg/libwebp/libtiff, which are reachable *only* through the plugin, never
# through the app exe. Missing those DLLs makes the plugin fail to load, so
# QImage decodes PNG (built into Qt6Gui) but silently returns null for JPEG &
# co. We scan every deployed exe/dll and loop to a fixpoint so a freshly copied
# dependency's own deps get pulled in too. Windows resolves a plugin's DLLs from
# the process exe's directory, so landing them all in dist/ is enough.
if(MINGW)
    add_custom_command(TARGET deploy POST_BUILD
        COMMAND bash -c "prev=-1; while true; do ldd $(find '${DIST_DIR}' -type f \\( -name '*.exe' -o -name '*.dll' \\)) 2>/dev/null | grep -oE '/(mingw64|ucrt64|clang64)/[^ ]+[.]dll' | sort -u | xargs -r cp -u -t '${DIST_DIR}'; n=$(find '${DIST_DIR}' -maxdepth 1 -name '*.dll' | wc -l); [ \"$n\" = \"$prev\" ] && break; prev=$n; done"
        COMMENT "Copying MinGW runtime + plugin DLLs to dist/"
        VERBATIM)
endif()

add_custom_target(installer
    COMMAND ${CMAKE_COMMAND}
        -DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DAPP_VERSION=${PROJECT_VERSION}
        -P "${CMAKE_SOURCE_DIR}/installer/build_installer.cmake"
    DEPENDS deploy
    COMMENT "Building Windows installer with Qt Installer Framework"
    VERBATIM)
