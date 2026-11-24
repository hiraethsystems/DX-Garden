# The Linux app (linux/), included from CMakeLists.txt.
#
#   cmake -S . -B build-linux -G Ninja -DDXG_LINUX=ON
#   cmake --install build-linux --prefix ~/.local   (optional)
#   AppImage: tools/make_appimage.sh (a build with -DDXG_STATIC_DEPS=ON)
#
# Run tools/build_linux_deps.sh first (linux; the AppImage also
# linux-static).
set(DXG_LINUX_DEPS "${CMAKE_SOURCE_DIR}/third_party/linux" CACHE PATH "Linux dependency prefix")
set(DXG_LINUX_STATIC_DEPS "${CMAKE_SOURCE_DIR}/third_party/linux-static" CACHE PATH
    "Linux static dependency prefix (DXG_STATIC_DEPS)")
option(DXG_STATIC_DEPS "Link curl, mpg123 and the text libraries statically (the AppImage)" OFF)
list(PREPEND CMAKE_PREFIX_PATH "${DXG_LINUX_DEPS}")

find_package(SDL3 CONFIG REQUIRED COMPONENTS SDL3-static)
find_package(PkgConfig REQUIRED)
if(DXG_STATIC_DEPS)
    # Everything from third_party/linux-static/, so the program needs only
    # glibc and what SDL loads at run time.
    set(dxg_l "${DXG_LINUX_STATIC_DEPS}/lib")
    foreach(lib curl mbedtls mbedx509 mbedcrypto everest p256m mpg123 faad vorbisfile vorbis
                opusfile opus FLAC ogg z freetype harfbuzz fribidi)
        if(NOT EXISTS "${dxg_l}/lib${lib}.a")
            message(FATAL_ERROR "${dxg_l}/lib${lib}.a is missing: run tools/build_linux_deps.sh linux-static")
        endif()
    endforeach()
    set(DXG_CURL "${dxg_l}/libcurl.a" "${dxg_l}/libmbedtls.a" "${dxg_l}/libmbedx509.a"
        "${dxg_l}/libmbedcrypto.a" "${dxg_l}/libeverest.a" "${dxg_l}/libp256m.a" pthread)
    set(DXG_MPG123 "${dxg_l}/libmpg123.a" "${dxg_l}/libfaad.a" "${dxg_l}/libvorbisfile.a"
        "${dxg_l}/libvorbis.a" "${dxg_l}/libopusfile.a" "${dxg_l}/libopus.a" "${dxg_l}/libFLAC.a"
        "${dxg_l}/libogg.a" "${dxg_l}/libz.a")
    set(DXG_TEXT "${dxg_l}/libharfbuzz.a" "${dxg_l}/libfreetype.a" "${dxg_l}/libfribidi.a")
    set(DXG_TEXT_INCLUDES "${DXG_LINUX_STATIC_DEPS}/include"
        "${DXG_LINUX_STATIC_DEPS}/include/freetype2" "${DXG_LINUX_STATIC_DEPS}/include/harfbuzz"
        "${DXG_LINUX_STATIC_DEPS}/include/fribidi" "${DXG_LINUX_STATIC_DEPS}/include/opus")
    set(DXG_PLATFORM_NAME "Linux")
else()
    pkg_check_modules(DXG_PC REQUIRED IMPORTED_TARGET libcurl libmpg123 vorbisfile opusfile flac zlib)
    find_library(FAAD_LIB NAMES faad REQUIRED)
    pkg_check_modules(DXG_TEXT_PC REQUIRED IMPORTED_TARGET freetype2 harfbuzz fribidi)
    set(DXG_CURL PkgConfig::DXG_PC)
    set(DXG_MPG123 ${FAAD_LIB})
    set(DXG_TEXT PkgConfig::DXG_TEXT_PC)
    set(DXG_TEXT_INCLUDES "")
    set(DXG_PLATFORM_NAME "Linux")
endif()

add_executable(dxgarden_linux
    ${CORE_SOURCES}
    ${RB_SOURCES}
    linux/app.c
    linux/list_download.c
    linux/list_dialog.c
    linux/data.c
    linux/decode.c
    linux/draw.c
    linux/earth.c
    linux/font.c
    linux/fs.c
    linux/gallery.c
    linux/gl.c
    linux/globe_view.c
    linux/icons.c
    linux/library_view.c
    linux/log.c
    linux/main.c
    linux/player.c
    linux/theme.c
    linux/ui.c
    linux/ui_widgets.c
)
set_target_properties(dxgarden_linux PROPERTIES OUTPUT_NAME dxgarden C_EXTENSIONS ON)
target_include_directories(dxgarden_linux PRIVATE core linux ${DXG_TEXT_INCLUDES})
target_compile_definitions(dxgarden_linux PRIVATE DXG_VERSION="${DXG_VERSION}"
                           DXG_BUILD="${DXG_BUILD}" DXG_PLATFORM_NAME="${DXG_PLATFORM_NAME}"
                           "STATION_FILE_MAX=(32u * 1024 * 1024)")
