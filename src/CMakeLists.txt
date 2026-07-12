# TagLib fills track/album metadata (Title/Performer/Songwriter/ISRC and the
# album-wide fields) from the audio files' tags. Optional: when absent, track
# titles are seeded from filenames only.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(TAGLIB IMPORTED_TARGET taglib>=2.0)
endif()

add_executable(bincue-studio
    main.cpp
    redbook.cpp
    track.cpp
    tagreader.cpp
    exportworker.cpp
    burncontroller.cpp
    burnjob.cpp
    burndialog.cpp
    mainwindow.cpp
)

target_link_libraries(bincue-studio PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets hostkit::hostkit)

# GUI subsystem (no console window) and the embedded exe icon.
if(WIN32)
    set_target_properties(bincue-studio PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(bincue-studio PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/bincue-studio.rc)
endif()

# Single source of truth for the version shown in the About dialog.
target_compile_definitions(bincue-studio PRIVATE
    BINCUE_VERSION="${PROJECT_VERSION}")

# The desktop-entry icons, embedded so uninstalled builds have them too
# (cdlabel's is shown on the Create Label action). Rendering them needs
# Qt's SVG plugin at runtime (qt6-svg on Arch).
qt_add_resources(bincue-studio "appicon"
    PREFIX "/icons"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}/../packaging
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/bincue-studio.svg
        ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/cdlabel.svg)

# In-app action/dialog icons, same visual family as the desktop-entry ones.
qt_add_resources(bincue-studio "actionicons"
    PREFIX "/icons"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}/icons
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/icons/export.svg
        ${CMAKE_CURRENT_SOURCE_DIR}/icons/burn.svg)

if(TAGLIB_FOUND)
    target_link_libraries(bincue-studio PRIVATE PkgConfig::TAGLIB)
    target_compile_definitions(bincue-studio PRIVATE HAVE_TAGLIB=1)
    message(STATUS "TagLib found — tag import enabled in bincue-studio")
else()
    message(WARNING "TagLib not found — tag import disabled in bincue-studio")
endif()

install(TARGETS bincue-studio RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
