# DX Garden

A radio player and explorer in the spirit of spinning a globe and listening
to what's on air somewhere else. There are two apps, released separately:

- **DX Garden for PS Vita** (homebrew, HENkaku/ensō): MP3 stations on a
  globe you spin with the sticks. Latest release: 0.9.0.
- **DX Garden for Linux** (an AppImage): the same idea on the desktop, with
  more formats (AAC, Ogg, Opus, FLAC, HLS). Latest release: 0.9.2.

They share their station-file format and the portable code in `core/`;
everything else is separate.

![The globe, with the crosshair on Swansea and a Welsh station playing](docs/screenshots/globe.png)

**It comes with a station list, and the list is yours.** The VPK includes
about 7,500 MP3 stations from a snapshot of
[Radio Browser](https://www.radio-browser.info), a community station
directory, which become your `stations.txt` the first time the app starts.
The Vita app never goes online to fetch a directory: everything else comes
from your own `stations.txt`, M3U/PLS files you drop in, or URLs you type
in. The Linux version ships a larger snapshot (about 51,000 stations in
every format it plays, HLS included) the same way, and can download a
fresh list from Radio Browser when you ask it to (File, Download Station
List); it never does so by itself.

**About the bundled list:** it was correct as of the date of the release it
shipped with (the date is at the top of the file). Radio stations move,
change address and close, and Radio Browser's entries are written by its
users, so some stations will stop working or be placed oddly over time.
Keeping the list up to date, and any changes you make to it, are your
responsibility. The app never overwrites your `stations.txt`; delete it
and restart the app to get the bundled list back, or build a fresh one
with the desktop script (see "Building a station list" below) or, on
Linux, with File, Download Station List.

## Status

- **Vita: beta (0.9.0).** All the planned features are in and have been
  tested on a PCH-1000; this release is to collect reports from other
  Vitas before 1.0.
- **Linux: beta (0.9.2).** The AppImage (see "Installing on Linux" below):
  MP3, AAC, Ogg, Opus, FLAC and HLS, artist and song in Now Playing,
  station websites, about 51,000 bundled stations, and downloading a
  fresh station list.

The rest of this section is about the Vita app.

| Browse by country, city or genre | The START menu |
|---|---|
| ![The station list, showing the places in Wales](docs/screenshots/stations-list.png) | ![The menu: search, random place, sleep timer, dark screen and more](docs/screenshots/menu.png) |

- Spin a globe and stop the crosshair on a place to hear it, or browse
  stations by country, city or genre, with favourites and recently played.
- Search by station or place name, jump to a random place, a sleep timer,
  and "Dark screen", which turns the screen black (on the OLED Vita that
  saves most of the display's power) and keeps the radio playing until you
  press a button.
- MP3 streams over HTTP and HTTPS, with track titles, playlist links,
  clear error messages and automatic reconnecting. AAC, Ogg and HLS
  stations say so instead of playing.
- Station names in Latin, Greek, Cyrillic, Arabic, Hebrew, Thai, Georgian,
  Ethiopic, Chinese, Japanese and Korean scripts.
- Buttons other than Cross and Circle can be remapped (START, then
  Settings).
- The Vita won't switch its display off by itself while a station plays,
  because that would silence the app. Real standby (the power button) or
  leaving the app with the PS button pauses it, as with any homebrew app;
  when you come back, the station reconnects by itself.

## Installing on the Vita

You need a PS Vita running HENkaku or ensō with
[VitaShell](https://github.com/TheOfficialFloW/VitaShell), and Wi-Fi.

1. **The shader compiler, once.** DX Garden draws with vitaGL, which needs
   the Vita's shader compiler at `ur0:data/libshacccg.suprx`. If you don't
   have it yet, install and run the ShaRKBR33D homebrew, which extracts it.
   Without it, DX Garden shows a message and quits.
2. **Install the VPK.** Copy `dxgarden-0.9.0.vpk` to the Vita and install it
   with VitaShell.
3. **Start it.** The first start creates `ux0:data/dxgarden/` with the
   bundled station list and opens on the globe.

To update, install the new VPK over the old one. Your `stations.txt`,
favourites, recently played and settings are kept. If the bubble still
shows old artwork, use VitaShell's "Refresh LiveArea" or reinstall.

## Installing on Linux

Download `dxgarden-0.9.2-x86_64.AppImage` from the release. It runs on
most distributions from 2024 on: it needs glibc 2.38 or later (Ubuntu
24.04+, Fedora 39+, Debian 13+, Arch, openSUSE Tumbleweed). So far it has
only been tested on Debian 13 with KDE Plasma, so reports from other
systems are welcome.

To try it, make it executable and start it:

```sh
chmod +x dxgarden-0.9.2-x86_64.AppImage
./dxgarden-0.9.2-x86_64.AppImage
```

To add it to your desktop's menu (KDE Plasma, GNOME and others), keep it
somewhere permanent under a fixed name and write a menu entry and icon for
it. Run this in the folder you downloaded it to:

```sh
mkdir -p ~/Applications ~/.local/share/applications ~/.local/share/icons/hicolor/128x128/apps
mv dxgarden-0.9.2-x86_64.AppImage ~/Applications/dxgarden.AppImage
chmod +x ~/Applications/dxgarden.AppImage
(cd /tmp && ~/Applications/dxgarden.AppImage --appimage-extract club.hiraeth.dxgarden.png >/dev/null &&
  mv squashfs-root/club.hiraeth.dxgarden.png ~/.local/share/icons/hicolor/128x128/apps/ &&
  rm -r squashfs-root)
cat > ~/.local/share/applications/club.hiraeth.dxgarden.desktop <<EOF
[Desktop Entry]
Type=Application
Name=DX Garden
GenericName=Radio Explorer
Comment=Spin a globe and listen to live radio streams from your own station list
Exec="$HOME/Applications/dxgarden.AppImage"
Icon=club.hiraeth.dxgarden
Terminal=false
Categories=AudioVideo;Audio;Player;
Keywords=radio;stream;internet radio;globe;
StartupWMClass=club.hiraeth.dxgarden
EOF
```

DX Garden then shows up under Multimedia (on KDE, run `kbuildsycoca6` if it
doesn't appear straight away). To update, replace
`~/Applications/dxgarden.AppImage` with the new file. Your stations,
favourites and settings live in `~/.local/share/dxgarden/` and are kept.
If the system has no FUSE, start the AppImage with
`--appimage-extract-and-run`.

The Linux version plays MP3, AAC and HE-AAC (v1 and v2), Ogg Vorbis, Opus
and FLAC streams, and HLS streams (`.m3u8`, used by the BBC, Radio France
and many others), unless they're encrypted (the Vita version plays MP3
only). It works out the format
from the stream itself, so a station that calls its AAC "MP3" still plays.

Now Playing shows the song and the artist when the station sends them
(as a stream title, or in the tags of Ogg, FLAC and HLS streams), and a
Website link (also Station, Open Website) when the station has one: a
`web:https://...` word in the station's Notes, or the address the stream
itself announces.

### Downloading a fresh station list

File, Download Station List builds a new list from today's Radio Browser,
the same way the bundled list was made. Choose the formats (all of them,
HLS included, by default), and if you like some countries (two-letter
codes such as `GB,IE`), a tag, a lowest bitrate, how many stations at
most (the most popular first) and how far a station can be from a town
before it keeps its own position. Then:

1. The app asks Radio Browser for the stations, one page of 500 a second
   to go easy on its volunteer-run servers. The whole directory takes
   about a minute, or about three with stations without coordinates.
2. The first time, it downloads GeoNames' town list (`cities1000.zip`,
   11 MB) and region names (`admin1CodesASCII.txt`, 150 KB) and Natural
   Earth's country borders (3 MB), and keeps them in
   `~/.local/share/dxgarden/cache/` for next time. Tick "Download the town
   names, regions and borders again" to refresh them.
3. It writes `stations-new.txt` in the data folder and shows a summary.
   Nothing changes until you press **Replace My Station List**, which
   keeps your old list as `stations-backup-<date>.txt` and loads the new
   one. Favourites and recently played are kept (they're stream
   addresses).

"Stations without coordinates" (on by default) also keeps the stations
Radio Browser has no position for, about four in five of them. Each is
placed, in this order: at another stream of the same station (same name
and website); at the town or region given as its region (a region's
stations go to its capital); at a town or region named in its name or
tags; or, when nothing says where it is, in its country's
**Country-wide** place, a light blue dot at the middle of the country.
The Notes say which, and these positions are only as good as Radio
Browser's region field: a station placed by region is in the right
region, not necessarily the right town. UK stations that give only "UK"
are in United Kingdom, Country-wide (no nation can be told).

The new list replaces stations you added or edited in `stations.txt`
(they're still in the backup); stations in `import/` aren't touched.
The country labels follow the same rules as the bundled list.

Without a window, `dxgarden --download-list [--codecs all] [--hls]
[--countries GB,IE] [--max N] [--without-coords] [--refresh]` does steps 1 to 3 and prints
the summary (its defaults are MP3 only and 2,000 stations, as with the
script).

### Looks

View, Look switches between the classic look and a flat one in your
desktop's colours (your KDE colour scheme, or GNOME's light or dark style).
On KDE Plasma it runs through XWayland by default (SDL's choice there); to
run it as a native Wayland app, start it with `SDL_VIDEO_DRIVER=wayland`.

## Vita controls

These are the defaults. Everything except Cross and Circle can be changed
under START, then Settings. The rear touchpad isn't used.

| | Globe | List |
|---|---|---|
| Left stick, d-pad, front touch drag | Turn the globe | Move (d-pad) |
| Pinch, or R / L | Zoom in / out | |
| L / R | | Previous / next tab |
| Cross | Play the place under the crosshair, or show its stations | Open, play |
| Circle | | Back |
| Right stick left/right, or the panel arrows | Previous / next station in the place | |
| Triangle | Favourite | Favourite |
| Square | Stop | Stop |
| SELECT | Switch to the list | Switch to the globe |
| START | Menu: search, random place, sleep timer, dark screen, add a station by URL, settings | Menu |

Stop the crosshair on a place and it starts playing after a moment.

## Versions

Each app has its own release number, MAJOR.MINOR.PATCH (`vita/VERSION`,
`linux/VERSION`), and its own build number for test builds (`vita/BUILD`,
`linux/BUILD`), which only goes up. The app, the log and test files show
both, e.g. "0.9.0 (build 29)", `dxgarden-0.9.0-b29.vpk` and
`dxgarden-0.9.2-b47-x86_64.AppImage`. Releases are tagged `vita-vX.Y.Z` or
`linux-vX.Y.Z` in git (the earlier `v0.9.0` is the Vita's, `v0.9.1` is
Linux's; Linux 0.9.2 is `linux-v0.9.2`), and their files are named without the build number (Vita: build
with `-DDXG_RELEASE=ON`; Linux: `tools/make_appimage.sh --release`).

The Vita itself stores a version as XX.YY, so it shows 0.9.0 as 00.90,
1.0.1 as 01.01 and 1.2.3 as 01.23 (minor and patch go up to 9). Test
builds of the same version share that number; tell them apart by the
build number in About.

## Building the Vita app

Needs [VitaSDK](https://vitasdk.org) with curl (mbedTLS build), mpg123,
freetype, harfbuzz, fribidi, vitaGL and libvita2d from vdpm.

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This produces `build/dxgarden-<version>-b<build>.vpk`, or
`build/dxgarden-<version>.vpk` when configured with `-DDXG_RELEASE=ON`.

The portable core has Linux unit tests:

```sh
cmake -S . -B build-test && cmake --build build-test && ./build-test/dxgarden_test
```

## Building the Linux app

### The AppImage

`tools/make_appimage.sh` builds `dist/dxgarden-<version>-b<build>-x86_64.AppImage`,
one file that runs on most Linux distributions from 2024 on (it needs glibc
2.38 or later: Ubuntu 24.04+, Fedora 39+, Debian 13+, Arch, openSUSE
Tumbleweed). It needs the libraries below built first:

```sh
tools/build_linux_deps.sh linux
tools/build_linux_deps.sh linux-static
python3 tools/make_earth.py --cache third_party/earth --out assets-build
tools/make_appimage.sh
```

`tools/make_appimage.sh --release` names it `dxgarden-<version>-x86_64.AppImage`
instead, for a release. See "Installing on Linux" above for running it and
adding it to the menu.

### From source

To build and install it for your user without an AppImage, on Debian 13
you need these packages:

```sh
sudo apt install cmake ninja-build meson curl libcurl4-openssl-dev libmpg123-dev \
  libfaad-dev libvorbis-dev libopusfile-dev libflac-dev zlib1g-dev \
  libfreetype-dev libharfbuzz-dev libfribidi-dev libx11-dev libxext-dev libxrandr-dev \
  libxcursor-dev libxi-dev libxss-dev libxfixes-dev libwayland-dev libxkbcommon-dev \
  wayland-protocols libdecor-0-dev libegl-dev libgl-dev libpipewire-0.3-dev libpulse-dev \
  libasound2-dev libdbus-1-dev libudev-dev
```

Then build SDL3, the globe's imagery (a one-off download of NASA's Blue
Marble, about 70 MB once built; needs Pillow) and the app, and install it
for your user:

```sh
tools/build_linux_deps.sh linux
python3 tools/make_earth.py --cache third_party/earth --out assets-build
cmake -S . -B build-linux -G Ninja -DDXG_LINUX=ON && cmake --build build-linux
cmake --install build-linux --prefix ~/.local
```

It keeps its settings and station files in `~/.local/share/dxgarden/`;
the program's own files go in `~/.local/share/club.hiraeth.dxgarden/`,
so reinstalling never touches your stations or favourites.
It works on KDE Plasma and GNOME, on Wayland and X11. View, Look switches
between the classic look and a flat one in your desktop's colours (your
KDE colour scheme, or GNOME's light or dark style). On KDE Plasma it
runs through XWayland by default (SDL's choice there); to run it as a
native Wayland app, start it with `SDL_VIDEO_DRIVER=wayland dxgarden`. If
the menu entry doesn't show up straight away on KDE, run `kbuildsycoca6`.

## Building a station list (optional)

`tools/rb_to_stations.py` (Python 3.8+, standard library only) downloads
stations from Radio Browser once and writes a `stations.txt`. Radio Browser
has no city field, so the script fills it from a GeoNames cities file you
download first (https://download.geonames.org/export/dump/cities1000.zip),
falling back to Radio Browser's region (state or province).

With `--borders` and Natural Earth's country shapes
(`ne_50m_admin_0_countries.geojson`, public domain), it also checks that
each station is in its own country and fixes the usual mistakes: swapped
or sign-flipped coordinates, pins out at sea, a town given in the station's
name, and country codes picked wrongly from a list. Anything it changes is
explained in the station's Notes.

With `--without-coords` and GeoNames' region names (`--admin1
admin1CodesASCII.txt`) it also keeps stations without coordinates, placed
as described under "Downloading a fresh station list".

```sh
python3 tools/rb_to_stations.py --geonames cities1000.zip \
    --borders ne_50m_admin_0_countries.geojson --max 10000 -o stations.txt
python3 tools/rb_to_stations.py --help   # countries, codecs, bitrate, tag, --check, --save-raw
```

Copy the result to `ux0:data/dxgarden/stations.txt`. It keeps MP3
stations by default, since that is the only format the app plays.
The bundled list was made this way, with `--bundle <version>` for its
header.

## LiveArea art

The home-screen bubble and LiveArea images live in `art/livearea/` as normal
full-colour PNGs. The Vita needs them as 8-bit palette PNGs at exact sizes:

| File | Size | What it is |
|---|---|---|
| `icon0.png` | 128x128 | Home-screen bubble, cropped to a circle |
| `bg.png` | 840x500 | LiveArea background; the gate and title sit on top |
| `startup.png` | 280x158 | The gate you tap to start the app |

```sh
python3 tools/make_livearea.py render         # the globe artwork (needs numpy)
python3 tools/make_livearea.py placeholders   # labelled templates with a grid
python3 tools/make_livearea.py install        # your art in art/livearea/ -> sce_sys/
```

`install` checks the sizes, converts to 256 colours (dithered, with the
app's amber and text colours kept exact) and copies the results into
`sce_sys/`; then rebuild. Needs Pillow. The current art is the globe from
`render`, drawn from the app's own Natural Earth map.

The gate (`startup.png`) sits over the upper middle of `bg.png` when the
LiveArea is open, so keep text and detail out of that part of the
background.

## Files on the Vita

All in `ux0:data/dxgarden/`. They are created on first run if missing.

- `stations.txt`: your stations, one per line:
  `Name | Stream URL | Country | City | Latitude | Longitude | Genre | Notes`.
  The file explains the format and has commented-out examples. In the
  Notes, `web:<address>` gives the station's website (the desktop app
  shows it as a link; lists made by `tools/rb_to_stations.py` include it
  when Radio Browser has one).
- `import/`: drop M3U, M3U8 or PLS files here. Their stations appear under
  "Unplaced" until you give them a place in the app.
- `favourites.txt`, `recent.txt`: managed by the app (URLs, one per line).
- `config.ini`: settings.
- `log-<version>-b<build>.txt`: log of the last run of that build, e.g.
  `log-0.9.0-b29.txt`, and `...-previous.txt` for the run before it.

## Credits and licences

- DX Garden is licensed under the GNU GPL v3 (see `LICENSE`).
- CA certificates: Mozilla's root store, via curl.se (MPL 2.0).
- Fonts by the Noto Project Authors, SIL Open Font License 1.1
  (`assets/fonts/OFL.txt`, also shipped in the VPK): Noto Sans, Noto Sans
  Arabic, Hebrew, Thai, Georgian and Ethiopic, and a subset of Noto Sans
  CJK SC made by `tools/make_fonts.py`.
- Libraries linked into the app, from VitaSDK's packages: vitaGL and
  vitaShaRK (LGPL 3.0), math-neon (MIT), libvita2d (MIT), libcurl (curl
  licence), Mbed TLS (Apache 2.0), mpg123 (LGPL 2.1), HarfBuzz (MIT),
  FriBidi (LGPL 2.1), FreeType (FreeType licence), libpng (libpng
  licence), libjpeg-turbo (IJG and BSD), zlib (zlib licence), bzip2
  (bzip2 licence) and Zstandard (BSD).
- The Linux AppImage: SDL3 (zlib licence), libcurl (curl licence), Mbed
  TLS (Apache 2.0), mpg123 (LGPL 2.1), faad2 (GPL 2 or later), libogg,
  libvorbis, Opus, opusfile and libFLAC (BSD), FreeType (FreeType
  licence), HarfBuzz (MIT), FriBidi (LGPL 2.1) and zlib (zlib licence),
  linked in; Unicode tables made from the Unicode Character Database
  (Unicode licence); Liberation
  Sans and Noto Sans fonts (SIL Open Font License 1.1); globe imagery from
  NASA Blue Marble: Next Generation (Reto Stöckli, NASA Earth Observatory;
  public domain).
- LiveArea art: see "LiveArea art" above.
- The bundled station lists (`assets/stations/stations.txt` in the VPK,
  `assets/stations/linux/stations.txt` in the AppImage), lists made with
  `tools/rb_to_stations.py` and lists the Linux app downloads come from
  Radio Browser (radio-browser.info), whose API documentation says it may be used
  in free and non-free software and its data mirrored. Town names in them
  come from GeoNames (geonames.org), licensed CC BY 4.0. Positions are
  checked against Natural Earth country shapes (public domain). The Linux
  app downloads GeoNames' `cities1000.zip` from download.geonames.org and
  Natural Earth's `ne_50m_admin_0_countries.geojson` (v5.1.2) from the
  natural-earth-vector repository on GitHub, only when you ask it to.
- The globe map (`assets/map/world.png`) is drawn by `tools/make_map.py`
  from [Natural Earth](https://www.naturalearthdata.com) 1:50m data, which
  is in the public domain.