target_compile_options(dxgarden_linux PRIVATE -Wall -Wextra -O2 -g)
target_link_libraries(dxgarden_linux PRIVATE SDL3::SDL3-static ${DXG_CURL} ${DXG_MPG123}
                      ${DXG_TEXT} m)
if(DXG_STATIC_DEPS)
    # HarfBuzz is C++: its runtime goes in too, so the program doesn't need
    # this machine's libstdc++.
    target_compile_definitions(dxgarden_linux PRIVATE CURL_STATICLIB FRIBIDI_LIB_STATIC
                               FLAC__NO_DLL DXG_STATIC_DEPS)
    target_link_options(dxgarden_linux PRIVATE -static-libstdc++ -static-libgcc)
    set_target_properties(dxgarden_linux PROPERTIES LINKER_LANGUAGE CXX)
    enable_language(CXX)
endif()

# What the app reads at run time, next to the executable: fonts/ and the
# bundled station list.
set(DXG_LINUX_FONTS
    LiberationSans-Regular.ttf LiberationSans-Bold.ttf OFL-Liberation.txt
    NotoSans-Regular.ttf NotoSans-Bold.ttf NotoSansArabic-Regular.ttf
    NotoSansHebrew-Regular.ttf NotoSansThai-Regular.ttf NotoSansGeorgian-Regular.ttf
    NotoSansEthiopic-Regular.ttf NotoSansCJKsc-Subset.otf OFL.txt)
foreach(f ${DXG_LINUX_FONTS})
    add_custom_command(TARGET dxgarden_linux POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/assets/fonts/${f}" "$<TARGET_FILE_DIR:dxgarden_linux>/fonts/${f}")
endforeach()
add_custom_command(TARGET dxgarden_linux POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/stations/linux/stations.txt"
            "$<TARGET_FILE_DIR:dxgarden_linux>/stations.txt")
# The window icon (X11 title bars and task switchers).
add_custom_command(TARGET dxgarden_linux POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/linux/share/club.hiraeth.dxgarden.png"
            "$<TARGET_FILE_DIR:dxgarden_linux>/icon.png")
# The globe's imagery, built by tools/make_earth.py (not in git: 70 MB).
if(EXISTS "${CMAKE_SOURCE_DIR}/assets-build/earth-day.pack")
    add_custom_command(TARGET dxgarden_linux POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/assets-build/earth-day.pack"
                "$<TARGET_FILE_DIR:dxgarden_linux>/earth-day.pack")
else()
    message(WARNING "assets-build/earth-day.pack is missing: run tools/make_earth.py "
                    "for the globe's imagery")
endif()

# Linux install layout (also the AppImage's): bin/dxgarden, the files it
# reads in share/club.hiraeth.dxgarden/ (found from bin/), and a
# menu entry and icon named after the app ID SDL gives Wayland.
include(GNUInstallDirs)
install(TARGETS dxgarden_linux RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# Not share/dxgarden/: with --prefix ~/.local that is the user's data folder.
set(DXG_SHARE "${CMAKE_INSTALL_DATADIR}/club.hiraeth.dxgarden")
foreach(f ${DXG_LINUX_FONTS})
    install(FILES "${CMAKE_SOURCE_DIR}/assets/fonts/${f}" DESTINATION "${DXG_SHARE}/fonts")
endforeach()
install(FILES "${CMAKE_SOURCE_DIR}/assets/stations/linux/stations.txt" DESTINATION "${DXG_SHARE}")
install(FILES linux/share/club.hiraeth.dxgarden.png DESTINATION "${DXG_SHARE}"
        RENAME icon.png)
if(EXISTS "${CMAKE_SOURCE_DIR}/assets-build/earth-day.pack")
    install(FILES "${CMAKE_SOURCE_DIR}/assets-build/earth-day.pack" DESTINATION "${DXG_SHARE}")
endif()
# The menu entry runs the program by its full path: desktops don't
# always have ~/.local/bin on their PATH (Plasma on Debian doesn't).
# Written at install time, so `cmake --install --prefix` is honoured.
install(CODE "
    set(dxg_bin \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/dxgarden\")
    file(READ \"${CMAKE_SOURCE_DIR}/linux/share/club.hiraeth.dxgarden.desktop\" dxg_entry)
    string(REPLACE \"Exec=dxgarden\" \"Exec=\\\"\${dxg_bin}\\\"\" dxg_entry \"\${dxg_entry}\")
    set(dxg_out \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/applications/club.hiraeth.dxgarden.desktop\")
    message(STATUS \"Installing: \${dxg_out}\")
    file(WRITE \"\${dxg_out}\" \"\${dxg_entry}\")
")
install(FILES linux/share/club.hiraeth.dxgarden.png
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps")
