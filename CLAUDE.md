# Project brief: a Radio Garden style radio explorer for PS Vita

Name: **DX Garden**. Internal/folder name: `dxgarden` (data folder `ux0:data/dxgarden/`).

## What this is

A Vita take on Radio Garden: spin a globe, move over a city, and hear live radio from there. It is a homebrew app for a hacked PS Vita (PCH-1000, HENkaku/ensō, taiHEN, SD2Vita storage), written in C with VitaSDK and packaged as a VPK.

It should feel like Radio Garden to use, but with its own visual identity. Do not copy Radio Garden's artwork, logo, colours, icons or text.

## Two apps, one repo (my decision, 2026-11-22)

There are now two separate products that share this repo and `core/`:

- **DX Garden for PS Vita** (`vita/`): the original brief below. Mostly finished; leave it as it is unless I ask.
- **DX Garden for Linux** (`linux/`): a desktop app on SDL3 and OpenGL, released as an AppImage. It has its own features (AAC, Ogg, Opus, FLAC, HLS, artist/song, station websites, its own bundled list) and its own Linux look; the Vita-specific rules below (rear touchpad, 960x544, button remapping) don't apply to it.

Each app has its own `VERSION` and `BUILD` (`vita/VERSION`, `vita/BUILD`, `linux/VERSION`, `linux/BUILD`), its own releases, tagged `vita-vX.Y.Z` or `linux-vX.Y.Z`, and its own bundled station list (`assets/stations/stations.txt` for the Vita, `assets/stations/linux/stations.txt` for Linux). `core/` stays portable C99 and shared: when it changes, the Linux unit tests must pass and the Vita build must still compile. There is no Windows version any more (removed 2026-11-22).

I will test every build on real hardware and report back. Work in small milestones, and stop at the end of each one so I can test before you continue.

## Station data (important)

On the Vita, stations come only from my own station file and imports:

- `ux0:data/dxgarden/stations.txt` in a simple documented format: name, stream URL, country, city, latitude, longitude, plus optional genre and notes.
- M3U/PLS files placed in `ux0:data/dxgarden/import/` (these lack coordinates; let me assign a place to them in the app).
- Add station by URL with the on-screen keyboard (IME dialog).

**One directory is allowed, as a desktop snapshot only: Radio Browser (radio-browser.info).** A desktop script (`tools/rb_to_stations.py`) downloads stations from it once and writes a normal `stations.txt`. The Vita app never contacts Radio Browser.

**Bundled list (my decision, 2026-10-21):** one snapshot made with that script (`--bundle <version>`) ships in the VPK as `assets/stations/stations.txt`. On first run it becomes `ux0:data/dxgarden/stations.txt`; the app never overwrites an existing one. The README says the list is correct as of the release date and that changes are the user's responsibility. Country labels follow where a station actually is, whatever Radio Browser says (my decision): Ukraine's territory, including Crimea and the occupied east and south, is labelled Ukraine; Palestinian territory is labelled Gaza or Occupied West Bank; UK stations are labelled by nation (England, Scotland, Wales, Northern Ireland), and the Crown Dependencies by their own names (Isle of Man, Jersey, Guernsey). Bundle no other list, and do not use Radio Garden's own API or data in any form (including entries that point back at Radio Garden). Ask me before using any other directory, and before adding any live lookup from the Vita (including to Radio Browser).

**Desktop app downloads (my decision, 2026-11-22):** the desktop (Linux) app may download Radio Browser station data, the GeoNames cities file and Natural Earth country borders, to build a new `stations.txt` in the app, when the user asks for it (a dialog with filters, never in the background). The desktop app ships its own bundled list (`assets/stations/linux/stations.txt`, every format it plays, HLS included, no cap); the Vita keeps its MP3 list and never goes online for directory data. The same country-label rules apply to every list. No other directory or live lookup without asking me.

Keep all station loading behind a `station_source` interface in `core/` so another source could be added later, but only if I explicitly decide to. If a feature would need a directory or a live lookup, stop and ask me.

## Architecture

Follow the same split as my previous Vita project:

- `core/`: portable C99, no Vita headers. Station file and playlist parsing, ICY metadata parsing, stream buffering logic, geographic clustering of stations into "places", config handling. Must build and run unit tests on Linux.
- `vita/`: everything SCE-specific (networking, audio output, input, rendering, power/standby handling).
- A native Linux test target (`dxgarden_test`) that builds `core/` without VitaSDK.

