#!/bin/sh
# Builds the libraries the Linux app needs, from their official source
# releases, as static libraries:
#
#   third_party/linux/   SDL3 only, for the development build, so it uses
#                        the same SDL as the AppImage. The rest come from the
#                        distribution's -dev packages.
#   third_party/linux-static/
#                        curl (HTTP and HTTPS only, with mbedTLS), mpg123,
#                        faad2 (AAC), Ogg, Vorbis, Opus, opusfile, FLAC, zlib,
#                        FreeType, HarfBuzz and FriBidi for the AppImage, so
#                        it needs nothing from the system but glibc and what
#                        SDL loads at run time (graphics, sound, input).
#
# Usage: tools/build_linux_deps.sh [linux|linux-static|linux-decoders|zlib|all]
#        (default: all)
# Needs: cmake, ninja-build, meson, curl, sha256sum.
# Downloads are checked against the SHA-256 sums below.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TP="$ROOT/third_party"
SRC="$TP/src"
LNX="$TP/linux"
LST="$TP/linux-static"
JOBS=$(nproc 2>/dev/null || echo 4)

SDL_VER=3.4.18
CURL_VER=8.23.0
MPG123_VER=1.33.7
FREETYPE_VER=2.15.0
HARFBUZZ_VER=15.0.0
FRIBIDI_VER=1.0.18
MBEDTLS_VER=3.6.7
FAAD2_VER=2.11.4
OGG_VER=1.3.6
VORBIS_VER=1.3.7
OPUS_VER=1.6.1
OPUSFILE_VER=0.12
FLAC_VER=1.5.0
ZLIB_VER=1.3.1

# name|url|sha256
SOURCES="
SDL3-$SDL_VER.tar.gz|https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL3-$SDL_VER.tar.gz|9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
curl-$CURL_VER.tar.xz|https://curl.se/download/curl-$CURL_VER.tar.xz|8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5
mpg123-$MPG123_VER.tar.bz2|https://www.mpg123.de/download/mpg123-$MPG123_VER.tar.bz2|31d0e35a4ca567ec9b5ebda6c3062bb4435d6d3eacd6ef0d95cadd7854dc03ee
freetype-$FREETYPE_VER.tar.xz|https://download.savannah.gnu.org/releases/freetype/freetype-$FREETYPE_VER.tar.xz|93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5
harfbuzz-$HARFBUZZ_VER.tar.xz|https://github.com/harfbuzz/harfbuzz/releases/download/$HARFBUZZ_VER/harfbuzz-$HARFBUZZ_VER.tar.xz|968536e9abd782f07f7954044c0c4020199fcdd39b41d221ec42d2a3be4a44df
fribidi-$FRIBIDI_VER.tar.xz|https://github.com/fribidi/fribidi/releases/download/v$FRIBIDI_VER/fribidi-$FRIBIDI_VER.tar.xz|1de5e8073d035388b71d0b2d452b1da1f8caeea8a7702997b401fe1b6250727f
mbedtls-$MBEDTLS_VER.tar.bz2|https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBEDTLS_VER/mbedtls-$MBEDTLS_VER.tar.bz2|a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
faad2-$FAAD2_VER.tar.gz|https://github.com/knik0/faad2/archive/refs/tags/$FAAD2_VER.tar.gz|ee479ccbae4a8387ab696e6f21a481bd83fe3881471cafa81b4ae59d7d3aed43
libogg-$OGG_VER.tar.xz|https://downloads.xiph.org/releases/ogg/libogg-$OGG_VER.tar.xz|5c8253428e181840cd20d41f3ca16557a9cc04bad4a3d04cce84808677fa1061
libvorbis-$VORBIS_VER.tar.xz|https://downloads.xiph.org/releases/vorbis/libvorbis-$VORBIS_VER.tar.xz|b33cc4934322bcbf6efcbacf49e3ca01aadbea4114ec9589d1b1e9d20f72954b
opus-$OPUS_VER.tar.gz|https://downloads.xiph.org/releases/opus/opus-$OPUS_VER.tar.gz|6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1
opusfile-$OPUSFILE_VER.tar.gz|https://downloads.xiph.org/releases/opus/opusfile-$OPUSFILE_VER.tar.gz|118d8601c12dd6a44f52423e68ca9083cc9f2bfe72da7a8c1acb22a80ae3550b
zlib-$ZLIB_VER.tar.gz|https://zlib.net/fossils/zlib-$ZLIB_VER.tar.gz|9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
flac-$FLAC_VER.tar.xz|https://downloads.xiph.org/releases/flac/flac-$FLAC_VER.tar.xz|f2c1c76592a82ffff8413ba3c4a1299b6c7ab06c734dee03fd88630485c2b920
"

