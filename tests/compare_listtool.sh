#!/bin/sh
# Builds station lists from one saved Radio Browser snapshot with both
# tools/rb_to_stations.py and dxgarden_listtool (core/rb_build.c) and checks
# that they write the same station lines and the same summary.
#
#   tests/compare_listtool.sh RAW.json CITIES.zip BORDERS.geojson [BUILD_DIR]
#                             [NOGEO_RAW.json ADMIN1.txt]
#
# RAW.json is a file saved with rb_to_stations.py --save-raw. With a second
# snapshot of stations without coordinates and GeoNames' admin1CodesASCII.txt,
# it also compares --without-coords.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
RAW=$1 GEO=$2 BORDERS=$3 BUILD=${4:-$ROOT/build-linux}
NOGEO=${5:-} ADMIN1=${6:-}
TOOL="$BUILD/dxgarden_listtool"
OUT=$(mktemp -d)
status=0

run() { # name, then the options for both
    name=$1
    shift
    python3 -I "$ROOT/tools/rb_to_stations.py" --from-raw "$RAW" "$@" --max-mb 64 \
        -o "$OUT/$name.py.txt" >"$OUT/$name.py.report" 2>/dev/null
    "$TOOL" --from-raw "$RAW" "$@" -o "$OUT/$name.c.txt" >"$OUT/$name.c.report" 2>/dev/null
    grep -v '^#' "$OUT/$name.py.txt" >"$OUT/$name.py.lines"
    grep -v '^#' "$OUT/$name.c.txt" >"$OUT/$name.c.lines"
    # The summary's first line names the output file; the rest must match.
    tail -n +2 "$OUT/$name.py.report" >"$OUT/$name.py.sum"
    tail -n +2 "$OUT/$name.c.report" >"$OUT/$name.c.sum"
    if cmp -s "$OUT/$name.py.lines" "$OUT/$name.c.lines" &&
        cmp -s "$OUT/$name.py.sum" "$OUT/$name.c.sum"; then
        echo "same:      $name ($(wc -l <"$OUT/$name.c.lines") stations)"
    else
        echo "DIFFERENT: $name"
        diff "$OUT/$name.py.lines" "$OUT/$name.c.lines" | head -20
        diff "$OUT/$name.py.sum" "$OUT/$name.c.sum" | head -20
        status=1
    fi
}

run full --geonames "$GEO" --borders "$BORDERS" --codecs all --hls --max 100000
run vita --geonames "$GEO" --borders "$BORDERS" --codecs mp3 --max 10000
run no-borders --geonames "$GEO" --codecs aac,aac+,ogg --max 5000 --min-bitrate 96
run no-city --no-city --codecs all --hls --max 100000
run small --geonames "$GEO" --borders "$BORDERS" --codecs mp3,flac --max 300 --city-km 12.5
if [ -n "$NOGEO" ]; then
    run without-coords --from-raw "$NOGEO" --geonames "$GEO" --borders "$BORDERS" \
        --admin1 "$ADMIN1" --without-coords --codecs all --hls --max 100000
    run without-coords-no-borders --from-raw "$NOGEO" --geonames "$GEO" --admin1 "$ADMIN1" \
        --without-coords --codecs mp3 --max 20000
    run without-coords-no-admin1 --from-raw "$NOGEO" --geonames "$GEO" --borders "$BORDERS" \
        --without-coords --codecs all --max 100000
fi

[ $status = 0 ] && rm -r "$OUT" || echo "outputs kept in $OUT"
exit $status
