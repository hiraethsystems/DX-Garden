cmake_minimum_required(VERSION 3.16)

# Ensō Radio for PS Vita (the Linux app is in hiraethsystems/Enso-Radio-Linux):
#   Vita:  cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
#   Tests: cmake -S . -B build-test        (core/ unit tests, on Linux)
project(ensoradio C)

# Versions (README, "Versions"), in vita/:
#   VERSION  the release version, MAJOR.MINOR.PATCH, minor and patch 0-9.
#   BUILD    goes up by one for every build handed over for testing.
set(ENSO_APP_DIR "${CMAKE_SOURCE_DIR}/vita")
file(STRINGS "${ENSO_APP_DIR}/VERSION" ENSO_VERSION LIMIT_COUNT 1)
file(STRINGS "${ENSO_APP_DIR}/BUILD" ENSO_BUILD LIMIT_COUNT 1)
# Re-run CMake when either changes, so a bumped BUILD reaches the binary.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${ENSO_APP_DIR}/VERSION" "${ENSO_APP_DIR}/BUILD")
if(NOT ENSO_VERSION MATCHES "^([0-9]|[1-9][0-9])\\.([0-9])\\.([0-9])$")
    message(FATAL_ERROR "VERSION must be MAJOR.MINOR.PATCH with minor and patch 0-9, "
                        "e.g. 1.0.0 (the Vita's version field is XX.YY): '${ENSO_VERSION}'")
endif()
set(ENSO_MAJOR ${CMAKE_MATCH_1})
set(ENSO_MINOR ${CMAKE_MATCH_2})
set(ENSO_PATCH ${CMAKE_MATCH_3})
if(NOT ENSO_BUILD MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "BUILD must be a whole number: '${ENSO_BUILD}'")
endif()
# The Vita's APP_VER is XX.YY: 0.9.0 -> 00.90, 1.0.1 -> 01.01, 1.2.3 -> 01.23.
if(ENSO_MAJOR LESS 10)
    set(ENSO_SFO_VERSION "0${ENSO_MAJOR}.${ENSO_MINOR}${ENSO_PATCH}")
else()
    set(ENSO_SFO_VERSION "${ENSO_MAJOR}.${ENSO_MINOR}${ENSO_PATCH}")
endif()
# Test builds carry the build number in the file name; -DENSO_RELEASE=ON
# names the VPK for a release.
option(ENSO_RELEASE "Name the VPK for a release, without the build number" OFF)
if(ENSO_RELEASE)
    set(ENSO_VPK_NAME "ensoradio-${ENSO_VERSION}.vpk")
else()
    set(ENSO_VPK_NAME "ensoradio-${ENSO_VERSION}-b${ENSO_BUILD}.vpk")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CORE_SOURCES
    core/bidi_order.c
    core/config.c
    core/controls.c
    core/globe.c
    core/hls.c
    core/icy.c
    core/library.c
    core/load_report.c
    core/media_demux.c
    core/playlist.c
    core/ring_buffer.c
    core/search.c
    core/station.c
    core/stations_file.c
    core/stream_buffer.c
    core/stream_info.c
    core/strmap.c
    core/text.c
    core/track_info.c
    core/userlist.c
)

