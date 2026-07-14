# Script-mode CMake (invoked by the `installer` target): run makensis on the
# NSIS script with every path/version passed in as a /D define. Nothing is
# written back into the source tree — the Setup exe lands in the build dir.
#
# Required -D variables: CMAKE_BINARY_DIR, SOURCE_DIR, APP_VERSION.

cmake_minimum_required(VERSION 3.21)

foreach(var CMAKE_BINARY_DIR SOURCE_DIR APP_VERSION)
    if(NOT ${var})
        message(FATAL_ERROR "${var} must be set when running this script")
    endif()
endforeach()

# On MSYS2 makensis comes from mingw-w64-x86_64-nsis; elsewhere it is on PATH
# once the Nullsoft Scriptable Install System is installed.
find_program(MAKENSIS_EXECUTABLE
    NAMES makensis makensis.exe
    DOC "NSIS makensis compiler")
if(NOT MAKENSIS_EXECUTABLE)
    message(FATAL_ERROR "makensis not found — install NSIS "
        "(MSYS2: pacman -S mingw-w64-x86_64-nsis)")
endif()

set(DIST_DIR "${CMAKE_BINARY_DIR}/dist")
if(NOT EXISTS "${DIST_DIR}/bincue-studio.exe")
    message(FATAL_ERROR "dist/ is not populated — build the `deploy` target first")
endif()

set(OUTPUT "${CMAKE_BINARY_DIR}/BinCueStudio-${APP_VERSION}-Setup.exe")

# VIProductVersion needs four fields; pad x.y.z out to x.y.z.0.
set(VIVERSION "${APP_VERSION}.0")

# makensis on Windows wants native (backslash) paths in its /D defines.
file(TO_NATIVE_PATH "${DIST_DIR}" DIST_DIR_NATIVE)
file(TO_NATIVE_PATH "${SOURCE_DIR}/LICENSE" LICENSE_NATIVE)
file(TO_NATIVE_PATH "${SOURCE_DIR}/packaging/bincue-studio.ico" ICON_NATIVE)
file(TO_NATIVE_PATH "${OUTPUT}" OUTPUT_NATIVE)

message(STATUS "Creating installer ${OUTPUT}")
execute_process(
    COMMAND "${MAKENSIS_EXECUTABLE}"
        "/DVERSION=${APP_VERSION}"
        "/DVIVERSION=${VIVERSION}"
        "/DDIST_DIR=${DIST_DIR_NATIVE}"
        "/DLICENSE_FILE=${LICENSE_NATIVE}"
        "/DICON=${ICON_NATIVE}"
        "/DOUTFILE=${OUTPUT_NATIVE}"
        "${SOURCE_DIR}/installer/bincue-studio.nsi"
    RESULT_VARIABLE INSTALLER_RESULT)
if(NOT INSTALLER_RESULT EQUAL 0)
    message(FATAL_ERROR "makensis failed with exit code ${INSTALLER_RESULT}")
endif()
message(STATUS "Installer created: ${OUTPUT}")
