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

# The desktop-entry icon, embedded so uninstalled builds have it too.
# Rendering it needs Qt's SVG plugin at runtime (qt6-svg on Arch).
qt_add_resources(cdlabel "appicon"
    PREFIX "/icons"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}/../packaging
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/cdlabel.svg)

if(TAGLIB_FOUND)
    target_link_libraries(cdlabel PRIVATE PkgConfig::TAGLIB)
    target_compile_definitions(cdlabel PRIVATE HAVE_TAGLIB=1)
    message(STATUS "TagLib found — embedded cover-art extraction enabled")
else()
    message(WARNING "TagLib not found — only sidecar cover images will be used")
endif()

install(TARGETS cdlabel RUNTIME DESTINATION bin)
