# The desktop app (desktop/), included from CMakeLists.txt.
#
#   Windows: cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake
#   Linux:   cmake -S . -B build-linux -G Ninja -DDXG_DESKTOP=ON
#            cmake --install build-linux --prefix ~/.local   (optional)
#
# Both need tools/build_win_deps.sh run first (win64 or linux).
set(DXG_LINUX_DEPS "${CMAKE_SOURCE_DIR}/third_party/linux" CACHE PATH "Linux dependency prefix")
if(NOT WIN32)
    list(PREPEND CMAKE_PREFIX_PATH "${DXG_LINUX_DEPS}")
endif()

find_package(SDL3 CONFIG REQUIRED COMPONENTS SDL3-static)
find_package(PkgConfig REQUIRED)
if(WIN32)
    # Static libraries only: the .exe ships without DLLs.
    find_package(CURL CONFIG REQUIRED)
    set(DXG_CURL CURL::libcurl_static)
    find_library(MPG123_LIB NAMES libmpg123.a mpg123 REQUIRED)
    set(DXG_MPG123 ${MPG123_LIB} shlwapi)
    find_library(FREETYPE_LIB NAMES libfreetype.a REQUIRED)
    find_library(HARFBUZZ_LIB NAMES libharfbuzz.a REQUIRED)
    find_library(FRIBIDI_LIB NAMES libfribidi.a REQUIRED)
    # HarfBuzz is C++; with -static the C++ runtime links in too.
    set(DXG_TEXT ${HARFBUZZ_LIB} ${FREETYPE_LIB} ${FRIBIDI_LIB} stdc++)
    set(DXG_TEXT_INCLUDES "${DXG_WIN_DEPS}/include/freetype2" "${DXG_WIN_DEPS}/include/harfbuzz"
        "${DXG_WIN_DEPS}/include/fribidi")
    set(DXG_PLATFORM_NAME "Windows")
else()
    pkg_check_modules(DXG_PC REQUIRED IMPORTED_TARGET libcurl libmpg123)
    pkg_check_modules(DXG_TEXT_PC REQUIRED IMPORTED_TARGET freetype2 harfbuzz fribidi)
    set(DXG_CURL PkgConfig::DXG_PC)
    set(DXG_MPG123 "")
    set(DXG_TEXT PkgConfig::DXG_TEXT_PC)
    set(DXG_TEXT_INCLUDES "")
    set(DXG_PLATFORM_NAME "Linux")
endif()

add_executable(dxgarden_desktop WIN32
    ${CORE_SOURCES}
    desktop/app.c
    desktop/data.c
    desktop/draw.c
    desktop/earth.c
    desktop/font.c
    desktop/fs.c
    desktop/gallery.c
    desktop/gl.c
    desktop/globe_view.c
    desktop/icons.c
    desktop/library_view.c
    desktop/log.c
    desktop/main.c
    desktop/player.c
    desktop/theme.c
    desktop/ui.c
    desktop/ui_widgets.c
)
set_target_properties(dxgarden_desktop PROPERTIES OUTPUT_NAME dxgarden C_EXTENSIONS ON)
target_include_directories(dxgarden_desktop PRIVATE core desktop ${DXG_TEXT_INCLUDES})
target_compile_definitions(dxgarden_desktop PRIVATE DXG_VERSION="${DXG_VERSION}"
                           DXG_BUILD="${DXG_BUILD}" DXG_PLATFORM_NAME="${DXG_PLATFORM_NAME}")
target_compile_options(dxgarden_desktop PRIVATE -Wall -Wextra -O2 -g)
target_link_libraries(dxgarden_desktop PRIVATE SDL3::SDL3-static ${DXG_CURL} ${DXG_MPG123}
                      ${DXG_TEXT} m)
if(WIN32)
    target_compile_definitions(dxgarden_desktop PRIVATE CURL_STATICLIB WIN32_LEAN_AND_MEAN
                               FRIBIDI_LIB_STATIC)
    enable_language(RC)
    target_sources(dxgarden_desktop PRIVATE desktop/dxgarden.rc)
    set_property(SOURCE desktop/dxgarden.rc APPEND PROPERTY OBJECT_DEPENDS
                 "${CMAKE_SOURCE_DIR}/desktop/dxgarden.manifest")