fetch_all() {
    mkdir -p "$SRC"
    echo "$SOURCES" | while IFS='|' read -r name url sum; do
        [ -n "$name" ] || continue
        if [ ! -f "$SRC/$name" ]; then
            echo "== downloading $name"
            curl -fL --retry 3 -o "$SRC/$name.part" "$url"
            mv "$SRC/$name.part" "$SRC/$name"
        fi
        got=$(sha256sum "$SRC/$name" | cut -d' ' -f1)
        if [ "$got" != "$sum" ]; then
            echo "SHA-256 mismatch for $name: got $got, expected $sum" >&2
            exit 1
        fi
        dir="$SRC/${name%.tar.*}"
        [ -d "$dir" ] || tar -xf "$SRC/$name" -C "$SRC"
    done
}

cmake_build() { # name srcdir prefix toolchain-or-empty args...
    name=$1 src=$2 prefix=$3 tc=$4
    shift 4
    bdir="$TP/build/$(basename "$prefix")/$name"
    echo "== $name -> $prefix"
    set -- -S "$src" -B "$bdir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_PREFIX_PATH="$prefix" \
        -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON "$@"
    [ -n "$tc" ] && set -- "$@" -DCMAKE_TOOLCHAIN_FILE="$tc"
    cmake "$@" >"$TP/build/$name-$(basename "$prefix")-configure.log"
    cmake --build "$bdir" -j "$JOBS" >/dev/null
    cmake --install "$bdir" >/dev/null
}

meson_native() { # name srcdir prefix args...
    name=$1 src=$2 prefix=$3
    shift 3
    bdir="$TP/build/$(basename "$prefix")/$name"
    echo "== $name -> $prefix"
    rm -rf "$bdir"
    PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig" meson setup "$bdir" "$src" --prefix "$prefix" \
        --libdir lib --buildtype release -Ddefault_library=static -Db_staticpic=true "$@" \
        >"$TP/build/$name-$(basename "$prefix")-configure.log"
    meson compile -C "$bdir" -j "$JOBS" >/dev/null
    meson install -C "$bdir" >/dev/null
}

autotools_native() { # name srcdir prefix configure-args...
    name=$1 src=$2 prefix=$3
    shift 3
    bdir="$TP/build/$(basename "$prefix")/$name"
    echo "== $name -> $prefix"
    mkdir -p "$bdir"
    (cd "$bdir" && PKG_CONFIG_PATH="$prefix/lib/pkgconfig" CFLAGS="-O2 -fPIC" \
        "$src/configure" --prefix="$prefix" --libdir="$prefix/lib" --enable-static \
        --disable-shared "$@" >"$TP/build/$name-$(basename "$prefix")-configure.log" &&
        make -j "$JOBS" >/dev/null && make install >/dev/null)
}

build_linux() {
    # XTest is only for SDL's own input-synthesis tests.
    cmake_build SDL3 "$SRC/SDL3-$SDL_VER" "$LNX" "" \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF \
        -DSDL_X11_XTEST=OFF
}

