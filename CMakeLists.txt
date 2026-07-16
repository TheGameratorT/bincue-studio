cmake_minimum_required(VERSION 3.21)
project(bincue-studio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

# Both executables land side by side, so bincue-studio finds cdlabel next to
# itself at runtime.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Shared "run a tool on a local or remote (SSH) host" library, shared with
# disc-damage-viewer. Provides hostkit::hostkit (HostSession, HostProcess,
# TerminalView) used by burning. It is vendored as a static library: dev builds
# use the sibling checkout, and the AUR PKGBUILD points BINCUE_HOSTKIT_SOURCE_DIR
# at the hostkit tarball it extracts alongside, so the app carries hostkit
# statically with no separate runtime package. Set BINCUE_USE_SYSTEM_HOSTKIT=ON
# to instead link an installed shared libhostkit.
option(BINCUE_USE_SYSTEM_HOSTKIT
    "Link the installed hostkit package instead of vendoring the source" OFF)
set(BINCUE_HOSTKIT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../hostkit"
    CACHE PATH "hostkit source tree to vendor (static link) when not using the system package")
if(NOT BINCUE_USE_SYSTEM_HOSTKIT
        AND EXISTS ${BINCUE_HOSTKIT_SOURCE_DIR}/CMakeLists.txt)
    add_subdirectory(${BINCUE_HOSTKIT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/hostkit)
else()
    find_package(hostkit 1 CONFIG REQUIRED)
endif()

# Windows has no system FFmpeg to link, so build a minimal audio-only one from
# source and put its .pc files on pkg-config's path. Must run before the src
# subdirectory, whose pkg_check_modules(LIBAV REQUIRED) resolves at configure
# time. On Linux the libraries come from the system ffmpeg package instead.
if(WIN32)
    include(installer/ffmpeg-audio.cmake)
endif()

add_subdirectory(src)      # the BinCue Studio application
add_subdirectory(cdlabel)  # the CD label editor (also builds standalone)

# Windows: `deploy` gathers both exes + runtime DLLs into build/dist,
# `installer` packs dist into an NSIS setup exe (see .github/workflows).
if(WIN32)
    include(installer/windows.cmake)
endif()

# Desktop integration: menu entries and icons for both executables. The
# binary names match the .desktop basenames so Wayland/X11 associate windows
# with the right icon without a StartupWMClass override.
install(FILES
    packaging/bincue-studio.desktop
    packaging/cdlabel.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES
    packaging/bincue-studio.svg
    packaging/cdlabel.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
