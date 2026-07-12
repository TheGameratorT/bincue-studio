# Windows-only deployment: gathers both executables plus their Qt and MinGW
# runtime DLLs into build/dist, and adds the `installer` target that packs
# dist into a QtIFW setup exe. Included from the top-level CMakeLists inside
# if(WIN32).

set(DIST_DIR "${CMAKE_BINARY_DIR}/dist")

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
    COMMENT "Deploying executables and Qt DLLs to dist/"
    VERBATIM)

# windeployqt only covers Qt itself; the MinGW runtime and third-party DLLs
# (libstdc++, taglib, ...) are resolved through ldd. MSYS2-only, which is the
# supported Windows build environment.
if(MINGW)
    add_custom_command(TARGET deploy POST_BUILD
        COMMAND bash -c "ldd '${DIST_DIR}/bincue-studio.exe' '${DIST_DIR}/cdlabel.exe' | grep -oE '/(mingw64|ucrt64|clang64)/[^ ]+[.]dll' | sort -u | xargs -r cp -u -t '${DIST_DIR}'"
        COMMENT "Copying MinGW runtime DLLs to dist/"
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
