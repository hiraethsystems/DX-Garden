#!/bin/sh
# Builds the Linux AppImage: dist/dxgarden-<version>-b<build>-x86_64.AppImage
# (or dist/dxgarden-<version>-x86_64.AppImage with --release).
#
# The program is linked with the static libraries from
# `tools/build_win_deps.sh linux-static` (and SDL3 from `... linux`), so it
# needs only glibc (2.38 or later, as built on Debian 13) and what SDL loads
# at run time: the desktop's graphics, sound and input libraries.
#
# appimagetool and the AppImage runtime are downloaded once into
# third_party/appimage/, at fixed versions, and checked against the SHA-256
# sums GitHub publishes for them.
#
# Usage: tools/make_appimage.sh [--release]
# Needs: cmake, ninja-build, curl, sha256sum; assets-build/earth-day.pack
# (tools/make_earth.py) for the globe's imagery.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TOOLS="$ROOT/third_party/appimage"
BUILD="$ROOT/build-appimage"
APPDIR="$BUILD/AppDir"
DIST="$ROOT/dist"
JOBS=$(nproc 2>/dev/null || echo 4)

APPIMAGETOOL_VER=1.9.1
APPIMAGETOOL_SHA=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
RUNTIME_VER=20251108
RUNTIME_SHA=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d

release=OFF
[ "${1:-}" = "--release" ] && release=ON

fetch() { # file url sha256
    if [ ! -f "$TOOLS/$1" ]; then
        echo "== downloading $1"
        mkdir -p "$TOOLS"
        curl -fL --retry 3 -o "$TOOLS/$1.part" "$2"
        mv "$TOOLS/$1.part" "$TOOLS/$1"
    fi
    got=$(sha256sum "$TOOLS/$1" | cut -d' ' -f1)
    if [ "$got" != "$3" ]; then
        echo "SHA-256 mismatch for $1: got $got, expected $3" >&2
        exit 1
    fi
}

fetch "appimagetool-$APPIMAGETOOL_VER-x86_64.AppImage" \
    "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_VER/appimagetool-x86_64.AppImage" \
    "$APPIMAGETOOL_SHA"
fetch "runtime-$RUNTIME_VER-x86_64" \
    "https://github.com/AppImage/type2-runtime/releases/download/$RUNTIME_VER/runtime-x86_64" \
    "$RUNTIME_SHA"
chmod +x "$TOOLS/appimagetool-$APPIMAGETOOL_VER-x86_64.AppImage"

for lib in "$ROOT/third_party/linux/lib/libSDL3.a" "$ROOT/third_party/linux-static/lib/libcurl.a"; do
    if [ ! -f "$lib" ]; then
        echo "$lib is missing: run tools/build_win_deps.sh linux and linux-static" >&2
        exit 1
    fi
done
[ -f "$ROOT/assets-build/earth-day.pack" ] ||
    echo "warning: assets-build/earth-day.pack is missing; the globe will be plain" >&2

echo "== building"
mkdir -p "$BUILD"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DDXG_DESKTOP=ON \
    -DDXG_STATIC_DEPS=ON -DDXG_RELEASE="$release" >"$BUILD/configure.log"
cmake --build "$BUILD" -j "$JOBS" --target dxgarden_desktop >/dev/null

echo "== AppDir"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD" --prefix /usr --strip >/dev/null
# The AppImage's own menu entry runs "dxgarden" from the image (the install
# step wrote the installed path into it).
cp "$ROOT/desktop/linux/club.hiraeth.dxgarden.desktop" \
    "$APPDIR/usr/share/applications/club.hiraeth.dxgarden.desktop"
cp "$APPDIR/usr/share/applications/club.hiraeth.dxgarden.desktop" "$APPDIR/"
cp "$ROOT/desktop/linux/club.hiraeth.dxgarden.png" "$APPDIR/club.hiraeth.dxgarden.png"
ln -sf club.hiraeth.dxgarden.png "$APPDIR/.DirIcon"
ln -sf usr/bin/dxgarden "$APPDIR/AppRun"

# The name: as the VPK's, from VERSION and BUILD.
version=$(tr -d ' \n' <"$ROOT/VERSION")
build=$(tr -d ' \n' <"$ROOT/BUILD")
if [ "$release" = ON ]; then
    out="$DIST/dxgarden-$version-x86_64.AppImage"
else
    out="$DIST/dxgarden-$version-b$build-x86_64.AppImage"
fi
mkdir -p "$DIST"
rm -f "$out"

echo "== packing $out"
# appimagetool is itself an AppImage; extract-and-run means it works
# without FUSE too.
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 \
    "$TOOLS/appimagetool-$APPIMAGETOOL_VER-x86_64.AppImage" --no-appstream \
    --runtime-file "$TOOLS/runtime-$RUNTIME_VER-x86_64" "$APPDIR" "$out" \
    >"$BUILD/appimagetool.log" 2>&1 || {
    tail -20 "$BUILD/appimagetool.log" >&2
    exit 1
}
ls -la "$out"
echo "== done"
