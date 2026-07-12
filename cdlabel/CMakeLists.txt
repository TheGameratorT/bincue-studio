cmake_minimum_required(VERSION 3.21)
project(cdlabel VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# TagLib pulls embedded cover art out of the audio files' tags. Optional: when
# absent, only sidecar images (cover.jpg beside the audio) are found.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(TAGLIB IMPORTED_TARGET taglib>=2.0)
endif()

add_executable(cdlabel
    src/main.cpp
    src/labelconfig.cpp
    src/labelproject.cpp
    src/covers.cpp
    src/render.cpp
    src/rendertext.cpp
    src/previewwidget.cpp
    src/colorbutton.cpp
    src/labeldialog.cpp
    src/labeldialog_state.cpp
)

target_link_libraries(cdlabel PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

# GUI subsystem (no console window) and the embedded exe icon. Note this
# silences --render's stdout progress on Windows; the PNG is still written.
if(WIN32)
    set_target_properties(cdlabel PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(cdlabel PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/cdlabel.rc)
endif()

# The desktop-entry icon, embedded so uninstalled builds have it too.
# Rendering it needs Qt's SVG plugin at runtime (qt6-svg on Arch).
qt_add_resources(cdlabel "appicon"
    PREFIX "/icons"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}/../packaging
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/cdlabel.svg)

if(TAGLIB_FOUND)
    target_link_libraries(cdlabel PRIVATE PkgConfig::TAGLIB)
    target_compile_definitions(cdlabel PRIVATE HAVE_TAGLIB=1)
    # Shared header-only helper for opening Unicode paths with TagLib (see
    # common/taglibpath.h); the same file backs bincue-studio's tag reader.
    target_include_directories(cdlabel PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../common)
    message(STATUS "TagLib found — embedded cover-art extraction enabled")
else()
    message(WARNING "TagLib not found — only sidecar cover images will be used")
endif()

install(TARGETS cdlabel RUNTIME DESTINATION bin)