## Libraries

Before choosing libraries, check what is actually available as VitaSDK/vdpm packages on this machine. For example: libcurl, mbedTLS or OpenSSL, mpg123, an AAC decoder, libvorbis/libogg, opus/opusfile, and vitaGL for 3D. Tell me what you find and what you propose before writing code that depends on them. Prefer packages already in vdpm over vendoring.

Any world map or globe texture must be public domain or have a licence that allows redistribution in the VPK. Tell me which one you picked and its licence.

## Features, by milestone

**M1: plays one stream**
- Hardcoded test URL, HTTP and HTTPS.
- MP3 decoding, output via the Vita's audio API.
- Ring buffer with configurable prebuffer, so playback survives brief Wi-Fi hiccups.
- On-screen status: connecting, buffering %, playing, error message.

**M2: station library**
- Load `stations.txt` and imports, skipping and reporting malformed lines.
- Group stations into places by city or by nearby coordinates.
- List view sorted and filterable by country, city or genre.
- Favourites and recently played (`favourites.txt`, `recent.txt`).

**M3: the globe (the core experience)**
- A rotating 3D globe with dots for each place. If 3D turns out too heavy for the Vita, fall back to a flat world map and tell me why.
- A fixed crosshair in the centre: rotate the globe with the left stick, d-pad or front-touch drag, and whichever place sits under the crosshair starts playing after a short pause, Radio Garden style.
- Zoom with L/R or pinch.
- Bottom panel: place name, country, station name, now playing; step through stations in that place with buttons.
- Smooth frame rate matters more than visual detail.

**M4: more formats and metadata**
- AAC/HE-AAC (very common for radio), Ogg Vorbis, Opus.
- ICY metadata: show current track/show title.
- Follow redirects, handle playlist URLs that point to streams.
- Auto-reconnect with backoff when a stream drops.
- Clear error for geo-blocked or dead streams (show HTTP status).

**M5: polish**
- Search by station or place name.
- "Jump to random place" button.
- Sleep timer.
- Keep playing with the screen off, if feasible on the Vita (investigate and tell me what is possible).
- Reconnect cleanly after standby/resume.
- HLS streams, only if time allows. Ask me first.

## Controls and UI

- Hardware buttons plus front touchscreen. **Do not use the rear touchpad at all.**
- All hardware controls remappable via a settings screen, except Cross (confirm) and Circle (back).
- Dark theme. Top status bar: station name, now playing, buffer level, Wi-Fi state.
- Big, readable text. The Vita screen is 960x544.

## Files on the Vita

- `ux0:data/dxgarden/config.ini`: settings and button map.
- `ux0:data/dxgarden/stations.txt`, `favourites.txt`, `recent.txt`.
- `ux0:data/dxgarden/import/`: drop M3U/PLS files here.
- Create missing files and folders on first run with sensible defaults, plus a commented example `stations.txt`. Never crash on a malformed file.

## Helper tools (desktop)

- `tools/rb_to_stations.py`: builds a `stations.txt` from a Radio Browser snapshot, with filters (countries, codecs, bitrate, tag, a station cap). Cities come from a GeoNames cities file on my PC (CC BY 4.0, credited), never shipped in the VPK. Standard library only.
- Optional, later: a small cross-platform script that takes my own list of station names and URLs and helps me add city, country and coordinates, then writes a valid `stations.txt`. It must only work on lists I give it.

## How to work with me

- Explain decisions in plain terms and keep a running `NOTES.md` of design decisions and known issues.
- Before each milestone, give a short plan; after it, give me a test checklist (for the Vita or the Linux desktop, whichever app it touches).
- Bump the build number (that app's `BUILD`) for every build so I can tell builds apart; the app, log and VPK or AppImage name show it. `VERSION` (MAJOR.MINOR.PATCH, minor and patch 0-9, shown by the Vita as XX.YY) changes only when we decide that app's next release; releases are tagged `vita-vX.Y.Z` or `linux-vX.Y.Z`.
- If something is uncertain on real hardware (performance, decoder speed, standby behaviour), say so instead of guessing, and suggest how I can test it.
- Keep the README honest: a player and explorer for streams, shipping a dated Radio Browser snapshot as a default list that is the user's to maintain. The Vita app never fetches a directory; the desktop app downloads one only when the user asks. A desktop script can build a fresh list from Radio Browser.