if(VITA)
    include("${VITASDK}/share/vita.cmake" REQUIRED)

    set(VITA_APP_NAME "Ensō Radio")
    set(VITA_TITLEID  "DXGA00001")

    add_executable(ensoradio
        ${CORE_SOURCES}
        vita/app.c
        vita/data.c
        vita/font.c
        vita/fs.c
        vita/gfx.c
        vita/gfx_fallback.c
        vita/globe_view.c
        vita/ime.c
        vita/log.c
        vita/main.c
        vita/player.c
    )
    target_include_directories(ensoradio PRIVATE core "${VITASDK}/arm-vita-eabi/include/freetype2"
                               "${VITASDK}/arm-vita-eabi/include/harfbuzz"
                               "${VITASDK}/arm-vita-eabi/include/fribidi"
                               "${CMAKE_BINARY_DIR}/generated")
    # revision.h: the git commit, refreshed on every build (shown in About).
    add_custom_target(enso_revision
        COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR}
                -DOUT=${CMAKE_BINARY_DIR}/generated/revision.h
                -P ${CMAKE_SOURCE_DIR}/cmake/revision.cmake
        BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/revision.h)
    add_dependencies(ensoradio enso_revision)
    target_compile_definitions(ensoradio PRIVATE ENSO_VERSION="${ENSO_VERSION}"
                               ENSO_BUILD="${ENSO_BUILD}")
    # vita/ uses POSIX bits (strncasecmp) from newlib.
    set_target_properties(ensoradio PROPERTIES C_EXTENSIONS ON)
    target_compile_options(ensoradio PRIVATE -Wall -Wextra -O2)
    # Keeps room after the code segment for vita-elf-create (see the file).
    target_link_options(ensoradio PRIVATE "-Wl,-T,${CMAKE_SOURCE_DIR}/cmake/vita-gap.ld")

    target_link_libraries(ensoradio
        vitaGL vitashark SceShaccCgExt mathneon taihen_stub SceShaccCg_stub
        vita2d
        curl mbedtls mbedx509 mbedcrypto zstd
        mpg123
        harfbuzz fribidi freetype bz2 png jpeg z m
        pthread
        SceAppMgr_stub
        SceAppUtil_stub
        SceAudio_stub
        SceCommonDialog_stub
        SceCtrl_stub
        SceDisplay_stub
        SceGxm_stub
        SceIme_stub
        SceKernelDmacMgr_stub
        SceNet_stub
        SceNetCtl_stub
        ScePgf_stub
        ScePower_stub
        SceRtc_stub
        SceSysmodule_stub
        SceTouch_stub
        stdc++
    )

    vita_create_self(eboot.bin ensoradio)
    vita_create_vpk(${ENSO_VPK_NAME} ${VITA_TITLEID} eboot.bin
        VERSION ${ENSO_SFO_VERSION}
        NAME ${VITA_APP_NAME}
        FILE sce_sys/icon0.png sce_sys/icon0.png
        FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
        FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
        FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
        FILE assets/cacert.pem cacert.pem
        FILE assets/stations/stations.txt stations.txt
        FILE assets/fonts/NotoSans-Regular.ttf fonts/NotoSans-Regular.ttf
        FILE assets/fonts/NotoSans-Bold.ttf fonts/NotoSans-Bold.ttf
        FILE assets/fonts/OFL.txt fonts/OFL.txt
        FILE assets/fonts/NotoSansArabic-Regular.ttf fonts/NotoSansArabic-Regular.ttf
        FILE assets/fonts/NotoSansHebrew-Regular.ttf fonts/NotoSansHebrew-Regular.ttf
        FILE assets/fonts/NotoSansThai-Regular.ttf fonts/NotoSansThai-Regular.ttf
        FILE assets/fonts/NotoSansGeorgian-Regular.ttf fonts/NotoSansGeorgian-Regular.ttf
        FILE assets/fonts/NotoSansEthiopic-Regular.ttf fonts/NotoSansEthiopic-Regular.ttf
        FILE assets/fonts/NotoSansCJKsc-Subset.otf fonts/NotoSansCJKsc-Subset.otf
        FILE assets/map/world.png map/world.png
        FILE assets/map/lines.bin map/lines.bin
    )
else()
    # core/'s unit tests, on Linux.
    add_executable(ensoradio_test
        ${CORE_SOURCES}
        tests/test_main.c
        tests/test_library.c
        tests/test_globe.c
        tests/test_controls.c
        tests/test_stationfiles.c
        tests/test_bidi.c
        tests/test_stream.c
        tests/test_hls.c
    )
    target_include_directories(ensoradio_test PRIVATE core)
    target_compile_definitions(ensoradio_test PRIVATE
                               ENSO_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/testdata")
    target_compile_options(ensoradio_test PRIVATE -Wall -Wextra -Wpedantic -g)
    target_compile_options(ensoradio_test PRIVATE -fsanitize=address,undefined)
    target_link_options(ensoradio_test PRIVATE -fsanitize=address,undefined)
    target_link_libraries(ensoradio_test m)

    enable_testing()
    add_test(NAME core COMMAND ensoradio_test)
endif()
