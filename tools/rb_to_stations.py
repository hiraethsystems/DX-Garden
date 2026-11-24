#!/usr/bin/env python3
"""Builds a DX Garden stations.txt from a Radio Browser snapshot.

Radio Browser (radio-browser.info) is a community station directory. This
desktop script downloads a filtered list of stations once and writes a
normal stations.txt that you copy to the Vita. The Vita app never contacts
Radio Browser. A snapshot made with --bundle is shipped in the VPK as the
default station list (decided by the project owner, 2026-10-21).

Coordinates in Radio Browser are sometimes wrong (a station in the middle
of the ocean, or in another country), and so are country codes (Germany's
DE picked as DK, DZ or NE). With a GeoNames file, each station must lie
within GEO_CHECK_KM of a town in its own country, or with --borders (Natural
Earth country shapes, public domain), inside its country or within
NEAR_OWN_KM of one of its towns. If not, in order:
swapped or sign-flipped coordinates; a town of NAMED_TOWN_POP or more named
in the station's name; the coordinates themselves, if they are on a big
city in another country (the country label is corrected); Radio Browser's
region as a town name; the country's capital. Stations in the open sea that
name no country are dropped.

Radio Browser has no city field, so the city is the nearest place in a
GeoNames cities file on this computer (CC BY 4.0, geonames.org), within a
distance cap, falling back to Radio Browser's region (state or province).
Download a cities file first. cities1000 (towns over 1,000 people) leaves
far fewer stations without a city than cities15000:
    https://download.geonames.org/export/dump/cities1000.zip

Examples:
    python3 tools/rb_to_stations.py --geonames ~/Downloads/cities1000.zip \\
        -o stations.txt
    python3 tools/rb_to_stations.py --geonames cities1000.zip \\
        --countries GB,IE --codecs mp3 --min-bitrate 96 --max 500 -o uk.txt
    python3 tools/rb_to_stations.py ... --check 40

Standard library only. Python 3.8 or newer.
"""
import argparse
import concurrent.futures
import http.client
import io
import json
import math
import os
import random
import socket
import sys
import time
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DNS_NAME = "all.api.radio-browser.info"
PAGE_SIZE = 500
PAGE_DELAY_S = 1.0
TIMEOUT_S = 30
RETRIES = 4  # per server, waiting 2, 4 and 8 s between tries
DEFAULT_MAX = 2000
DEFAULT_CITY_KM = 30.0
DEFAULT_MAX_MB = 8  # the Vita app's stations.txt limit; the desktop app takes 32

# Codec names as Radio Browser reports them, keyed by our option names.
# "ogg" covers Ogg Vorbis (and some Ogg Opus) streams.
CODECS = {"mp3": "MP3", "aac": "AAC", "aac+": "AAC+", "ogg": "OGG", "opus": "OPUS",
          "flac": "FLAC"}

# Radio Browser's long official country names, shortened for the screen.
# Keys are lower case, without a leading "The ".
COUNTRY_NAMES = {
    "united states of america": "United States",
    "united kingdom of great britain and northern ireland": "United Kingdom",
    "russian federation": "Russia",
    "republic of korea": "South Korea",
    "democratic people's republic of korea": "North Korea",
    "iran (islamic republic of)": "Iran",
    "islamic republic of iran": "Iran",
    "bolivia (plurinational state of)": "Bolivia",
    "plurinational state of bolivia": "Bolivia",
    "venezuela (bolivarian republic of)": "Venezuela",
    "bolivarian republic of venezuela": "Venezuela",
    "taiwan, republic of china": "Taiwan",
    "taiwan, province of china": "Taiwan",
    "viet nam": "Vietnam",
    "republic of moldova": "Moldova",
    "united republic of tanzania": "Tanzania",
    "lao people's democratic republic": "Laos",
    "syrian arab republic": "Syria",
    "czech republic": "Czechia",
    "democratic republic of the congo": "DR Congo",
    "state of palestine": "Palestine",
    "republic of north macedonia": "North Macedonia",
    "holy see": "Vatican City",
    # Radio Browser drops punctuation from some names; these are the forms
    # it actually sends.
    "democratic peoples republic of korea": "North Korea",
    "coted ivoire": "Côte d'Ivoire",
    "cote d'ivoire": "Côte d'Ivoire",
    "dutch part sint maarten": "Sint Maarten",
    "falkland islands malvinas": "Falkland Islands (Malvinas)",
    "reunion": "Réunion",
}

# Small words inside a name stay lower case: "Trinidad and Tobago".
SMALL_WORDS = ("And", "Of", "The")

# Stream URLs that point back at Radio Garden (for example a
# "listening-from-radio-garden" parameter) suggest the entry was copied
# from it. Radio Garden's data is off-limits, so these are skipped.
RADIO_GARDEN_MARKS = ("radio-garden", "radiogarden", "radio_garden", "radio.garden")

# GeoNames feature codes that are parts of a city or no longer exist.
SKIP_FEATURES = {"PPLX", "PPLH", "PPLQ", "PPLW", "PPLCH"}
BIG_CITY_KM = 20.0  # within this, the most populous city wins
GEO_CHECK_KM = 200.0  # a station must be this close to a town in its country
GEO_FIX_KM = 30.0  # a swapped/flipped position must land this close to one
# A misplaced station's coordinates are trusted (and its country label
# corrected) when they sit this close to a city at least this big: someone
# put the pin on a real city, and picked the wrong country from the list.
TRUST_CITY_KM = 15.0
TRUST_CITY_POP = 40000
NAMED_TOWN_POP = 50000  # smallest town in a station name, in its own country
NAMED_ABROAD_POP = 200000  # in another country ("Forest", "Roman" are towns too)
NEAR_OWN_KM = 30.0  # with --borders: this close to an own-country town counts as in it
# Within this of its own country, a station placed across a border is left
# exactly as Radio Browser has it (border towns, disputed areas): the
# script only corrects what is clearly a slip.
BORDER_AREA_KM = 300.0

# Country labels by where a station is, whatever Radio Browser says (the
# project owner's decision, 2026-10-21): Ukraine's territory, including
# Crimea and the occupied east and south, is labelled Ukraine; Palestinian
# territory is labelled Gaza or Occupied West Bank. Decided by the nearest
# GeoNames town (which has Crimea as UA and tells East Jerusalem from West),
# then by the Natural Earth shape, which puts Crimea in Russia, hence the
# explicit Crimea box.
# Likewise stations in the UK are labelled by nation: England, Scotland,
# Wales or Northern Ireland (GeoNames admin1 codes).
UK_NATIONS = {"ENG": "England", "SCT": "Scotland", "WLS": "Wales", "NIR": "Northern Ireland"}
# The Crown Dependencies are not in the UK: by their own names.
CROWN_DEPENDENCIES = {"IM": "Isle of Man", "JE": "Jersey", "GG": "Guernsey"}
UK_TOWN_KM = 60.0  # far from a town (Highlands, islands): nearest UK town this close
TERRITORY_TOWN_KM = 15.0
CRIMEA_BOX = (44.3, 32.4, 46.2, 36.65)  # lat0, lon0, lat1, lon1
GAZA_MAX_LON = 34.6  # Gaza lies west of this, the West Bank east of it