endif()

# What the app reads at run time, next to the executable: fonts/ and the
# bundled station list.
set(DXG_DESKTOP_FONTS
    LiberationSans-Regular.ttf LiberationSans-Bold.ttf OFL-Liberation.txt
    NotoSans-Regular.ttf NotoSans-Bold.ttf NotoSansArabic-Regular.ttf
    NotoSansHebrew-Regular.ttf NotoSansThai-Regular.ttf NotoSansGeorgian-Regular.ttf
    NotoSansEthiopic-Regular.ttf NotoSansCJKsc-Subset.otf OFL.txt)
foreach(f ${DXG_DESKTOP_FONTS})
    add_custom_command(TARGET dxgarden_desktop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/assets/fonts/${f}" "$<TARGET_FILE_DIR:dxgarden_desktop>/fonts/${f}")
endforeach()
add_custom_command(TARGET dxgarden_desktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/stations/stations.txt"
            "$<TARGET_FILE_DIR:dxgarden_desktop>/stations.txt")
if(NOT WIN32)
    # The window icon (X11 title bars and task switchers).
    add_custom_command(TARGET dxgarden_desktop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/desktop/linux/club.hiraeth.dxgarden.png"
                "$<TARGET_FILE_DIR:dxgarden_desktop>/icon.png")
endif()
# The globe's imagery, built by tools/make_earth.py (not in git: 70 MB).
if(EXISTS "${CMAKE_SOURCE_DIR}/assets-build/earth-day.pack")
    add_custom_command(TARGET dxgarden_desktop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/assets-build/earth-day.pack"
                "$<TARGET_FILE_DIR:dxgarden_desktop>/earth-day.pack")
else()
    message(WARNING "assets-build/earth-day.pack is missing: run tools/make_earth.py "
                    "for the globe's imagery")
endif()

# Linux install layout (also the AppImage's): bin/dxgarden, the files it
# reads in share/club.hiraeth.dxgarden/ (found from bin/), and a
# menu entry and icon named after the app ID SDL gives Wayland.
if(NOT WIN32)
    include(GNUInstallDirs)
    install(TARGETS dxgarden_desktop RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    # Not share/dxgarden/: with --prefix ~/.local that is the user's data folder.
    set(DXG_SHARE "${CMAKE_INSTALL_DATADIR}/club.hiraeth.dxgarden")
    foreach(f ${DXG_DESKTOP_FONTS})
        install(FILES "${CMAKE_SOURCE_DIR}/assets/fonts/${f}" DESTINATION "${DXG_SHARE}/fonts")
    endforeach()
    install(FILES "${CMAKE_SOURCE_DIR}/assets/stations/stations.txt" DESTINATION "${DXG_SHARE}")
    install(FILES desktop/linux/club.hiraeth.dxgarden.png DESTINATION "${DXG_SHARE}"
            RENAME icon.png)
    if(EXISTS "${CMAKE_SOURCE_DIR}/assets-build/earth-day.pack")
        install(FILES "${CMAKE_SOURCE_DIR}/assets-build/earth-day.pack" DESTINATION "${DXG_SHARE}")
    endif()
    # The menu entry runs the program by its full path: desktops don't
    # always have ~/.local/bin on their PATH (Plasma on Debian doesn't).
    # Written at install time, so `cmake --install --prefix` is honoured.
    install(CODE "
        set(dxg_bin \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/dxgarden\")
        file(READ \"${CMAKE_SOURCE_DIR}/desktop/linux/club.hiraeth.dxgarden.desktop\" dxg_entry)
        string(REPLACE \"Exec=dxgarden\" \"Exec=\\\"\${dxg_bin}\\\"\" dxg_entry \"\${dxg_entry}\")
        set(dxg_out \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/applications/club.hiraeth.dxgarden.desktop\")
        message(STATUS \"Installing: \${dxg_out}\")
        file(WRITE \"\${dxg_out}\" \"\${dxg_entry}\")
    ")
    install(FILES desktop/linux/club.hiraeth.dxgarden.png
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps")
endif()
