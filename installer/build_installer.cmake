# Script-mode CMake (invoked by the `installer` target): stage the QtIFW
# input tree inside the build directory and run binarycreator on it. Nothing
# is written back into the source tree.
#
# Required -D variables: CMAKE_BINARY_DIR, SOURCE_DIR, APP_VERSION.

cmake_minimum_required(VERSION 3.21)

foreach(var CMAKE_BINARY_DIR SOURCE_DIR APP_VERSION)
    if(NOT ${var})
        message(FATAL_ERROR "${var} must be set when running this script")
    endif()
endforeach()

# On MSYS2 binarycreator comes from mingw-w64-x86_64-qt-installer-framework;
# elsewhere point QTIFWDIR at a Qt Installer Framework install.
find_program(BINARYCREATOR_EXECUTABLE
    NAMES binarycreator binarycreator.exe
    PATHS ENV QTIFWDIR
    PATH_SUFFIXES bin
    DOC "Qt Installer Framework binarycreator executable")
if(NOT BINARYCREATOR_EXECUTABLE)
    message(FATAL_ERROR "binarycreator not found — install Qt Installer "
        "Framework and set the QTIFWDIR environment variable")
endif()

set(DIST_DIR "${CMAKE_BINARY_DIR}/dist")
set(STAGING "${CMAKE_BINARY_DIR}/installer-staging")
set(PACKAGE "${STAGING}/packages/com.TheGameratorT.bincuestudio")

if(NOT EXISTS "${DIST_DIR}/bincue-studio.exe")
    message(FATAL_ERROR "dist/ is not populated — build the `deploy` target first")
endif()

file(REMOVE_RECURSE "${STAGING}")
file(MAKE_DIRECTORY "${STAGING}/config" "${PACKAGE}/meta")

# The package payload is the deployed dist/ tree (both exes + Qt/MinGW DLLs).
file(COPY "${DIST_DIR}/" DESTINATION "${PACKAGE}/data")

# Configure the XML templates. @ApplicationsDir@ is a QtIFW runtime variable,
# not ours — pin it to itself so configure_file passes it through instead of
# erasing it.
set(ApplicationsDir "@ApplicationsDir@")
set(PROJECT_VERSION "${APP_VERSION}")
string(TIMESTAMP RELEASE_DATE "%Y-%m-%d")
configure_file("${SOURCE_DIR}/installer/config/config.xml.in"
    "${STAGING}/config/config.xml" @ONLY)
configure_file("${SOURCE_DIR}/installer/packages/com.TheGameratorT.bincuestudio/meta/package.xml.in"
    "${PACKAGE}/meta/package.xml" @ONLY)

file(COPY "${SOURCE_DIR}/installer/packages/com.TheGameratorT.bincuestudio/meta/installscript.qs"
    DESTINATION "${PACKAGE}/meta")
configure_file("${SOURCE_DIR}/LICENSE" "${PACKAGE}/meta/license.txt" COPYONLY)

# Installer branding referenced from config.xml.
configure_file("${SOURCE_DIR}/packaging/bincue-studio.ico"
    "${STAGING}/config/bincue-studio.ico" COPYONLY)
configure_file("${SOURCE_DIR}/packaging/installer-icon.png"
    "${STAGING}/config/installer-icon.png" COPYONLY)

set(OUTPUT "${CMAKE_BINARY_DIR}/BinCueStudio-${APP_VERSION}-Setup.exe")
message(STATUS "Creating installer ${OUTPUT}")
execute_process(
    COMMAND "${BINARYCREATOR_EXECUTABLE}"
        --offline-only
        -c "${STAGING}/config/config.xml"
        -p "${STAGING}/packages"
        "${OUTPUT}"
    RESULT_VARIABLE INSTALLER_RESULT)
if(NOT INSTALLER_RESULT EQUAL 0)
    message(FATAL_ERROR "binarycreator failed with exit code ${INSTALLER_RESULT}")
endif()
message(STATUS "Installer created: ${OUTPUT}")