# Stations without coordinates (--without-coords) are placed, in order:
# at another stream of the same station (same name and country) that has
# a position; at the town or region Radio Browser gives as its region; at
# a town of NOCOORD_TOWN_POP or more in the station's name; at a region (or
# a UK county) named in the station's name or tags; otherwise in one
# "Country-wide" place per country, at Natural Earth's label point (the
# project owner's decision, 2026-11-22). Regions are GeoNames' admin1 areas
# (admin1CodesASCII.txt), placed at their capital.
COUNTRY_WIDE = "Country-wide"
NOCOORD_TOWN_POP = 20000
ALT_NAME_POP = 15000  # towns this big are also found by their other names
REGION_TOWN_WINS_POP = 1000000  # "New York": the city, not the state
# Towns that are also everyday words in station names.
TOWN_STOPWORDS = {"reading", "sale", "deal", "march", "bath", "hyde", "mobile", "normal",
                  "independence", "paradise", "surprise", "spring", "orange", "phoenix",
                  "hope", "unity", "liberty", "harmony", "eden", "radio", "music", "classic",
                  "gold", "love", "smooth", "magic", "star", "sun", "kiss", "heart", "energy",
                  "capital", "central", "best", "good", "nice", "salsa", "jazz", "rock",
                  "victoria", "florence", "mix", "hit", "hits", "top", "rio", "santa", "san",
                  "nova", "vida", "fe", "paz", "esperanza", "union"}
# Words that make a region name too vague to find in a station's name.
REGION_GENERIC = {"central", "centre", "center", "north", "northern", "south", "southern",
                  "east", "eastern", "west", "western", "capital", "coast", "island", "islands",
                  "region", "district", "national", "metropolitan", "city", "upper", "lower",
                  "new", "greater", "federal", "territory", "valley", "lake", "river", "sea",
                  "the", "of", "and", "province", "state", "county"}
# Regions as Radio Browser's users write them: local names, abbreviations
# and old spellings, by country, as GeoNames admin1 codes.
REGION_ALIASES = {
    "AU": {"nsw": "02", "vic": "07", "qld": "04", "wa": "08", "sa": "05", "tas": "06",
           "nt": "03", "act": "01"},
    "AT": {"wien": "09", "tirol": "07", "steiermark": "06", "oberosterreich": "04",
           "niederosterreich": "03", "karnten": "02"},
    "BE": {"bruxelles": "BRU", "brussel": "BRU", "brussels": "BRU", "wallonie": "WAL",
           "vlaanderen": "VLG"},
    "BR": {"ac": "01", "al": "02", "ap": "03", "am": "04", "ba": "05", "ce": "06", "df": "07",
           "es": "08", "go": "29", "ma": "13", "mt": "14", "ms": "11", "mg": "15", "pa": "16",
           "pb": "17", "pr": "18", "pe": "30", "pi": "20", "rj": "21", "rn": "22", "rs": "23",
           "ro": "24", "rr": "25", "sc": "26", "sp": "27", "se": "28", "to": "31"},
    "CA": {"ab": "01", "bc": "02", "mb": "03", "nb": "04", "nl": "05", "ns": "07", "on": "08",
           "pe": "09", "pei": "09", "qc": "10", "que": "10", "quebec": "10", "sk": "11",
           "yt": "12", "nt": "13", "nu": "14", "newfoundland": "05"},
    "CH": {"zurich": "ZH", "geneve": "GE", "genf": "GE", "luzern": "LU", "graubunden": "GR",
           "st gallen": "SG", "basel": "BS", "wallis": "VS", "tessin": "TI", "waadt": "VD"},
    "CN": {"kiangsu": "04", "hopei": "10", "shantung": "25", "chekiang": "02", "szechuan": "32",
           "szechwan": "32", "kwangtung": "30", "sinkiang": "13", "anhwei": "01",
           "kansu": "15", "shansi": "24", "hupei": "12", "hupeh": "12", "fukien": "07",
           "kiangsi": "03", "kwangsi": "16", "shensi": "26", "honan": "09", "hunan": "11",
           "kweichow": "18", "yunnan": "29", "kirin": "05", "heilungkiang": "08",
           "tsinghai": "06", "peking": "22", "inner mongolia": "20", "nei mongol": "20"},
    "DE": {"bayern": "02", "hessen": "05", "nordrhein westfalen": "07", "nrw": "07",
           "niedersachsen": "06", "sachsen": "13", "sachsen anhalt": "14", "thuringen": "15",
           "baden wurttemberg": "01", "bw": "01", "rheinland pfalz": "08", "berlin": "16",
           "brandenburg": "11", "mecklenburg vorpommern": "12", "saarland": "09",
           "schleswig holstein": "10", "hamburg": "04", "bremen": "03", "sassonia": "13",
           "saxe": "13", "baviera": "02", "rhineland palatinate": "08"},
    "ES": {"catalunya": "56", "cataluna": "56", "andalucia": "51", "euskadi": "59",
           "pais vasco": "59", "comunidad de madrid": "29", "comunitat valenciana": "60",
           "comunidad valenciana": "60", "castilla y leon": "55", "castilla la mancha": "54",
           "islas baleares": "07", "illes balears": "07", "canarias": "53",
           "islas canarias": "53", "navarra": "32", "aragon": "52"},
    "FR": {"bretagne": "53", "normandie": "28", "nouvelle aquitaine": "75", "corse": "94",
           "ile de france": "11", "provence alpes cote d azur": "93", "paca": "93",
           "centre val de loire": "24"},
    "IT": {"lombardia": "09", "piemonte": "12", "sicilia": "15", "puglia": "13", "marche": "10",
           "toscana": "16", "sardegna": "14", "lazio": "07", "veneto": "20",
           "emilia romagna": "05", "friuli": "06", "trentino": "17", "alto adige": "17",
           "sudtirol": "17", "valle d aosta": "19", "basilicata": "02", "liguria": "08",
           "trentino alto adige": "17"},
    "NL": {"noord holland": "07", "zuid holland": "11", "noord brabant": "06",
           "fryslan": "02", "friesland": "02"},
    "PL": {"malopolska": "77", "malopolskie": "77", "mazowsze": "78", "mazowieckie": "78",
           "slask": "83", "slaskie": "83", "pomorze": "82", "pomorskie": "82",
           "wielkopolska": "86", "wielkopolskie": "86", "dolny slask": "72",
           "dolnoslaskie": "72", "podkarpacie": "80", "podkarpackie": "80"},
}
# UK counties and areas in station names and tags: the town to place them at
# (the nation follows from the town).
GB_AREAS = {"yorkshire": "Leeds", "cornwall": "Truro", "devon": "Exeter", "kent": "Maidstone",
            "essex": "Chelmsford", "sussex": "Brighton", "surrey": "Guildford",
            "norfolk": "Norwich", "suffolk": "Ipswich", "lancashire": "Preston",
            "cumbria": "Carlisle", "dorset": "Dorchester", "somerset": "Taunton",
            "hampshire": "Winchester", "merseyside": "Liverpool", "midlands": "Birmingham",
            "tyneside": "Newcastle upon Tyne", "teesside": "Middlesbrough",
            "cheshire": "Chester", "derbyshire": "Derby", "lincolnshire": "Lincoln",
            "northumberland": "Morpeth", "wiltshire": "Salisbury",
            "gloucestershire": "Gloucester", "oxfordshire": "Oxford",
            "berkshire": "Reading", "hertfordshire": "Hertford",
            "cambridgeshire": "Cambridge", "shropshire": "Shrewsbury",
            "staffordshire": "Stafford", "warwickshire": "Warwick",
            "leicestershire": "Leicester", "nottinghamshire": "Nottingham",
            "worcestershire": "Worcester", "herefordshire": "Hereford",
            "buckinghamshire": "Aylesbury", "bedfordshire": "Bedford",
            "northamptonshire": "Northampton", "isle of wight": "Newport",
            "fife": "Glenrothes", "ayrshire": "Ayr", "lanarkshire": "Hamilton",
            "aberdeenshire": "Aberdeen", "perthshire": "Perth", "highlands": "Inverness",
            "gwynedd": "Caernarfon", "anglesey": "Llangefni", "pembrokeshire": "Haverfordwest",
            "ceredigion": "Aberystwyth", "powys": "Llandrindod Wells",
            "glamorgan": "Cardiff", "carmarthenshire": "Carmarthen", "antrim": "Antrim",
            "armagh": "Armagh", "tyrone": "Omagh", "fermanagh": "Enniskillen",
            "east anglia": "Norwich", "west country": "Bristol", "west midlands": "Birmingham",
            "east midlands": "Nottingham", "greater london": "London",
            "greater manchester": "Manchester", "east sussex": "Brighton",
            "west sussex": "Chichester", "north yorkshire": "York",
            "south yorkshire": "Sheffield", "west yorkshire": "Leeds",
            "east yorkshire": "Hull"}