build_linux_static() {
    cmake_build mbedtls "$SRC/mbedtls-$MBEDTLS_VER" "$LST" "" \
        -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON \
        -DUSE_SHARED_MBEDTLS_LIBRARY=OFF
    # No CA bundle path built in: the app looks for the system's (they
    # differ between distributions) and sets it.
    cmake_build curl "$SRC/curl-$CURL_VER" "$LST" "" \
        -DCURL_USE_MBEDTLS=ON -DCURL_USE_OPENSSL=OFF -DCURL_USE_GNUTLS=OFF \
        -DCURL_USE_LIBPSL=OFF -DUSE_LIBIDN2=OFF -DUSE_NGHTTP2=OFF -DCURL_ZLIB=OFF \
        -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_USE_GSSAPI=OFF \
        -DCURL_DISABLE_LDAP=ON -DCURL_CA_BUNDLE=none -DCURL_CA_PATH=none \
        -DCURL_DISABLE_DICT=ON -DCURL_DISABLE_FILE=ON -DCURL_DISABLE_FTP=ON \
        -DCURL_DISABLE_GOPHER=ON -DCURL_DISABLE_IMAP=ON -DCURL_DISABLE_IPFS=ON \
        -DCURL_DISABLE_MQTT=ON -DCURL_DISABLE_POP3=ON -DCURL_DISABLE_RTSP=ON \
        -DCURL_DISABLE_SMTP=ON -DCURL_DISABLE_TELNET=ON -DCURL_DISABLE_TFTP=ON \
        -DCURL_DISABLE_WEBSOCKETS=ON \
        -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF \
        -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF
    cmake_build mpg123 "$SRC/mpg123-$MPG123_VER/ports/cmake" "$LST" "" \
        -DBUILD_PROGRAMS=OFF -DBUILD_LIBOUT123=OFF
    cmake_build freetype "$SRC/freetype-$FREETYPE_VER" "$LST" "" \
        -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON \
        -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
    meson_native harfbuzz "$SRC/harfbuzz-$HARFBUZZ_VER" "$LST" \
        -Dfreetype=enabled -Dglib=disabled -Dgobject=disabled -Dcairo=disabled \
        -Dicu=disabled -Dchafa=disabled -Dtests=disabled -Ddocs=disabled \
        -Dutilities=disabled -Dintrospection=disabled
    meson_native fribidi "$SRC/fribidi-$FRIBIDI_VER" "$LST" \
        -Ddocs=false -Dbin=false -Dtests=false
    build_linux_decoders
    build_zlib
}

# Decoders for the formats beyond MP3 (also on their own, as
# linux-decoders, for a linux-static tree built before they were added).
# zlib: for GeoNames' cities1000.zip in "Download Station List". Its
# configure isn't autotools, and builds in its source tree: use a copy.
build_zlib() {
    bdir="$TP/build/linux-static/zlib"
    echo "== zlib -> $LST"
    rm -rf "$bdir"
    mkdir -p "$(dirname "$bdir")"
    cp -R "$SRC/zlib-$ZLIB_VER" "$bdir"
    (cd "$bdir" && CFLAGS="-O2 -fPIC" ./configure --prefix="$LST" --static \
        >"$TP/build/zlib-linux-static-configure.log" &&
        make -j "$JOBS" libz.a >/dev/null && make install >/dev/null)
}

build_linux_decoders() {
    # faad2 builds four variants;
    # only libfaad (floating point, no DRM) is used.
    cmake_build faad2 "$SRC/faad2-$FAAD2_VER" "$LST" "" -DFAAD_BUILD_CLI=OFF
    cmake_build ogg "$SRC/libogg-$OGG_VER" "$LST" "" -DINSTALL_DOCS=OFF -DBUILD_TESTING=OFF
    cmake_build vorbis "$SRC/libvorbis-$VORBIS_VER" "$LST" "" -DBUILD_TESTING=OFF
    cmake_build opus "$SRC/opus-$OPUS_VER" "$LST" "" -DOPUS_BUILD_PROGRAMS=OFF \
        -DOPUS_BUILD_TESTING=OFF -DOPUS_INSTALL_PKG_CONFIG_MODULE=ON
    # opusfile's own HTTP code would need OpenSSL; curl fetches the stream.
    autotools_native opusfile "$SRC/opusfile-$OPUSFILE_VER" "$LST" \
        --disable-http --disable-examples --disable-doc
    cmake_build flac "$SRC/flac-$FLAC_VER" "$LST" "" -DWITH_OGG=ON -DBUILD_CXXLIBS=OFF \
        -DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_DOCS=OFF \
        -DINSTALL_MANPAGES=OFF -DINSTALL_PKGCONFIG_MODULES=ON
}


what=${1:-all}
mkdir -p "$TP/build"
fetch_all
case $what in
linux) build_linux ;;
linux-static) build_linux_static ;;
linux-decoders) build_linux_decoders ;;
zlib) build_zlib ;;
all) build_linux; build_linux_static ;;
*) echo "usage: $0 [linux|linux-static|linux-decoders|zlib|all]" >&2; exit 2 ;;
esac
echo "== done"