# Words that mark a test broadcast rather than a station.
TEST_WORDS = ("test",)

def tool_version():
    try:
        with open(os.path.join(ROOT, "linux", "VERSION"), encoding="utf-8") as f:
            return f.read().strip() or "dev"
    except OSError:
        return "dev"


USER_AGENT = "dxgarden-tools/" + tool_version()


def log(*a):
    print(*a, file=sys.stderr)


# ---------------------------------------------------------------- cleaning


def clean(s, limit=200):
    """One stations.txt field: no '|', no line breaks or control characters,
    single spaces, trimmed, at most `limit` characters."""
    if s is None:
        return ""
    s = str(s)
    out = []
    for ch in s:
        if ch == "|":
            out.append(" ")
        elif unicodedata.category(ch) in ("Cc", "Cf", "Zl", "Zp"):
            out.append(" ")
        else:
            out.append(ch)
    s = " ".join("".join(out).split())
    if len(s) > limit:
        s = s[:limit].rstrip()
    return s


def shorten(s, limit):
    """Cut at a word boundary with an ellipsis if longer than `limit`."""
    if len(s) <= limit:
        return s
    cut = s[:limit - 1]
    if " " in cut[limit // 2:]:
        cut = cut[:cut.rindex(" ")]
    return cut.rstrip(" ,-") + "\u2026"


def clean_name(s):
    # A line starting with '#' is a comment in stations.txt.
    return shorten(clean(s, 300).lstrip("#").strip(), 100)


def unwrap_url(u):
    """The real address inside an email link-scanner wrapper (Proofpoint
    urldefense v2, Outlook safelinks), which would route every connection
    through the scanner. Other URLs come back unchanged."""
    try:
        parts = urllib.parse.urlsplit(u)
    except ValueError:
        return u
    host = parts.netloc.lower()
    query = urllib.parse.parse_qs(parts.query)
    if host.endswith("urldefense.proofpoint.com") and parts.path.startswith("/v2/url") \
            and query.get("u"):
        # v2 encodes "/" as "_" and other characters as "-XX" (hex).
        inner = query["u"][0].replace("_", "/")
        inner = "".join(
            chr(int(seg[:2], 16)) + seg[2:] if i and len(seg) >= 2 else ("-" + seg if i else seg)
            for i, seg in enumerate(inner.split("-")))
        return inner
    if host.endswith("safelinks.protection.outlook.com") and query.get("url"):
        return query["url"][0]
    return u


def clean_url(u):
    """The stream URL, or None if it can't be written safely."""
    u = unwrap_url((u or "").strip())
    if not (u.lower().startswith("http://") or u.lower().startswith("https://")):
        return None
    if any(unicodedata.category(c) == "Cc" for c in u) or "|" in u:
        return None
    u = u.replace(" ", "%20")
    if len(u) > 500:
        return None
    return u


def clean_homepage(u):
    """The station's website for "web:<url>" in the notes, or None. The
    notes are split at spaces, so the address must not contain any."""
    u = (u or "").strip()
    if not u:
        return None
    u = clean_url(u)
    if not u or len(u) > 200:
        return None
    return u


def country_name(s):
    s = clean(s, 80)
    if s.lower().startswith("the "):
        s = s[4:]
    s = COUNTRY_NAMES.get(s.lower(), s)
    for w in SMALL_WORDS:
        s = s.replace(" %s " % w, " %s " % w.lower())
    return s


def genre_from_tags(tags):
    out = []
    for t in (tags or "").split(","):
        t = clean(t, 30).lstrip("#").strip()
        if t and t.lower() not in (x.lower() for x in out):
            out.append(t)
        if len(out) == 2:
            break
    return ", ".join(out)


def coord(v, lo, hi):
    try:
        f = float(v)
    except (TypeError, ValueError):
        return None
    if math.isnan(f) or f < lo or f > hi:
        return None
    return f


# ---------------------------------------------------------------- borders


class Borders:
    """Which country a point is in, from Natural Earth's admin-0 country
    shapes (ne_50m_admin_0_countries.geojson). None means the sea, or a
    place too small for the 1:50m shapes."""

    def __init__(self, path):
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
        self.shapes = []  # (cc, (lon0, lat0, lon1, lat1), [outer, hole, ...])
        self.sovereign = {}  # country code -> sovereign state (SOV_A3): IM, GB -> GBR
        # country code -> its name (ADMIN), for a station moved to a country
        # that no other station in the snapshot is in
        self.names = {}
        # country code -> (lat, lon) of Natural Earth's label point, inside
        # the country: where its "Country-wide" place goes
        self.labels = {}
        for feat in data.get("features", []):
            props = feat.get("properties") or {}
            # ISO_A2 is -99 for France and Norway; ISO_A2_EH has them.
            cc = props.get("ISO_A2_EH") or props.get("ISO_A2") or ""
            geom = feat.get("geometry") or {}
            if len(cc) != 2 or not cc.isalpha():
                continue
            if props.get("SOV_A3"):
                self.sovereign[cc.upper()] = props["SOV_A3"]
            name = country_name(props.get("ADMIN"))
            if name:
                self.names.setdefault(cc.upper(), name)
            lx = coord(props.get("LABEL_X"), -180, 180)
            ly = coord(props.get("LABEL_Y"), -90, 90)
            if lx is not None and ly is not None:
                self.labels.setdefault(cc.upper(), (ly, lx))
            polys = geom.get("coordinates") or []
            if geom.get("type") == "Polygon":
                polys = [polys]
            elif geom.get("type") != "MultiPolygon":
                continue
            for poly in polys:
                if not poly or not poly[0]:
                    continue
                xs = [p[0] for p in poly[0]]
                ys = [p[1] for p in poly[0]]
                self.shapes.append((cc.upper(), (min(xs), min(ys), max(xs), max(ys)), poly))

    @staticmethod
    def _in_ring(ring, x, y):
        inside = False
        j = len(ring) - 1
        for i in range(len(ring)):
            xi, yi = ring[i][0], ring[i][1]
            xj, yj = ring[j][0], ring[j][1]
            if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
                inside = not inside
            j = i
        return inside

    def same_state(self, a, b):
        """True if two country codes are one sovereign state (GB and IM)."""
        return a == b or (a in self.sovereign and self.sovereign.get(a) == self.sovereign.get(b))

    def at(self, lat, lon):
        for cc, (x0, y0, x1, y1), poly in self.shapes:
            if x0 <= lon <= x1 and y0 <= lat <= y1 and self._in_ring(poly[0], lon, lat) \
                    and not any(self._in_ring(h, lon, lat) for h in poly[1:]):
                return cc
        return None


# ------------------------------------------------------------------ cities


class Cities:
    """Nearest-city lookup over a GeoNames cities file, bucketed by whole
    degrees so each lookup only scans nearby cells."""

    def __init__(self, path):
        self.cells = {}
        self.count = 0
        self.countries = set()
        self.capitals = {}  # country code -> (lat, lon, name, pop)
        self.by_name = {}   # (country code, folded name) -> (lat, lon, name, pop)
        self.big_by_name = {}  # folded name -> (lat, lon, name, pop, cc), pop >= NAMED_TOWN_POP
        self.country_names = {}  # country code -> name, filled in from the stations
        self.borders = None  # a Borders, if given
        # For stations without coordinates (rfold keys):
        self.towns = {}    # (cc, name) -> (lat, lon, name, pop, admin1); other names if big
        self.seats = {}    # (cc, admin1) -> ((is seat, pop), (lat, lon, name, pop))
        self.regions = {}  # (cc, region name) -> admin1 code, from load_admin1()
        for row in self._rows(path):
            parts = row.split("\t")
            if len(parts) < 15:
                continue
            try:
                lat, lon = float(parts[4]), float(parts[5])
            except ValueError:
                continue
            name = clean(parts[1], 60)
            # "Halle (Saale)" is "Halle" in a station's name.
            short = name.split(" (")[0].strip()
            if not name or parts[7] in SKIP_FEATURES:
                continue
            try:
                pop = int(parts[14] or 0)
            except ValueError:
                pop = 0
            key = (math.floor(lat), math.floor(lon))
            cc = parts[8]
            # (lat, lon, name, country code, population, admin1 code)
            self.cells.setdefault(key, []).append((lat, lon, name, cc, pop, parts[10]))
            self.count += 1
            self.countries.add(cc)
            entry = (lat, lon, name, pop)
            if parts[7] == "PPLC" and pop >= self.capitals.get(cc, (0, 0, "", -1))[3]:
                self.capitals[cc] = entry
            names = [name, clean(parts[2], 60), short]
            if pop >= ALT_NAME_POP:
                names += [clean(a, 60) for a in parts[3].split(",")]
            for n in names:
                k = (cc, rfold(n))
                if len(k[1]) >= 3 and pop > self.towns.get(k, (0, 0, "", -1))[3]:
                    self.towns[k] = entry + (parts[10],)
            rank = (parts[7] in ("PPLA", "PPLC"), pop)
            k = (cc, parts[10])
            if parts[10] and (k not in self.seats or rank > self.seats[k][0]):
                self.seats[k] = (rank, entry)
            for n in {name, clean(parts[2], 60), short}:
                k = (cc, fold(n))
                if n and pop > self.by_name.get(k, (0, 0, "", -1))[3]:
                    self.by_name[k] = entry
                if n and pop >= NAMED_TOWN_POP and pop > self.big_by_name.get(
                        fold(n), (0, 0, "", -1, ""))[3]:
                    self.big_by_name[fold(n)] = entry + (cc,)

    @staticmethod
    def _rows(path):
        if path.lower().endswith(".zip"):
            with zipfile.ZipFile(path) as z:
                names = [n for n in z.namelist() if n.lower().endswith(".txt")]
                if not names:
                    raise ValueError("no .txt file inside " + path)
                with z.open(names[0]) as f:
                    for line in io.TextIOWrapper(f, encoding="utf-8"):
                        yield line.rstrip("\n")
        else:
            with open(path, encoding="utf-8") as f:
                for line in f:
                    yield line.rstrip("\n")

    def _within(self, lat, lon, max_km):
        span = int(max_km / 111.0) + 1  # degrees of latitude to scan
        coslat = math.cos(math.radians(min(89.0, abs(lat) + span)))
        lon_span = min(180, int(span / max(coslat, 0.01)) + 1)
        for dy in range(-span, span + 1):
            for dx in range(-lon_span, lon_span + 1):
                cx = (math.floor(lon) + dx + 180) % 360 - 180
                for c in self.cells.get((math.floor(lat) + dy, cx), ()):
                    d = km(lat, lon, c[0], c[1])
                    if d <= max_km:
                        yield d, c

    def near_town(self, lat, lon, max_km, country_code=None):
        """True if a town (in country_code, if given) is within max_km."""
        return any(country_code is None or c[3] == country_code
                   for _, c in self._within(lat, lon, max_km))

    def territory(self, lat, lon):
        """(country code, label) where the TERRITORY rule applies, else None."""
        near = list(self._within(lat, lon, TERRITORY_TOWN_KM))
        town = min(near, key=lambda f: f[0])[1] if near else None
        if town:
            cc = town[3]
        else:
            cc = self.borders.at(lat, lon) if self.borders else None
            la0, lo0, la1, lo1 = CRIMEA_BOX
            if cc in ("RU", None) and la0 <= lat <= la1 and lo0 <= lon <= lo1:
                cc = "UA"
        if cc == "UA":
            return "UA", "Ukraine"
        if cc == "PS":
            return "PS", "Gaza" if lon < GAZA_MAX_LON else "Occupied West Bank"
        if cc in CROWN_DEPENDENCIES:
            return cc, CROWN_DEPENDENCIES[cc]
        if cc in ("GB", None):
            if not town:
                uk = [f for f in self._within(lat, lon, UK_TOWN_KM) if f[1][3] == "GB"]
                town = min(uk, key=lambda f: f[0])[1] if uk else None
            if town and town[5] in UK_NATIONS:
                return "GB", UK_NATIONS[town[5]]
        return None

    def load_admin1(self, path):
        """GeoNames' admin1CodesASCII.txt: region names, and the aliases in
        REGION_ALIASES. Returns the number of regions."""
        n = 0
        with open(path, encoding="utf-8") as f:
            for line in f:
                parts = line.rstrip("\n").split("\t")
                if len(parts) < 3 or "." not in parts[0]:
                    continue
                cc, code = parts[0].split(".", 1)
                if (cc, code) not in self.seats:
                    continue
                n += 1
                for nm in (parts[1], parts[2]):
                    self.regions.setdefault((cc, rfold(nm)), code)
                # Letter codes are the usual abbreviations: US "TX", CH "ZH".
                if 2 <= len(code) <= 3 and all("A" <= ch <= "Z" for ch in code):
                    self.regions.setdefault((cc, code.lower()), code)
        for cc, aliases in REGION_ALIASES.items():
            for alias, code in aliases.items():
                if (cc, code) in self.seats:
                    self.regions.setdefault((cc, alias), code)
        return n

    def seat(self, cc, code):
        return self.seats[(cc, code)][1]

    def region_place(self, cc, text, country):
        """(lat, lon, how, admin1) for Radio Browser's region field: a town
        ("region_town") or a region ("admin1", placed at its capital), or
        None."""
        c = rfold(country)
        tries = [text]
        parts = [text]
        for sep in (",", "/", "(", " - "):
            parts = [q for p in parts for q in p.split(sep)]
        if len(parts) > 1:
            tries += [p.strip(" )") for p in parts if p.strip(" )")]
        for t in tries:
            for v in region_variants(t, country):
                if v == c:
                    continue  # "Mexico" is a state of Mexico too
                town = self.towns.get((cc, v))
                code = self.regions.get((cc, v))
                if code and not (town and town[3] >= REGION_TOWN_WINS_POP):
                    seat = self.seat(cc, code)
                    return seat[0], seat[1], "admin1", code
                if town:
                    return town[0], town[1], "region_town", ""
                if cc == "GB" and v in GB_AREAS:
                    town = self.towns.get((cc, rfold(GB_AREAS[v])))
                    if town:
                        return town[0], town[1], "region_town", ""
        # "Seattle Washington", "Heraklion Crete": a town inside it.
        hit = self.town_in_name(text, cc)
        if hit:
            return hit[0], hit[1], "region_town", ""
        return None

    def town_in_name(self, name, cc, admin1=""):
        """A town of NOCOORD_TOWN_POP or more in country cc (and region
        admin1, if given) named in a station name: (lat, lon), or None.
        Longest match first."""
        words = rfold("".join(ch if ch.isalnum() else " " for ch in name)).split()
        for n in (3, 2, 1):
            for i in range(len(words) - n + 1):
                key = " ".join(words[i:i + n])
                if len(key) < 4 or key in TOWN_STOPWORDS:
                    continue
                town = self.towns.get((cc, key))
                if town and town[3] >= NOCOORD_TOWN_POP and (not admin1 or town[4] == admin1):
                    return town[0], town[1]
        return None

    def region_in_text(self, text, cc, country):
        """A region (or, in the UK, a county) named in a station's name or
        tags: (lat, lon), or None. Longest match first."""
        words = rfold("".join(ch if ch.isalnum() else " " for ch in text)).split()
        for n in (4, 3, 2, 1):
            for i in range(len(words) - n + 1):
                key = " ".join(words[i:i + n])
                if len(key) < 5 or all(w in REGION_GENERIC for w in key.split()) \
                        or key == rfold(country):
                    continue
                if cc == "GB" and key in GB_AREAS:
                    town = self.towns.get((cc, rfold(GB_AREAS[key])))
                    if town:
                        return town[0], town[1]
                code = self.regions.get((cc, key))
                if code:
                    c = self.seat(cc, code)
                    return c[0], c[1]
        return None

    def place_without_coords(self, cc, country, name, region, tags, hosts, sisters):
        """(lat, lon, how, country code) for a station with no coordinates,
        or None. how is "sister", "region_town", "name_town", "admin1",
        "name_region" or "country". hosts: the station's website and stream
        hosts; a sister must share one."""
        sister = sisters.get((cc, fold(name)))
        if sister and sister[3] & hosts:
            return sister[0], sister[1], "sister", sister[2]
        hit = self.region_place(cc, region, country) if region else None
        if hit and hit[2] == "region_town":
            return hit[0], hit[1], "region_town", cc
        # A town in the name is more precise than a region ("Joliet",
        # region "Illinois"), but only one in that region.
        town = self.town_in_name(name, cc, hit[3] if hit else "")
        if town:
            return town[0], town[1], "name_town", cc
        if hit:
            return hit[0], hit[1], "admin1", cc
        hit = self.region_in_text(name + " " + (tags or "").replace(",", " , "), cc, country)
        if hit:
            return hit[0], hit[1], "name_region", cc
        label = self.borders.labels.get(cc) if self.borders else None
        if label:
            return label[0], label[1], "country", cc
        return None

    def named_town(self, name, cc):
        """A town of NAMED_TOWN_POP or more named in a station name, as
        (lat, lon, name, pop, cc): one in country cc if there is one, else
        the biggest anywhere. Matches whole words, up to three of them."""
        words = fold("".join(ch if ch.isalnum() else " " for ch in name)).split()
        hits = []
        for n in (3, 2, 1):
            for i in range(len(words) - n + 1):
                key = " ".join(words[i:i + n])
                if len(key) < 4:
                    continue
                local = self.by_name.get((cc, key))
                if local and local[3] >= NAMED_TOWN_POP:
                    return local + (cc,)
                big = self.big_by_name.get(key)
                if big and big[3] >= NAMED_ABROAD_POP:
                    hits.append(big)
        return max(hits, key=lambda h: h[3]) if hits else None

    def check_position(self, lat, lon, cc, region, name=""):
        """(lat, lon, how, country code) for a station, or None to drop it.
        how is "ok", "swapped", "flipped", "named", "trusted", "region" or
        "capital"; the country code changes for "named" and "trusted"."""
        b = self.borders
        at = b.at(lat, lon) if b else None
        if cc not in self.countries:
            # No country to check against: only rule out the open sea.
            if at or self.near_town(lat, lon, NEAR_OWN_KM if b else GEO_CHECK_KM):
                return lat, lon, "ok", cc
            return None
        if b:
            # In the country (or a dependency of the same state), or near one
            # of its towns (the 1:50m coastline is coarse, so harbour towns
            # can fall just offshore).
            if (at and b.same_state(at, cc)) or self.near_town(lat, lon, NEAR_OWN_KM, cc):
                return lat, lon, "ok", cc
            # Out at sea off its own coast: to the nearest town ashore.
            if at is None:
                ashore = [f for f in self._within(lat, lon, GEO_CHECK_KM) if f[1][3] == cc]
                if ashore:
                    town = min(ashore, key=lambda f: f[0])[1]
                    return town[0], town[1], "ashore", cc
        elif self.near_town(lat, lon, GEO_CHECK_KM, cc):
            return lat, lon, "ok", cc
        for la, lo, how in ((lon, lat, "swapped"), (lat, -lon, "flipped"),
                            (-lat, lon, "flipped"), (-lat, -lon, "flipped"),
                            (lon, -lat, "swapped"), (-lon, lat, "swapped")):
            if -90 <= la <= 90 and -180 <= lo <= 180 and self.near_town(la, lo, GEO_FIX_KM, cc):
                return la, lo, how, cc
        if b and at and self.near_town(lat, lon, BORDER_AREA_KM, cc):
            return lat, lon, "ok", cc
        town = self.named_town(name, cc) if name else None
        if town:
            return town[0], town[1], "named", town[4]
        stated = fold(self.country_names.get(cc, ""))
        if not (stated and stated in fold(name)):
            big = [c for _, c in self._within(lat, lon, TRUST_CITY_KM) if c[4] >= TRUST_CITY_POP]
            if big:
                where = at or max(big, key=lambda c: c[4])[3]
                return lat, lon, "trusted", where
        if region:
            hit = self.by_name.get((cc, fold(region)))
            if hit:
                return hit[0], hit[1], "region", cc
        if cc in self.capitals:
            c = self.capitals[cc]
            return c[0], c[1], "capital", cc
        return None

    def nearest(self, lat, lon, max_km, country_code=""):
        """(name, km) of the city for a station, or (None, None) if none is
        within max_km. Within BIG_CITY_KM the most populous city wins (a
        station in a suburb belongs to the city); beyond that, the nearest.
        Cities in the station's own country are preferred over ones across
        a border."""
        span = int(max_km / 111.0) + 1  # degrees of latitude to scan
        coslat = math.cos(math.radians(min(89.0, abs(lat) + span)))
        lon_span = min(180, int(span / max(coslat, 0.01)) + 1)
        found = []
        for dy in range(-span, span + 1):
            for dx in range(-lon_span, lon_span + 1):
                cx = (math.floor(lon) + dx + 180) % 360 - 180
                for c in self.cells.get((math.floor(lat) + dy, cx), ()):
                    d = km(lat, lon, c[0], c[1])
                    if d <= max_km:
                        found.append((d, c))
        if country_code:
            same = [f for f in found if f[1][3] == country_code]
            if same:
                found = same
        if not found:
            return (None, None)
        near = [f for f in found if f[0] <= BIG_CITY_KM]
        if near:
            d, c = max(near, key=lambda f: (f[1][4], -f[0]))
        else:
            d, c = min(found, key=lambda f: f[0])
        return (c[2], d)


def fold(s):
    """Lower case without accents, for matching town names."""
    s = unicodedata.normalize("NFKD", s or "")
    return " ".join("".join(ch for ch in s if not unicodedata.combining(ch)).lower().split())


def rfold(s):
    """fold() for region and town names: hyphens, apostrophes and full
    stops are spaces ("Baden-Württemberg", "St. Gallen")."""
    s = fold(s)
    for ch in "-'.\u2019":
        s = s.replace(ch, " ")
    return " ".join(s.split())


def region_variants(text, country):
    """Ways to read Radio Browser's region field: as written; without a
    "(...)" ending; without a trailing abbreviation ("Sydney NSW"); without
    the country's name ("Athens Greece"); without words like "province"."""
    out = []

    def add(v):
        v = rfold(v)
        if v and v not in out:
            out.append(v)

    t = clean(text, 100)
    add(t)
    t = t.split(" (")[0].strip()
    add(t)
    words = t.split()
    if len(words) > 1 and 2 <= len(words[-1]) <= 3 and all("A" <= ch <= "Z" for ch in words[-1]):
        t = " ".join(words[:-1])
        add(t)
    c = rfold(country)
    v = rfold(t)
    if c and v.endswith(" " + c):
        add(v[: -len(c) - 1])
    for suffix in (" province", " state", " region", " oblast", " county", " prefecture",
                   " city", " town"):
        if v.endswith(suffix):
            add(v[: -len(suffix)])
    for prefix in ("province of ", "state of ", "region of ", "city of ", "provincia de ",
                   "provincia di ", "region de ", "estado de ", "departamento de ", "grad "):
        if v.startswith(prefix):
            add(v[len(prefix):])
    return out


def is_test(name):
    words = "".join(ch if ch.isalnum() else " " for ch in name.lower()).split()
    return any(w in TEST_WORDS for w in words)


def km(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp, dl = p2 - p1, math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 6371.0 * 2 * math.asin(min(1.0, math.sqrt(a)))


# --------------------------------------------------------------- the API


def servers():
    """Radio Browser servers in random order, found the documented way: a
    DNS lookup of all.api.radio-browser.info, then each address's name."""
    try:
        infos = socket.getaddrinfo(DNS_NAME, 443, proto=socket.IPPROTO_TCP)
    except socket.gaierror as e:
        raise SystemExit("DNS lookup of %s failed: %s" % (DNS_NAME, e))
    names = set()
    for info in infos:
        ip = info[4][0]
        try:
            names.add(socket.gethostbyaddr(ip)[0])
        except OSError:
            continue
    names = [n for n in names if n.endswith(".radio-browser.info")]
    if not names:
        raise SystemExit("no Radio Browser servers found via " + DNS_NAME)
    random.shuffle(names)
    return names


class Api:
    def __init__(self):
        self.hosts = servers()
        log("Radio Browser servers:", ", ".join(self.hosts))

    def get(self, path, params):
        query = urllib.parse.urlencode(params)
        last = None
        while self.hosts:
            host = self.hosts[0]
            url = "https://%s%s?%s" % (host, path, query)
            req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            # A dropped connection is often a blip: retry the same server
            # a few times before giving up on it.
            for attempt in range(RETRIES):
                try:
                    with urllib.request.urlopen(req, timeout=TIMEOUT_S) as r:
                        return json.loads(r.read().decode("utf-8"))
                except (urllib.error.URLError, OSError, ValueError) as e:
                    last = e
                    if attempt + 1 < RETRIES:
                        log("  %s failed (%s), retrying in %d s" % (host, e, 2 << attempt))
                        time.sleep(2 << attempt)
            log("  %s failed %d times, trying the next server" % (host, RETRIES))
            self.hosts.pop(0)
        raise SystemExit("all Radio Browser servers failed; last error: %s" % last)


def fetch(api, args):
    """Yields raw station objects, most-clicked first, page by page."""
    base = {} if args.without_coords else {"has_geo_info": "true"}
    base.update({
        "hidebroken": "true",
        "order": "clickcount",
        "reverse": "true",
        "limit": PAGE_SIZE,
    })
    if len(args.codecs) == 1:
        base["codec"] = CODECS[args.codecs[0]]
        base["codecExact"] = "true"
    if args.min_bitrate:
        base["bitrateMin"] = args.min_bitrate
    if args.tag:
        base["tag"] = args.tag
    # Enough raw results to fill the cap after filtering, but bounded.
    wanted = max(args.max * 4, 2000)
    for cc in args.countries or [None]:
        params = dict(base)
        if cc:
            params["countrycode"] = cc
        offset = 0
        got = 0
        while got < wanted:
            params["offset"] = offset
            page = api.get("/json/stations/search", params)
            if not isinstance(page, list):
                raise SystemExit("unexpected reply from Radio Browser")
            log("  %s offset %d: %d stations" % (cc or "all countries", offset, len(page)))
            for st in page:
                yield st
            got += len(page)
            if len(page) < PAGE_SIZE:
                break
            offset += PAGE_SIZE
            time.sleep(PAGE_DELAY_S)


# ------------------------------------------------------------- conversion


def sister_positions(stations, cities):
    """(country code, folded name) -> (lat, lon, country code) of stations
    with coordinates, checked as convert() checks them; the most popular
    wins. Stations without coordinates are placed at their sister's."""
    out = {}
    for st in stations:
        if st.get("lastcheckok") not in (1, True, "1"):
            continue
        lat = coord(st.get("geo_lat"), -90, 90)
        lon = coord(st.get("geo_long"), -180, 180)
        if lat is None or lon is None or (abs(lat) < 1e-6 and abs(lon) < 1e-6):
            continue
        name = clean_name(st.get("name"))
        cc = clean(st.get("countrycode"), 2).upper()
        key = (cc, fold(name))
        if not name or not cc or key in out:
            continue
        pos = cities.check_position(lat, lon, cc, clean(st.get("state"), 60), name)
        if pos:
            out[key] = (pos[0], pos[1], pos[3], station_hosts(st))
    return out


def host_of(url):
    """A URL's host name, lower case, without "www."; "" if none."""
    url = (url or "").strip().lower()
    if "://" not in url:
        return ""
    host = url.split("://", 1)[1].split("/", 1)[0].split("?", 1)[0].split("#", 1)[0]
    host = host.rsplit("@", 1)[-1].split(":", 1)[0]
    return host[4:] if host.startswith("www.") else host


def station_hosts(st):
    """The hosts of a station's website and stream, for telling another
    stream of the same station from a different station of the same name."""
    return {h for h in (host_of(st.get("homepage")), host_of(st.get("url_resolved"))) if h}


def convert(stations, args, cities):
    allowed = {CODECS[c] for c in args.codecs}
    seen_uuid, seen_url = set(), set()
    skipped = {}
    out = []
    without = bool(getattr(args, "without_coords", False) and cities)
    sisters = sister_positions(stations, cities) if without else {}

    def skip(why):
        skipped[why] = skipped.get(why, 0) + 1

    for st in stations:
        uuid = clean(st.get("stationuuid"), 40)
        if uuid in seen_uuid:
            continue  # the same station on two pages
        seen_uuid.add(uuid)
        is_hls = st.get("hls") in (1, True, "1")
        if is_hls and not args.hls:
            skip("HLS stream")
            continue
        if st.get("lastcheckok") not in (1, True, "1"):
            skip("last check failed")
            continue
        lat = coord(st.get("geo_lat"), -90, 90)
        lon = coord(st.get("geo_long"), -180, 180)
        no_coords = lat is None or lon is None or (abs(lat) < 1e-6 and abs(lon) < 1e-6)
        if no_coords and not without:
            skip("no usable coordinates")
            continue
        codec = clean(st.get("codec"), 20).upper()
        if is_hls:
            # The app finds out what is inside; Radio Browser often doesn't
            # know ("UNKNOWN").
            codec = "HLS" if codec in ("", "UNKNOWN") else codec + "/HLS"
        elif codec not in allowed:
            skip("codec %s not selected" % (codec or "unknown"))
            continue
        try:
            bitrate = int(st.get("bitrate") or 0)
        except (TypeError, ValueError):
            bitrate = 0
        if args.min_bitrate and bitrate < args.min_bitrate:
            skip("bitrate below %dk or unknown" % args.min_bitrate)
            continue
        url = clean_url(st.get("url_resolved"))
        if not url:
            skip("no usable stream URL")
            continue
        homepage = clean_homepage(st.get("homepage"))
        if any(m in url.lower() for m in RADIO_GARDEN_MARKS) or (
                homepage and any(m in homepage.lower() for m in RADIO_GARDEN_MARKS)):
            skip("URL refers to Radio Garden")
            continue
        if url in seen_url:
            skip("duplicate stream URL")
            continue
        name = clean_name(st.get("name"))
        if not name:
            skip("no name")
            continue
        if is_test(name):
            skip("test broadcast")
            continue
        country = country_name(st.get("country"))
        cc = clean(st.get("countrycode"), 2).upper()
        geo = "ok"
        if cities:
            if no_coords:
                pos = cities.place_without_coords(cc, country, name, clean(st.get("state"), 60),
                                                  clean(st.get("tags"), 300), station_hosts(st),
                                                  sisters) if cc else None
                if pos is None:
                    skip("no coordinates, and no country to place it in")
                    continue
            else:
                pos = cities.check_position(lat, lon, cc, clean(st.get("state"), 60), name)
            if pos is None:
                skip("coordinates in the open sea, no country to fall back on")
                continue
            lat, lon, geo, new_cc = pos
            if new_cc != cc:
                # The position wins: the name Radio Browser uses for that
                # country, or the borders file's when no station in the
                # snapshot is in it (a download for some countries only).
                new_name = cities.country_names.get(new_cc) or (
                    cities.borders.names.get(new_cc, "") if cities.borders else "")
                if new_name:
                    country, cc = new_name, new_cc
            terr = cities.territory(lat, lon) if geo != "country" else None
            if terr and terr[1] != country:
                cc, country = terr
                relabelled = True
            else:
                relabelled = False
        else:
            relabelled = False
        city, city_from = "", "none"
        if geo == "country":
            city, city_from = COUNTRY_WIDE, "country"
        elif cities:
            found, _ = cities.nearest(lat, lon, args.city_km, cc)
            if found:
                city, city_from = found, "city"
        if not city and geo != "country":
            # No town nearby: Radio Browser's region (state/province), so
            # the station joins a named place rather than one named by its
            # coordinates.
            region = clean(st.get("state"), 60)
            if region and region.lower() != country.lower() and not any(
                    ch.isdigit() for ch in region):
                city, city_from = region, "region"
        notes = "%s %s rb:%s" % (codec, ("%dk" % bitrate) if bitrate else "?k", uuid)
        if homepage and homepage.rstrip("/") != url.rstrip("/"):
            notes += " web:" + homepage
        if geo != "ok":
            notes += " " + GEO_NOTES[geo]
        if relabelled:
            notes += " (country: %s, where the position is)" % country
        line = " | ".join([
            name, url, country, city,
            "%.4f" % lat, "%.4f" % lon,
            genre_from_tags(st.get("tags")), notes,
        ])
        seen_url.add(url)
        out.append((line, codec, city_from, geo))
        if len(out) >= args.max:
            break
    return out, skipped


GEO_NOTES = {
    "swapped": "(position fixed: latitude and longitude were swapped)",
    "flipped": "(position fixed: a coordinate had the wrong sign)",
    "named": "(position: the town in the station's name; Radio Browser's was elsewhere)",
    "ashore": "(position: the nearest town; Radio Browser's was out at sea)",
    "trusted": "(country corrected to match the position)",
    "region": "(position: Radio Browser's region; its coordinates were outside the country)",
    "capital": "(position: the capital; Radio Browser's coordinates were outside the country)",
    "sister": "(position: another stream of this station; Radio Browser has no coordinates)",
    "region_town": "(position: the town given as its region; Radio Browser has no "
                   "coordinates)",
    "admin1": "(position: the capital of its region; Radio Browser has no coordinates)",
    "name_town": "(position: the town in the station's name; Radio Browser has no "
                 "coordinates)",
    "name_region": "(position: the capital of the region in its name or tags; Radio Browser "
                   "has no coordinates)",
    "country": "(position: none given; in its country's Country-wide place)",
}


def country_names(stations):
    """Country code -> the name Radio Browser uses most for it."""
    counts = {}
    for st in stations:
        cc = clean(st.get("countrycode"), 2).upper()
        name = country_name(st.get("country"))
        if cc and name:
            counts.setdefault(cc, {}).setdefault(name, 0)
            counts[cc][name] += 1
    return {cc: max(n, key=n.get) for cc, n in counts.items()}


def header(args, cities, count):
    when = time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime())
    opts = "codecs %s" % ",".join(args.codecs)
    if args.countries:
        opts += ", countries %s" % ",".join(args.countries)
    if args.min_bitrate:
        opts += ", min bitrate %dk" % args.min_bitrate
    if args.tag:
        opts += ", tag '%s'" % args.tag
    if args.hls:
        opts += ", HLS included"
    lines = [
        "# DX Garden stations.txt generated by tools/rb_to_stations.py on %s." % when,
        "# Source: a snapshot of Radio Browser (radio-browser.info) taken %s, %d stations, %s."
        % (time.strftime("%Y-%m-%d", time.gmtime(args.snapshot)), count, opts),
        "# Cities: nearest place within %g km from GeoNames (geonames.org, CC BY 4.0)%s;" % (
            args.city_km, "" if cities else " - not used"),
        "# otherwise Radio Browser's region, otherwise blank.",
        "# Fields: Name | Stream URL | Country | City | Latitude | Longitude | Genre | Notes",
        "# Notes: codec, bitrate, the Radio Browser station id (rb:<uuid>) and",
        "# the station's website (web:<url>) when Radio Browser has one.",
    ]
    if cities:
        lines.insert(4, "# Positions outside the station's country were checked and fixed "
                        "(see Notes).")
        if getattr(args, "without_coords", False):
            lines.insert(5, "# Stations without coordinates are placed by their region, a town "
                            "or region in their name, or in a Country-wide place (see Notes).")
    if args.bundle:
        lines = [
            "# DX Garden default station list, shipped with DX Garden %s." % args.bundle,
            "# Correct as of %s. Stations come and go: any that are dead or wrong are"
            % time.strftime("%Y-%m-%d", time.gmtime(args.snapshot)),
            "# yours to edit, delete or replace. This file is yours; the app never",
            "# overwrites it. Delete it to get the default list back on the next start.",
            "#",
        ] + lines
    return "\n".join(lines) + "\n"


# ------------------------------------------------------------------ check


def probe(url):
    """'ok', or a short reason the stream looks dead."""
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Icy-MetaData": "0"})
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            ctype = r.headers.get("Content-Type", "")
            data = r.read(4096)
            if not data:
                return "no data"
            if "text/html" in ctype:
                return "HTML page, not audio"
            return "ok"
    except urllib.error.HTTPError as e:
        return "HTTP %d" % e.code
    except http.client.BadStatusLine as e:
        # Shoutcast v1 answers "ICY 200 OK", which http.client rejects.
        return "ok" if "ICY 200" in str(e) else "bad reply: %s" % e
    except (urllib.error.URLError, OSError, http.client.HTTPException) as e:
        reason = getattr(e, "reason", e)
        return "no connection (%s)" % reason


def check(lines, n):
    sample = random.sample(lines, min(n, len(lines)))
    log("checking %d stream URLs..." % len(sample))
    dead = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        for line, result in zip(sample, pool.map(lambda l: probe(l.split(" | ")[1]), sample)):
            if result != "ok":
                dead.append((line.split(" | ")[0], line.split(" | ")[1], result))
    print("Check: %d of %d sampled streams did not answer as audio." % (len(dead), len(sample)))
    for name, url, why in dead:
        print("  %s: %s (%s)" % (name, url, why))


# ------------------------------------------------------------------- main


def parse_args(argv):
    ap = argparse.ArgumentParser(
        description="Build a DX Garden stations.txt from a Radio Browser snapshot.")
    ap.add_argument("-o", "--output", default="stations.txt", help="file to write")
    ap.add_argument("--geonames", help="GeoNames cities file (.zip or .txt), e.g. cities1000.zip")
    ap.add_argument("--no-city", action="store_true", help="leave every city blank")
    ap.add_argument("--borders", metavar="GEOJSON",
                    help="Natural Earth ne_50m_admin_0_countries.geojson, for checking that "
                         "each station is in its country")
    ap.add_argument("--city-km", type=float, default=DEFAULT_CITY_KM,
                    help="farthest a city can be from the station (default %(default)s km)")
    ap.add_argument("--countries", help="comma-separated ISO codes, e.g. GB,IE,FR")
    ap.add_argument("--codecs", default="mp3",
                    help="comma-separated: %s, or all (default mp3)" % ", ".join(CODECS))
    ap.add_argument("--hls", action="store_true",
                    help="also HLS streams, whatever their codec (the desktop app plays "
                         "them; the Vita doesn't)")
    ap.add_argument("--max-mb", type=int, default=DEFAULT_MAX_MB,
                    help="largest file to write, in MB (default %(default)s, the Vita's "
                         "limit; the desktop app reads up to 32)")
    ap.add_argument("--min-bitrate", type=int, default=0, help="in kbps; drops unknown bitrates")
    ap.add_argument("--tag", help="only stations with this tag, e.g. jazz")
    ap.add_argument("--max", type=int, default=DEFAULT_MAX,
                    help="most stations to write, most popular first (default %(default)s)")
    ap.add_argument("--without-coords", action="store_true",
                    help="also stations without coordinates, placed by their region, a town "
                         "in their name, or country-wide (needs --geonames; best with "
                         "--admin1 and --borders)")
    ap.add_argument("--admin1", metavar="FILE",
                    help="GeoNames admin1CodesASCII.txt (region names), for --without-coords")
    ap.add_argument("--save-raw", metavar="FILE",
                    help="also save the downloaded stations (JSON), to rebuild offline")
    ap.add_argument("--from-raw", metavar="FILE", action="append",
                    help="rebuild from a file saved with --save-raw instead of downloading "
                         "(more than one: their stations in that order)")
    ap.add_argument("--bundle", metavar="VERSION",
                    help="add the header for the list shipped with this app version")
    ap.add_argument("--check", type=int, metavar="N", default=0,
                    help="after writing, test N random stream URLs and report dead ones")
    args = ap.parse_args(argv)

    args.codecs = [c.strip().lower() for c in args.codecs.split(",") if c.strip()]
    if args.codecs == ["all"]:
        args.codecs = list(CODECS)
    for c in args.codecs:
        if c not in CODECS:
            ap.error("unknown codec '%s'; use %s" % (c, ", ".join(CODECS)))
    if args.countries:
        args.countries = [c.strip().upper() for c in args.countries.split(",") if c.strip()]
        for c in args.countries:
            if len(c) != 2 or not c.isalpha():
                ap.error("country codes are two letters, like GB or US: '%s'" % c)
    if args.max < 1:
        ap.error("--max must be at least 1")
    if not args.geonames and not args.no_city:
        ap.error("give --geonames PATH (e.g. cities1000.zip from "
                 "download.geonames.org/export/dump/) or --no-city")
    return args


def main(argv=None):
    args = parse_args(argv)
    cities = None
    if args.geonames and not args.no_city:
        cities = Cities(args.geonames)
        log("GeoNames: %d cities loaded from %s" % (cities.count, args.geonames))
        if args.borders:
            cities.borders = Borders(args.borders)
            log("Borders: %d shapes loaded from %s" % (len(cities.borders.shapes), args.borders))
        if args.admin1:
            log("Regions: %d loaded from %s" % (cities.load_admin1(args.admin1), args.admin1))

    if args.from_raw:
        raw = []
        for path in args.from_raw:
            with open(path, encoding="utf-8") as f:
                part = json.load(f)
            log("%d stations read from %s" % (len(part), path))
            raw += part
        # When the snapshot was taken: the saved file's time, not today.
        args.snapshot = os.path.getmtime(args.from_raw[0])
    else:
        args.snapshot = time.time()
        raw = list(fetch(Api(), args))
        if args.save_raw:
            with open(args.save_raw, "w", encoding="utf-8") as f:
                json.dump(raw, f)
    if cities:
        cities.country_names = country_names(raw)
    lines, skipped = convert(raw, args, cities)
    text = header(args, cities, len(lines)) + "".join(l[0] + "\n" for l in lines)
    data = text.encode("utf-8")
    if len(data) > args.max_mb * 1024 * 1024:
        raise SystemExit("output would be %d bytes, over the %d MB limit; lower --max"
                         % (len(data), args.max_mb))
    tmp = args.output + ".part"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, args.output)

    per_codec = {}
    for _, codec, _, _ in lines:
        per_codec[codec] = per_codec.get(codec, 0) + 1
    print("Wrote %d stations to %s (%d KB)." % (len(lines), args.output, len(data) // 1024))
    with_city = sum(1 for _, _, c, _ in lines if c == "city")
    with_region = sum(1 for _, _, c, _ in lines if c == "region")
    geo = {}
    for _, _, _, g in lines:
        geo[g] = geo.get(g, 0) + 1
    with_country = sum(1 for _, _, c, _ in lines if c == "country")
    print("  Place: nearest city within %g km %d, Radio Browser region %d, none "
          "(named by coordinates on the Vita) %d%s."
          % (args.city_km, with_city, with_region,
             len(lines) - with_city - with_region - with_country,
             ", country-wide %d" % with_country if args.without_coords else ""))
    print("  Per codec: " + ", ".join("%s %d" % kv for kv in sorted(per_codec.items())))
    if cities:
        print("  Positions: ok %d, swapped %d, sign flipped %d, brought ashore %d, "
              "town in name %d, country corrected %d, moved to region %d, moved to "
              "capital %d." % (geo.get("ok", 0), geo.get("swapped", 0), geo.get("flipped", 0),
                               geo.get("ashore", 0), geo.get("named", 0),
                               geo.get("trusted", 0), geo.get("region", 0),
                               geo.get("capital", 0)))
        if args.without_coords:
            print("  Without coordinates: another stream %d, region's town %d, region %d, "
                  "town in name %d, region in name %d, country-wide %d."
                  % (geo.get("sister", 0), geo.get("region_town", 0), geo.get("admin1", 0),
                     geo.get("name_town", 0), geo.get("name_region", 0),
                     geo.get("country", 0)))
    if skipped:
        print("Skipped:")
        for why, n in sorted(skipped.items(), key=lambda kv: -kv[1]):
            print("  %6d  %s" % (n, why))
    if args.check:
        check([l[0] for l in lines], args.check)
    return 0


if __name__ == "__main__":
    sys.exit(main())
