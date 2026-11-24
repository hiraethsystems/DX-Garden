/*
 * See rb_build.h. Each function follows the one of the same name in
 * tools/rb_to_stations.py, in the same order of operations, so that ties
 * (the first of equals wins) and rounding come out the same.
 */
/* strdup, strtok_r, gmtime_r */
#define _POSIX_C_SOURCE 200809L
#include "rb_build.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strmap.h"
#include "ustr.h"

#define DEG2RAD (3.141592653589793 / 180.0)

static const char *const CODEC_OPT[] = { "mp3", "aac", "aac+", "ogg", "opus", "flac" };
static const char *const CODEC_RB[] = { "MP3", "AAC", "AAC+", "OGG", "OPUS", "FLAC" };
#define N_CODECS 6

static const char *const COUNTRY_NAMES[][2] = {
    { "united states of america", "United States" },
    { "united kingdom of great britain and northern ireland", "United Kingdom" },
    { "russian federation", "Russia" },
    { "republic of korea", "South Korea" },
    { "democratic people's republic of korea", "North Korea" },
    { "iran (islamic republic of)", "Iran" },
    { "islamic republic of iran", "Iran" },
    { "bolivia (plurinational state of)", "Bolivia" },
    { "plurinational state of bolivia", "Bolivia" },
    { "venezuela (bolivarian republic of)", "Venezuela" },
    { "bolivarian republic of venezuela", "Venezuela" },
    { "taiwan, republic of china", "Taiwan" },
    { "taiwan, province of china", "Taiwan" },
    { "viet nam", "Vietnam" },
    { "republic of moldova", "Moldova" },
    { "united republic of tanzania", "Tanzania" },
    { "lao people's democratic republic", "Laos" },
    { "syrian arab republic", "Syria" },
    { "czech republic", "Czechia" },
    { "democratic republic of the congo", "DR Congo" },
    { "state of palestine", "Palestine" },
    { "republic of north macedonia", "North Macedonia" },
    { "holy see", "Vatican City" },
    { "democratic peoples republic of korea", "North Korea" },
    { "coted ivoire", "C\xC3\xB4te d'Ivoire" },
    { "cote d'ivoire", "C\xC3\xB4te d'Ivoire" },
    { "dutch part sint maarten", "Sint Maarten" },
    { "falkland islands malvinas", "Falkland Islands (Malvinas)" },
    { "reunion", "R\xC3\xA9union" },
};
static const char *const SMALL_WORDS[] = { "And", "Of", "The" };
static const char *const RADIO_GARDEN_MARKS[] = { "radio-garden", "radiogarden", "radio_garden",
                                                  "radio.garden" };
static const char *const SKIP_FEATURES[] = { "PPLX", "PPLH", "PPLQ", "PPLW", "PPLCH" };

#define BIG_CITY_KM 20.0
#define GEO_CHECK_KM 200.0
#define GEO_FIX_KM 30.0
#define TRUST_CITY_KM 15.0
#define TRUST_CITY_POP 40000
#define NAMED_TOWN_POP 50000
#define NAMED_ABROAD_POP 200000
#define NEAR_OWN_KM 30.0
#define BORDER_AREA_KM 300.0
#define UK_TOWN_KM 60.0
#define TERRITORY_TOWN_KM 15.0
static const double CRIMEA_BOX[4] = { 44.3, 32.4, 46.2, 36.65 };
#define GAZA_MAX_LON 34.6
/* Stations without coordinates (see the script's comment on COUNTRY_WIDE). */
#define COUNTRY_WIDE "Country-wide"
#define NOCOORD_TOWN_POP 20000
#define ALT_NAME_POP 15000
#define REGION_TOWN_WINS_POP 1000000

/* Generated from tools/rb_to_stations.py (REGION_ALIASES, GB_AREAS, TOWN_STOPWORDS,
 * REGION_GENERIC): keep in step with the script. */
static const char *const REGION_ALIASES[][3] = {
    { "AU", "nsw", "02" },
    { "AU", "vic", "07" },
    { "AU", "qld", "04" },
    { "AU", "wa", "08" },
    { "AU", "sa", "05" },
    { "AU", "tas", "06" },
    { "AU", "nt", "03" },
    { "AU", "act", "01" },
    { "AT", "wien", "09" },
    { "AT", "tirol", "07" },
    { "AT", "steiermark", "06" },
    { "AT", "oberosterreich", "04" },
    { "AT", "niederosterreich", "03" },
    { "AT", "karnten", "02" },
    { "BE", "bruxelles", "BRU" },
    { "BE", "brussel", "BRU" },
    { "BE", "brussels", "BRU" },
    { "BE", "wallonie", "WAL" },
    { "BE", "vlaanderen", "VLG" },
    { "BR", "ac", "01" },
    { "BR", "al", "02" },
    { "BR", "ap", "03" },
    { "BR", "am", "04" },
    { "BR", "ba", "05" },
    { "BR", "ce", "06" },
    { "BR", "df", "07" },
    { "BR", "es", "08" },
    { "BR", "go", "29" },
    { "BR", "ma", "13" },
    { "BR", "mt", "14" },
    { "BR", "ms", "11" },
    { "BR", "mg", "15" },
    { "BR", "pa", "16" },
    { "BR", "pb", "17" },
    { "BR", "pr", "18" },
    { "BR", "pe", "30" },
    { "BR", "pi", "20" },
    { "BR", "rj", "21" },
    { "BR", "rn", "22" },
    { "BR", "rs", "23" },
    { "BR", "ro", "24" },
    { "BR", "rr", "25" },
    { "BR", "sc", "26" },
    { "BR", "sp", "27" },
    { "BR", "se", "28" },
    { "BR", "to", "31" },
    { "CA", "ab", "01" },
    { "CA", "bc", "02" },
    { "CA", "mb", "03" },
    { "CA", "nb", "04" },
    { "CA", "nl", "05" },
    { "CA", "ns", "07" },
    { "CA", "on", "08" },
    { "CA", "pe", "09" },
    { "CA", "pei", "09" },
    { "CA", "qc", "10" },
    { "CA", "que", "10" },
    { "CA", "quebec", "10" },
    { "CA", "sk", "11" },
    { "CA", "yt", "12" },
    { "CA", "nt", "13" },
    { "CA", "nu", "14" },
    { "CA", "newfoundland", "05" },
    { "CH", "zurich", "ZH" },
    { "CH", "geneve", "GE" },
    { "CH", "genf", "GE" },
    { "CH", "luzern", "LU" },
    { "CH", "graubunden", "GR" },
    { "CH", "st gallen", "SG" },
    { "CH", "basel", "BS" },
    { "CH", "wallis", "VS" },
    { "CH", "tessin", "TI" },
    { "CH", "waadt", "VD" },
    { "CN", "kiangsu", "04" },
    { "CN", "hopei", "10" },
    { "CN", "shantung", "25" },
    { "CN", "chekiang", "02" },
    { "CN", "szechuan", "32" },
    { "CN", "szechwan", "32" },
    { "CN", "kwangtung", "30" },
    { "CN", "sinkiang", "13" },
    { "CN", "anhwei", "01" },
    { "CN", "kansu", "15" },
    { "CN", "shansi", "24" },
    { "CN", "hupei", "12" },
    { "CN", "hupeh", "12" },
    { "CN", "fukien", "07" },
    { "CN", "kiangsi", "03" },
    { "CN", "kwangsi", "16" },
    { "CN", "shensi", "26" },
    { "CN", "honan", "09" },
    { "CN", "hunan", "11" },
    { "CN", "kweichow", "18" },
    { "CN", "yunnan", "29" },
    { "CN", "kirin", "05" },
    { "CN", "heilungkiang", "08" },
    { "CN", "tsinghai", "06" },
    { "CN", "peking", "22" },
    { "CN", "inner mongolia", "20" },
    { "CN", "nei mongol", "20" },
    { "DE", "bayern", "02" },
    { "DE", "hessen", "05" },
    { "DE", "nordrhein westfalen", "07" },
    { "DE", "nrw", "07" },
    { "DE", "niedersachsen", "06" },
    { "DE", "sachsen", "13" },
    { "DE", "sachsen anhalt", "14" },
    { "DE", "thuringen", "15" },
    { "DE", "baden wurttemberg", "01" },
    { "DE", "bw", "01" },
    { "DE", "rheinland pfalz", "08" },
    { "DE", "berlin", "16" },
    { "DE", "brandenburg", "11" },
    { "DE", "mecklenburg vorpommern", "12" },
    { "DE", "saarland", "09" },
    { "DE", "schleswig holstein", "10" },
    { "DE", "hamburg", "04" },
    { "DE", "bremen", "03" },
    { "DE", "sassonia", "13" },
    { "DE", "saxe", "13" },
    { "DE", "baviera", "02" },
    { "DE", "rhineland palatinate", "08" },
    { "ES", "catalunya", "56" },
    { "ES", "cataluna", "56" },
    { "ES", "andalucia", "51" },
    { "ES", "euskadi", "59" },
    { "ES", "pais vasco", "59" },
    { "ES", "comunidad de madrid", "29" },
    { "ES", "comunitat valenciana", "60" },
    { "ES", "comunidad valenciana", "60" },
    { "ES", "castilla y leon", "55" },
    { "ES", "castilla la mancha", "54" },
    { "ES", "islas baleares", "07" },
    { "ES", "illes balears", "07" },
    { "ES", "canarias", "53" },
    { "ES", "islas canarias", "53" },
    { "ES", "navarra", "32" },
    { "ES", "aragon", "52" },
    { "FR", "bretagne", "53" },
    { "FR", "normandie", "28" },
    { "FR", "nouvelle aquitaine", "75" },
    { "FR", "corse", "94" },
    { "FR", "ile de france", "11" },
    { "FR", "provence alpes cote d azur", "93" },
    { "FR", "paca", "93" },
    { "FR", "centre val de loire", "24" },
    { "IT", "lombardia", "09" },
    { "IT", "piemonte", "12" },
    { "IT", "sicilia", "15" },
    { "IT", "puglia", "13" },
    { "IT", "marche", "10" },
    { "IT", "toscana", "16" },
    { "IT", "sardegna", "14" },
    { "IT", "lazio", "07" },
    { "IT", "veneto", "20" },
    { "IT", "emilia romagna", "05" },
    { "IT", "friuli", "06" },
    { "IT", "trentino", "17" },
    { "IT", "alto adige", "17" },
    { "IT", "sudtirol", "17" },
    { "IT", "valle d aosta", "19" },
    { "IT", "basilicata", "02" },
    { "IT", "liguria", "08" },
    { "IT", "trentino alto adige", "17" },
    { "NL", "noord holland", "07" },
    { "NL", "zuid holland", "11" },
    { "NL", "noord brabant", "06" },
    { "NL", "fryslan", "02" },
    { "NL", "friesland", "02" },
    { "PL", "malopolska", "77" },
    { "PL", "malopolskie", "77" },
    { "PL", "mazowsze", "78" },
    { "PL", "mazowieckie", "78" },
    { "PL", "slask", "83" },
    { "PL", "slaskie", "83" },
    { "PL", "pomorze", "82" },
    { "PL", "pomorskie", "82" },
    { "PL", "wielkopolska", "86" },
    { "PL", "wielkopolskie", "86" },
    { "PL", "dolny slask", "72" },
    { "PL", "dolnoslaskie", "72" },
    { "PL", "podkarpacie", "80" },
    { "PL", "podkarpackie", "80" },
};
static const char *const GB_AREAS[][2] = {
    { "yorkshire", "Leeds" },
    { "cornwall", "Truro" },
    { "devon", "Exeter" },
    { "kent", "Maidstone" },
    { "essex", "Chelmsford" },
    { "sussex", "Brighton" },
    { "surrey", "Guildford" },
    { "norfolk", "Norwich" },
    { "suffolk", "Ipswich" },
    { "lancashire", "Preston" },
    { "cumbria", "Carlisle" },
    { "dorset", "Dorchester" },
    { "somerset", "Taunton" },
    { "hampshire", "Winchester" },
    { "merseyside", "Liverpool" },
    { "midlands", "Birmingham" },
    { "tyneside", "Newcastle upon Tyne" },
    { "teesside", "Middlesbrough" },
    { "cheshire", "Chester" },
    { "derbyshire", "Derby" },
    { "lincolnshire", "Lincoln" },
    { "northumberland", "Morpeth" },
    { "wiltshire", "Salisbury" },
    { "gloucestershire", "Gloucester" },
    { "oxfordshire", "Oxford" },
    { "berkshire", "Reading" },
    { "hertfordshire", "Hertford" },
    { "cambridgeshire", "Cambridge" },
    { "shropshire", "Shrewsbury" },
    { "staffordshire", "Stafford" },
    { "warwickshire", "Warwick" },
    { "leicestershire", "Leicester" },
    { "nottinghamshire", "Nottingham" },
    { "worcestershire", "Worcester" },
    { "herefordshire", "Hereford" },
    { "buckinghamshire", "Aylesbury" },
    { "bedfordshire", "Bedford" },
    { "northamptonshire", "Northampton" },
    { "isle of wight", "Newport" },
    { "fife", "Glenrothes" },
    { "ayrshire", "Ayr" },
    { "lanarkshire", "Hamilton" },
    { "aberdeenshire", "Aberdeen" },
    { "perthshire", "Perth" },
    { "highlands", "Inverness" },
    { "gwynedd", "Caernarfon" },
    { "anglesey", "Llangefni" },
    { "pembrokeshire", "Haverfordwest" },
    { "ceredigion", "Aberystwyth" },
    { "powys", "Llandrindod Wells" },
    { "glamorgan", "Cardiff" },
    { "carmarthenshire", "Carmarthen" },
    { "antrim", "Antrim" },
    { "armagh", "Armagh" },
    { "tyrone", "Omagh" },
    { "fermanagh", "Enniskillen" },
    { "east anglia", "Norwich" },
    { "west country", "Bristol" },
    { "west midlands", "Birmingham" },
    { "east midlands", "Nottingham" },
    { "greater london", "London" },
    { "greater manchester", "Manchester" },
    { "east sussex", "Brighton" },
    { "west sussex", "Chichester" },
    { "north yorkshire", "York" },
    { "south yorkshire", "Sheffield" },
    { "west yorkshire", "Leeds" },
    { "east yorkshire", "Hull" },
};
static const char *const TOWN_STOPWORDS[] = {
    "bath", "best", "capital", "central", "classic", "deal",
    "eden", "energy", "esperanza", "fe", "florence", "gold",
    "good", "harmony", "heart", "hit", "hits", "hope",
    "hyde", "independence", "jazz", "kiss", "liberty", "love",
    "magic", "march", "mix", "mobile", "music", "nice",
    "normal", "nova", "orange", "paradise", "paz", "phoenix",
    "radio", "reading", "rio", "rock", "sale", "salsa",
    "san", "santa", "smooth", "spring", "star", "sun",
    "surprise", "top", "union", "unity", "victoria", "vida",
};
static const char *const REGION_GENERIC[] = {
    "and", "capital", "center", "central", "centre", "city",
    "coast", "county", "district", "east", "eastern", "federal",
    "greater", "island", "islands", "lake", "lower", "metropolitan",
    "national", "new", "north", "northern", "of", "province",
    "region", "river", "sea", "south", "southern", "state",
    "territory", "the", "upper", "valley", "west", "western",
};

static const char *const UK_NATIONS[][2] = {
    { "ENG", "England" }, { "SCT", "Scotland" }, { "WLS", "Wales" }, { "NIR", "Northern Ireland" }
};
static const char *const CROWN_DEPENDENCIES[][2] = {
    { "IM", "Isle of Man" }, { "JE", "Jersey" }, { "GG", "Guernsey" }
};

static const char *geo_note(const char *geo)
{
    if (!strcmp(geo, "swapped"))
        return "(position fixed: latitude and longitude were swapped)";
    if (!strcmp(geo, "flipped"))
        return "(position fixed: a coordinate had the wrong sign)";
    if (!strcmp(geo, "named"))
        return "(position: the town in the station's name; Radio Browser's was elsewhere)";
    if (!strcmp(geo, "ashore"))
        return "(position: the nearest town; Radio Browser's was out at sea)";
    if (!strcmp(geo, "trusted"))
        return "(country corrected to match the position)";
    if (!strcmp(geo, "region"))
        return "(position: Radio Browser's region; its coordinates were outside the country)";
    if (!strcmp(geo, "capital"))
        return "(position: the capital; Radio Browser's coordinates were outside the country)";
    if (!strcmp(geo, "sister"))
        return "(position: another stream of this station; Radio Browser has no coordinates)";
    if (!strcmp(geo, "region_town"))
        return "(position: the town given as its region; Radio Browser has no coordinates)";
    if (!strcmp(geo, "admin1"))
        return "(position: the capital of its region; Radio Browser has no coordinates)";
    if (!strcmp(geo, "name_town"))
        return "(position: the town in the station's name; Radio Browser has no coordinates)";
    if (!strcmp(geo, "name_region"))
        return "(position: the capital of the region in its name or tags; Radio Browser has no "
               "coordinates)";
    if (!strcmp(geo, "country"))
        return "(position: none given; in its country's Country-wide place)";
    return "";
}

/* ---- options ---------------------------------------------------------- */

void rb_options_default(rb_options *o)
{
    memset(o, 0, sizeof(*o));
    o->codecs = RB_MP3;
    o->max = 2000;
    o->city_km = 30.0;
}

int rb_parse_codecs(rb_options *o, const char *list)
{
    char buf[256], *tok, *save = NULL;
    unsigned bits = 0;
    snprintf(buf, sizeof(buf), "%s", list);
    for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        int i, found = 0;
        char *t = tok;
        size_t k;
        while (*t == ' ')
            t++;
        for (k = strlen(t); k > 0 && t[k - 1] == ' '; k--)
            t[k - 1] = '\0';
        for (k = 0; t[k]; k++)
            t[k] = (char)tolower((unsigned char)t[k]);
        if (!*t)
            continue;
        if (!strcmp(t, "all")) {
            bits |= RB_ALL;
            continue;
        }
        for (i = 0; i < N_CODECS; i++)
            if (!strcmp(t, CODEC_OPT[i])) {
                bits |= 1u << i;
                found = 1;
            }
        if (!found)
            return -1;
    }
    o->codecs = bits;
    return 0;
}

int rb_parse_countries(rb_options *o, const char *list)
{
    const char *p = list;
    o->ncountries = 0;
    while (*p) {
        char cc[8];
        size_t k = 0;
        while (*p == ' ' || *p == ',')
            p++;
        while (*p && *p != ',' && k < sizeof(cc) - 1)
            cc[k++] = *p++;
        while (k && cc[k - 1] == ' ')
            k--;
        cc[k] = '\0';
        if (!k)
            continue;
        if (k != 2 || !isalpha((unsigned char)cc[0]) || !isalpha((unsigned char)cc[1]))
            return -1;
        if (o->ncountries < RB_MAX_COUNTRIES) {
            o->countries[o->ncountries][0] = (char)toupper((unsigned char)cc[0]);
            o->countries[o->ncountries][1] = (char)toupper((unsigned char)cc[1]);
            o->countries[o->ncountries][2] = '\0';
            o->ncountries++;
        }
    }
    return 0;
}

/* ---- values from JSON, as Python sees them ------------------------------- */

/* Python's repr of a float (shortest form that reads back the same). */
static void float_repr(double d, char *out, size_t n)
{
    int prec;
    if (isnan(d)) {
        snprintf(out, n, "nan");
        return;
    }
    if (isinf(d)) {
        snprintf(out, n, d < 0 ? "-inf" : "inf");
        return;
    }
    for (prec = 1; prec <= 17; prec++) {
        snprintf(out, n, "%.*g", prec, d);
        if (strtod(out, NULL) == d)
            break;
    }
    if (!strchr(out, '.') && !strchr(out, 'e') && !strchr(out, 'n'))
        strncat(out, ".0", n - strlen(out) - 1);
}

/* str(v) for a field; 0 if the value is None or missing. */
static int py_str(const json_value *v, ustr *out)
{
    char buf[64];
    ustr_clear(out);
    if (!v || v->type == JSON_NULL)
        return 0;
    switch (v->type) {
    case JSON_STRING: ustr_set_utf8(out, v->str, v->len); break;
    case JSON_TRUE: ustr_append_ascii(out, "True"); break;
    case JSON_FALSE: ustr_append_ascii(out, "False"); break;
    case JSON_NUMBER:
        if (v->is_int)
            snprintf(buf, sizeof(buf), "%lld", v->inum);
        else
            float_repr(v->num, buf, sizeof(buf));
        ustr_append_ascii(out, buf);
        break;
    default: break; /* lists and dicts never hold these fields */
    }
    return 1;
}

/* Python's float(str): surrounding whitespace allowed, no hex. */
static int py_float_str(const char *s, double *out)
{
    char buf[128], *end;
    size_t n;
    while (isspace((unsigned char)*s))
        s++;
    n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1]))
        n--;
    if (!n || n >= sizeof(buf) || memchr(s, 'x', n) || memchr(s, 'X', n) || memchr(s, '_', n))
        return 0;
    memcpy(buf, s, n);
    buf[n] = '\0';
    *out = strtod(buf, &end);
    return end != buf && *end == '\0';
}

/* coord(): float(v) within [lo, hi], else 0. */
static int coord(const json_value *v, double lo, double hi, double *out)
{
    double f;
    if (!v)
        return 0;
    if (v->type == JSON_NUMBER)
        f = v->num;
    else if (v->type == JSON_TRUE)
        f = 1.0;
    else if (v->type == JSON_FALSE)
        f = 0.0;
    else if (v->type == JSON_STRING) {
        if (!py_float_str(v->str, &f))
            return 0;
    } else
        return 0;
    if (isnan(f) || f < lo || f > hi)
        return 0;
    *out = f;
    return 1;
}

/* int(v or 0), with 0 for what int() refuses. */
static long py_int(const json_value *v)
{
    if (!v)
        return 0;
    switch (v->type) {
    case JSON_TRUE: return 1;
    case JSON_NUMBER:
        if (v->is_int)
            return (long)v->inum;
        return isfinite(v->num) ? (long)v->num : 0;
    case JSON_STRING: {
        const char *s = v->str;
        char *end;
        long r;
        size_t k = 0;
        while (isspace((unsigned char)*s))
            s++;
        if (*s == '+' || *s == '-')
            k = 1;
        if (!isdigit((unsigned char)s[k]))
            return 0;
        r = strtol(s, &end, 10);
        while (isspace((unsigned char)*end))
            end++;
        return *end ? 0 : r;
    }
    default: return 0;
    }
}

/* x in (1, True, "1") */
static int is_one(const json_value *v)
{
    if (!v)
        return 0;
    if (v->type == JSON_TRUE)
        return 1;
    if (v->type == JSON_NUMBER)
        return v->num == 1.0;
    if (v->type == JSON_STRING)
        return strcmp(v->str, "1") == 0;
    return 0;
}

/* ---- cleaning ------------------------------------------------------------- */

static void clean_u(ustr *s, size_t limit)
{
    size_t i;
    for (i = 0; i < s->n; i++)
        if (s->c[i] == '|' || (u_flags(s->c[i]) & (U_CC | U_CFZ)))
            s->c[i] = ' ';
    ustr_collapse_spaces(s);
    if (s->n > limit) {
        ustr_truncate(s, limit);
        ustr_rstrip(s);
    }
}

/* clean(st.get(key), limit) */
static void clean_field(const json_value *st, const char *key, size_t limit, ustr *out)
{
    if (!py_str(json_get(st, key), out))
        return;
    clean_u(out, limit);
}

static void shorten(ustr *s, size_t limit)
{
    size_t i;
    long sp = -1;
    if (s->n <= limit)
        return;
    ustr_truncate(s, limit - 1);
    for (i = limit / 2; i < s->n; i++)
        if (s->c[i] == ' ')
            sp = 1;
    if (sp > 0) {
        for (i = s->n; i-- > 0;)
            if (s->c[i] == ' ') {
                ustr_truncate(s, i);
                break;
            }
    }
    ustr_rstrip_chars(s, " ,-");
    ustr_push(s, 0x2026);
}

static void clean_name(const json_value *st, ustr *out)
{
    clean_field(st, "name", 300, out);
    ustr_lstrip_char(out, '#');
    ustr_strip(out);
    shorten(out, 100);
}

static int hexval(uint32_t c)
{
    if (c >= '0' && c <= '9')
        return (int)c - '0';
    if (c >= 'a' && c <= 'f')
        return (int)c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return (int)c - 'A' + 10;
    return -1;
}

/* The first value of `key` in a query string, as parse_qs decodes it
 * ('+' is a space, %XX bytes as UTF-8). Empty values don't count. */
static int query_value(const ustr *q, const char *key, ustr *out)
{
    size_t i = 0, klen = strlen(key);
    while (i < q->n) {
        size_t j = i, eq = (size_t)-1;
        while (j < q->n && q->c[j] != '&') {
            if (q->c[j] == '=' && eq == (size_t)-1)
                eq = j;
            j++;
        }
        if (eq != (size_t)-1 && eq - i == klen && eq + 1 < j) {
            size_t k;
            int match = 1;
            for (k = 0; k < klen; k++)
                if (q->c[i + k] != (unsigned char)key[k])
                    match = 0;
            if (match) {
                /* Decode to bytes, then UTF-8. */
                char *bytes = (char *)malloc((j - eq) * 4 + 1);
                size_t b = 0;
                if (!bytes)
                    return 0;
                for (k = eq + 1; k < j; k++) {
                    uint32_t c = q->c[k];
                    if (c == '+') {
                        bytes[b++] = ' ';
                    } else if (c == '%' && k + 2 < j && hexval(q->c[k + 1]) >= 0 &&
                               hexval(q->c[k + 2]) >= 0) {
                        bytes[b++] = (char)(hexval(q->c[k + 1]) * 16 + hexval(q->c[k + 2]));
                        k += 2;
                    } else {
                        ustr one;
                        char *u;
                        ustr_init(&one);
                        ustr_push(&one, c);
                        u = ustr_utf8(&one);
                        if (u) {
                            size_t ul = strlen(u);
                            memcpy(bytes + b, u, ul);
                            b += ul;
                            free(u);
                        }
                        ustr_free(&one);
                    }
                }
                ustr_set_utf8(out, bytes, b);
                free(bytes);
                return 1;
            }
        }
        i = j + 1;
    }
    return 0;
}

static int ends_with_ascii(const ustr *s, const char *suffix)
{
    size_t n = strlen(suffix), i;
    if (s->n < n)
        return 0;
    for (i = 0; i < n; i++)
        if (s->c[s->n - n + i] != (unsigned char)suffix[i])
            return 0;
    return 1;
}

static void unwrap_url(ustr *u)
{
    size_t i, start, host_end, path_end, q_end;
    ustr host, query, inner;
    long colon = -1;
    /* urlsplit: a scheme, then "//" and the netloc. */
    for (i = 0; i < u->n; i++) {
        uint32_t c = u->c[i];
        if (c == ':') {
            colon = (long)i;
            break;
        }
        if (!(c < 0x80 && (isalnum((int)c) || c == '+' || c == '-' || c == '.')))
            break;
    }
    if (colon < 1 || (size_t)colon + 2 >= u->n || u->c[colon + 1] != '/' || u->c[colon + 2] != '/')
        return;
    start = (size_t)colon + 3;
    for (host_end = start; host_end < u->n; host_end++)
        if (u->c[host_end] == '/' || u->c[host_end] == '?' || u->c[host_end] == '#')
            break;
    for (path_end = host_end; path_end < u->n; path_end++)
        if (u->c[path_end] == '?' || u->c[path_end] == '#')
            break;
    for (q_end = path_end; q_end < u->n; q_end++)
        if (u->c[q_end] == '#')
            break;
    ustr_init(&host);
    ustr_init(&query);
    ustr_init(&inner);
    for (i = start; i < host_end; i++)
        ustr_push(&host, u->c[i]);
    ustr_lower(&host);
    if (path_end < u->n && u->c[path_end] == '?')
        for (i = path_end + 1; i < q_end; i++)
            ustr_push(&query, u->c[i]);
    if (ends_with_ascii(&host, "urldefense.proofpoint.com") && host_end + 7 <= u->n) {
        ustr path;
        ustr_init(&path);
        for (i = host_end; i < path_end; i++)
            ustr_push(&path, u->c[i]);
        if (ustr_starts_ascii(&path, "/v2/url") && query_value(&query, "u", &inner)) {
            /* v2: "_" is "/", "-XX" a hex byte. */
            ustr dec;
            size_t seg = 0;
            ustr_init(&dec);
            for (i = 0; i < inner.n; i++)
                if (inner.c[i] == '_')
                    inner.c[i] = '/';
            i = 0;
            while (i <= inner.n) {
                size_t e = i;
                while (e < inner.n && inner.c[e] != '-')
                    e++;
                if (seg == 0) {
                    size_t k;
                    for (k = i; k < e; k++)
                        ustr_push(&dec, inner.c[k]);
                } else if (e - i >= 2 && hexval(inner.c[i]) >= 0 && hexval(inner.c[i + 1]) >= 0) {
                    size_t k;
                    ustr_push(&dec, (uint32_t)(hexval(inner.c[i]) * 16 + hexval(inner.c[i + 1])));
                    for (k = i + 2; k < e; k++)
                        ustr_push(&dec, inner.c[k]);
                } else {
                    size_t k;
                    ustr_push(&dec, '-');
                    for (k = i; k < e; k++)
                        ustr_push(&dec, inner.c[k]);
                }
                seg++;
                i = e + 1;
            }
            ustr_copy(u, &dec);
            ustr_free(&dec);
        }
        ustr_free(&path);
    } else if (ends_with_ascii(&host, "safelinks.protection.outlook.com") &&
               query_value(&query, "url", &inner)) {
        ustr_copy(u, &inner);
    }
    ustr_free(&host);
    ustr_free(&query);
    ustr_free(&inner);
}

/* clean_url(): 0 if the URL can't be written safely. */
static int clean_url(ustr *u)
{
    ustr low;
    size_t i;
    int ok;
    ustr_strip(u);
    unwrap_url(u);
    ustr_init(&low);
    ustr_copy(&low, u);
    ustr_lower(&low);
    ok = ustr_starts_ascii(&low, "http://") || ustr_starts_ascii(&low, "https://");
    ustr_free(&low);
    if (!ok)
        return 0;
    for (i = 0; i < u->n; i++)
        if ((u_flags(u->c[i]) & U_CC) || u->c[i] == '|')
            return 0;
    {
        ustr o;
        ustr_init(&o);
        for (i = 0; i < u->n; i++) {
            if (u->c[i] == ' ')
                ustr_append_ascii(&o, "%20");
            else
                ustr_push(&o, u->c[i]);
        }
        ustr_copy(u, &o);
        ustr_free(&o);
    }
    return u->n <= 500;
}

static int clean_homepage(ustr *u)
{
    ustr_strip(u);
    if (!u->n)
        return 0;
    if (!clean_url(u) || u->n > 200)
        return 0;
    return 1;
}

static void replace_all(ustr *s, const char *from, const char *to)
{
    ustr f, t, o;
    size_t i = 0;
    ustr_init(&f);
    ustr_init(&t);
    ustr_init(&o);
    ustr_append_ascii(&f, from);
    ustr_append_ascii(&t, to);
    while (i < s->n) {
        long at = ustr_find(s, &f, i);
        size_t k;
        if (at < 0) {
            for (k = i; k < s->n; k++)
                ustr_push(&o, s->c[k]);
            break;
        }
        for (k = i; k < (size_t)at; k++)
            ustr_push(&o, s->c[k]);
        ustr_append(&o, &t);
        i = (size_t)at + f.n;
    }
    ustr_copy(s, &o);
    ustr_free(&f);
    ustr_free(&t);
    ustr_free(&o);
}

/* country_name(): a country's name tidied, from obj[key]. */
static void country_name_from(const json_value *obj, const char *key, ustr *s)
{
    ustr low;
    size_t i;
    clean_field(obj, key, 80, s);
    ustr_init(&low);
    ustr_copy(&low, s);
    ustr_lower(&low);
    if (ustr_starts_ascii(&low, "the ")) {
        memmove(s->c, s->c + 4, (s->n - 4) * sizeof(uint32_t));
        s->n -= 4;
        ustr_copy(&low, s);
        ustr_lower(&low);
    }
    for (i = 0; i < sizeof(COUNTRY_NAMES) / sizeof(COUNTRY_NAMES[0]); i++)
        if (ustr_eq_ascii(&low, COUNTRY_NAMES[i][0])) {
            ustr_set_utf8(s, COUNTRY_NAMES[i][1], strlen(COUNTRY_NAMES[i][1]));
            break;
        }
    ustr_free(&low);
    for (i = 0; i < 3; i++) {
        char from[16], to[16];
        size_t k;
        snprintf(from, sizeof(from), " %s ", SMALL_WORDS[i]);
        snprintf(to, sizeof(to), " %s ", SMALL_WORDS[i]);
        for (k = 0; to[k]; k++)
            to[k] = (char)tolower((unsigned char)to[k]);
        replace_all(s, from, to);
    }
}

static void country_name(const json_value *st, ustr *s)
{
    country_name_from(st, "country", s);
}

static void genre_from_tags(const json_value *st, ustr *out)
{
    ustr tags, t, low;
    ustr seen[2];
    int nseen = 0, i;
    size_t a = 0;
    ustr_clear(out);
    ustr_init(&tags);
    ustr_init(&t);
    ustr_init(&low);
    ustr_init(&seen[0]);
    ustr_init(&seen[1]);
    py_str(json_get(st, "tags"), &tags);
    while (a <= tags.n && nseen < 2) {
        size_t e = a;
        while (e < tags.n && tags.c[e] != ',')
            e++;
        ustr_clear(&t);
        for (i = 0; (size_t)i < e - a; i++)
            ustr_push(&t, tags.c[a + (size_t)i]);
        clean_u(&t, 30);
        ustr_lstrip_char(&t, '#');
        ustr_strip(&t);
        if (t.n) {
            int dup = 0;
            ustr_copy(&low, &t);
            ustr_lower(&low);
            for (i = 0; i < nseen; i++) {
                ustr o;
                ustr_init(&o);
                ustr_copy(&o, &seen[i]);
                ustr_lower(&o);
                if (ustr_eq(&o, &low))
                    dup = 1;
                ustr_free(&o);
            }
            if (!dup) {
                ustr_copy(&seen[nseen], &t);
                if (nseen)
                    ustr_append_ascii(out, ", ");
                ustr_append(out, &t);
                nseen++;
            }
        }
        a = e + 1;
    }
    ustr_free(&tags);
    ustr_free(&t);
    ustr_free(&low);
    ustr_free(&seen[0]);
    ustr_free(&seen[1]);
}

/* "".join(ch if ch.isalnum() else " " for ch in s) */
static void alnum_words(ustr *s)
{
    size_t i;
    for (i = 0; i < s->n; i++)
        if (!u_isalnum(s->c[i]))
            s->c[i] = ' ';
}

/* The script's rfold(): fold(), with hyphens, apostrophes and full stops
 * as spaces. */
static void rfold(ustr *s)
{
    size_t i;
    ustr_fold(s);
    for (i = 0; i < s->n; i++)
        if (s->c[i] == '-' || s->c[i] == '\'' || s->c[i] == '.' || s->c[i] == 0x2019)
            s->c[i] = ' ';
    ustr_collapse_spaces(s);
}

/* rfold() of UTF-8 text, as malloc'd UTF-8. */
static char *rfold_utf8(const char *t, size_t n)
{
    ustr u;
    char *r;
    ustr_init(&u);
    ustr_set_utf8(&u, t, n);
    rfold(&u);
    r = ustr_utf8(&u);
    ustr_free(&u);
    return r;
}

static int is_test(const ustr *name)
{
    ustr w;
    size_t i = 0;
    int hit = 0;
    ustr_init(&w);
    ustr_copy(&w, name);
    ustr_lower(&w);
    alnum_words(&w);
    while (i < w.n && !hit) {
        size_t e;
        while (i < w.n && u_isspace(w.c[i]))
            i++;
        e = i;
        while (e < w.n && !u_isspace(w.c[e]))
            e++;
        if (e - i == 4 && w.c[i] == 't' && w.c[i + 1] == 'e' && w.c[i + 2] == 's' &&
            w.c[i + 3] == 't')
            hit = 1;
        i = e;
    }
    ustr_free(&w);
    return hit;
}

static double km(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = lat1 * DEG2RAD, p2 = lat2 * DEG2RAD;
    double dp = p2 - p1, dl = (lon2 - lon1) * DEG2RAD;
    double a = pow(sin(dp / 2), 2.0) + cos(p1) * cos(p2) * pow(sin(dl / 2), 2.0);
    double r = sqrt(a);
    return 6371.0 * 2 * asin(r < 1.0 ? r : 1.0);
}

/* ---- borders -------------------------------------------------------------- */

typedef struct {
    char cc[3];
    double x0, y0, x1, y1;
    int nrings;
    int *ring_len;
    double **rings; /* x, y pairs */
} shape;

/* ---- cities --------------------------------------------------------------- */

typedef struct {
    double lat, lon;
    char *name;  /* UTF-8, cleaned */
    char cc[8];
    long pop;
    char admin1[24];
    char is_seat; /* PPLA or PPLC: a region's capital */
} city;

#define GRID_ROWS 181 /* floor(lat) -90..90 */
#define GRID_COLS 361 /* floor(lon) -180..180 */

typedef struct {
    int *idx;
    int n, cap;
} cell;

struct rb_geo {
    city *cities;
    long ncities, cap;
    cell *grid; /* GRID_ROWS * GRID_COLS */
    strmap countries;       /* cc -> 1 */
    strmap capitals;        /* cc -> city */
    strmap by_name;         /* cc "\x1F" folded name -> city */
    strmap big_by_name;     /* folded name -> city */
    strmap country_names;   /* cc -> index in cn_names */
    char **cn_names;
    int n_cn;
    shape *shapes;
    long nshapes;
    strmap sovereign;       /* cc -> index in sov_names */
    char **sov_names;
    int n_sov;
    strmap border_names;    /* cc -> index in bn_names: the borders file's ADMIN */
    char **bn_names;
    int n_bn;
    int has_borders;
    /* For stations without coordinates (keys cc "\x1F" rfold()ed name): */
    strmap towns;           /* -> city, by any name (other names for big towns) */
    strmap seats;           /* cc "\x1F" admin1 code -> the region's capital */
    strmap regions;         /* -> index in reg_codes */
    char **reg_codes;
    int n_reg;
    strmap labels;          /* cc -> index in lab: Natural Earth's label point */
    double (*lab)[2];
    int n_lab;
};

rb_geo *rb_geo_new(void)
{
    rb_geo *g = (rb_geo *)calloc(1, sizeof(rb_geo));
    if (!g)
        return NULL;
    g->grid = (cell *)calloc((size_t)GRID_ROWS * GRID_COLS, sizeof(cell));
    if (!g->grid) {
        free(g);
        return NULL;
    }
    strmap_init(&g->countries);
    strmap_init(&g->capitals);
    strmap_init(&g->by_name);
    strmap_init(&g->big_by_name);
    strmap_init(&g->country_names);
    strmap_init(&g->sovereign);
    strmap_init(&g->border_names);
    strmap_init(&g->towns);
    strmap_init(&g->seats);
    strmap_init(&g->regions);
    strmap_init(&g->labels);
    return g;
}

static void clear_country_names(rb_geo *g)
{
    int i;
    for (i = 0; i < g->n_cn; i++)
        free(g->cn_names[i]);
    free(g->cn_names);
    g->cn_names = NULL;
    g->n_cn = 0;
    strmap_free(&g->country_names);
    strmap_init(&g->country_names);
}

void rb_geo_free(rb_geo *g)
{
    long i;
    int k;
    if (!g)
        return;
    for (i = 0; i < g->ncities; i++)
        free(g->cities[i].name);
    free(g->cities);
    for (i = 0; i < (long)GRID_ROWS * GRID_COLS; i++)
        free(g->grid[i].idx);
    free(g->grid);
    for (i = 0; i < g->nshapes; i++) {
        for (k = 0; k < g->shapes[i].nrings; k++)
            free(g->shapes[i].rings[k]);
        free(g->shapes[i].rings);
        free(g->shapes[i].ring_len);
    }
    free(g->shapes);
    for (k = 0; k < g->n_sov; k++)
        free(g->sov_names[k]);
    free(g->sov_names);
    for (k = 0; k < g->n_bn; k++)
        free(g->bn_names[k]);
    free(g->bn_names);
    strmap_free(&g->border_names);
    for (k = 0; k < g->n_reg; k++)
        free(g->reg_codes[k]);
    free(g->reg_codes);
    free(g->lab);
    strmap_free(&g->towns);
    strmap_free(&g->seats);
    strmap_free(&g->regions);
    strmap_free(&g->labels);
    clear_country_names(g);
    strmap_free(&g->countries);
    strmap_free(&g->capitals);
    strmap_free(&g->by_name);
    strmap_free(&g->big_by_name);
    strmap_free(&g->country_names);
    strmap_free(&g->sovereign);
    free(g);
}

static cell *grid_cell(rb_geo *g, long lat_i, long lon_i)
{
    if (lat_i < -90 || lat_i > 90 || lon_i < -180 || lon_i > 180)
        return NULL;
    return &g->grid[(lat_i + 90) * GRID_COLS + (lon_i + 180)];
}

static void cell_add(cell *c, int idx)
{
    if (c->n == c->cap) {
        int nc = c->cap ? c->cap * 2 : 4;
        int *ni = (int *)realloc(c->idx, (size_t)nc * sizeof(int));
        if (!ni)
            abort();
        c->idx = ni;
        c->cap = nc;
    }
    c->idx[c->n++] = idx;
}

static void name_key(char *out, size_t n, const char *cc, const ustr *folded)
{
    char *u = ustr_utf8(folded);
    snprintf(out, n, "%s\x1F%s", cc, u ? u : "");
    free(u);
}

/* Splits one line at tabs into at most max fields (pointers into line,
 * which is modified). */
static int split_tabs(char *line, char **f, int max)
{
    int n = 0;
    f[n++] = line;
    while (*line && n < max) {
        if (*line == '\t') {
            *line = '\0';
            f[n++] = line + 1;
        }
        line++;
    }
    return n;
}

long rb_geo_load_cities(rb_geo *g, const char *text, size_t n)
{
    size_t pos = 0;
    ustr name, alt, shortn, k;
    char key[600];
    ustr_init(&name);
    ustr_init(&alt);
    ustr_init(&shortn);
    ustr_init(&k);
    while (pos < n) {
        size_t e = pos, len;
        char *line, *f[20];
        int nf, j;
        double lat, lon;
        long pop, idx;
        city *c;
        while (e < n && text[e] != '\n' && text[e] != '\r')
            e++;
        len = e - pos;
        line = (char *)malloc(len + 1);
        if (!line)
            return -1;
        memcpy(line, text + pos, len);
        line[len] = '\0';
        pos = e + (e < n && text[e] == '\r' && e + 1 < n && text[e + 1] == '\n' ? 2 : 1);
        nf = split_tabs(line, f, 20);
        if (nf < 15 || !py_float_str(f[4], &lat) || !py_float_str(f[5], &lon)) {
            free(line);
            continue;
        }
        ustr_set_utf8(&name, f[1], strlen(f[1]));
        clean_u(&name, 60);
        {
            int skip = 0;
            for (j = 0; j < 5; j++)
                if (!strcmp(f[7], SKIP_FEATURES[j]))
                    skip = 1;
            if (!name.n || skip) {
                free(line);
                continue;
            }
        }
        /* "Halle (Saale)" is "Halle" in a station's name. */
        {
            long at = ustr_find_ascii(&name, " (");
            ustr_copy(&shortn, &name);
            if (at >= 0)
                ustr_truncate(&shortn, (size_t)at);
            ustr_strip(&shortn);
        }
        {
            json_value pv;
            memset(&pv, 0, sizeof(pv));
            pv.type = JSON_STRING;
            pv.str = f[14];
            pop = f[14][0] ? py_int(&pv) : 0;
        }
        if (g->ncities == g->cap) {
            long nc = g->cap ? g->cap * 2 : 1024;
            city *ncities = (city *)realloc(g->cities, (size_t)nc * sizeof(city));
            if (!ncities) {
                free(line);
                return -1;
            }
            g->cities = ncities;
            g->cap = nc;
        }
        idx = g->ncities++;
        c = &g->cities[idx];
        c->lat = lat;
        c->lon = lon;
        c->name = ustr_utf8(&name);
        snprintf(c->cc, sizeof(c->cc), "%s", f[8]);
        c->pop = pop;
        snprintf(c->admin1, sizeof(c->admin1), "%s", f[10]);
        {
            cell *cl = grid_cell(g, (long)floor(lat), (long)floor(lon));
            if (cl)
                cell_add(cl, (int)idx);
        }
        strmap_put(&g->countries, c->cc, 1);
        if (!strcmp(f[7], "PPLC")) {
            int cur;
            if (!strmap_get(&g->capitals, c->cc, &cur) || pop >= g->cities[cur].pop)
                strmap_put(&g->capitals, c->cc, (int)idx);
        }
        ustr_set_utf8(&alt, f[2], strlen(f[2]));
        clean_u(&alt, 60);
        /* Towns by any name, for stations without coordinates. */
        for (j = 0; j < 3; j++) {
            const ustr *nm = j == 0 ? &name : j == 1 ? &alt : &shortn;
            int cur;
            ustr_copy(&k, nm);
            rfold(&k);
            if (k.n < 3)
                continue;
            name_key(key, sizeof(key), c->cc, &k);
            if (!strmap_get(&g->towns, key, &cur) || pop > g->cities[cur].pop)
                strmap_put(&g->towns, key, (int)idx);
        }
        if (pop >= ALT_NAME_POP) {
            const char *a = f[3];
            while (1) {
                const char *e2 = strchr(a, ',');
                size_t al = e2 ? (size_t)(e2 - a) : strlen(a);
                int cur;
                ustr_set_utf8(&k, a, al);
                clean_u(&k, 60);
                rfold(&k);
                if (k.n >= 3) {
                    name_key(key, sizeof(key), c->cc, &k);
                    if (!strmap_get(&g->towns, key, &cur) || pop > g->cities[cur].pop)
                        strmap_put(&g->towns, key, (int)idx);
                }
                if (!e2)
                    break;
                a = e2 + 1;
            }
        }
        c->is_seat = !strcmp(f[7], "PPLA") || !strcmp(f[7], "PPLC");
        if (f[10][0]) {
            char sk[64];
            int cur;
            snprintf(sk, sizeof(sk), "%s\x1F%s", c->cc, f[10]);
            if (!strmap_get(&g->seats, sk, &cur) || c->is_seat > g->cities[cur].is_seat ||
                (c->is_seat == g->cities[cur].is_seat && pop > g->cities[cur].pop))
                strmap_put(&g->seats, sk, (int)idx);
        }
        for (j = 0; j < 3; j++) {
            const ustr *nm = j == 0 ? &name : j == 1 ? &alt : &shortn;
            int cur;
            if (!nm->n)
                continue;
            ustr_copy(&k, nm);
            ustr_fold(&k);
            name_key(key, sizeof(key), c->cc, &k);
            if (!strmap_get(&g->by_name, key, &cur) || pop > g->cities[cur].pop)
                strmap_put(&g->by_name, key, (int)idx);
            if (pop >= NAMED_TOWN_POP) {
                char *fk = ustr_utf8(&k);
                if (fk && (!strmap_get(&g->big_by_name, fk, &cur) || pop > g->cities[cur].pop))
                    strmap_put(&g->big_by_name, fk, (int)idx);
                free(fk);
            }
        }
        free(line);
    }
    ustr_free(&name);
    ustr_free(&alt);
    ustr_free(&shortn);
    ustr_free(&k);
    return g->ncities;
}

static int add_ring(shape *s, const json_value *ring)
{
    size_t i;
    double *pts;
    int *nl;
    double **nr;
    if (!ring || ring->type != JSON_ARRAY)
        return 0;
    pts = (double *)malloc((ring->n ? ring->n : 1) * 2 * sizeof(double));
    nl = (int *)realloc(s->ring_len, (size_t)(s->nrings + 1) * sizeof(int));
    if (nl)
        s->ring_len = nl;
    nr = (double **)realloc(s->rings, (size_t)(s->nrings + 1) * sizeof(double *));
    if (nr)
        s->rings = nr;
    if (!pts || !nl || !nr) {
        free(pts);
        return -1;
    }
    for (i = 0; i < ring->n; i++) {
        const json_value *p = &ring->items[i];
        pts[2 * i] = p->type == JSON_ARRAY && p->n >= 2 ? p->items[0].num : 0;
        pts[2 * i + 1] = p->type == JSON_ARRAY && p->n >= 2 ? p->items[1].num : 0;
    }
    s->rings[s->nrings] = pts;
    s->ring_len[s->nrings] = (int)ring->n;
    s->nrings++;
    return 0;
}

long rb_geo_load_borders(rb_geo *g, const char *text, size_t n, char *err, size_t errn)
{
    json_value *doc = json_parse(text, n, err, errn);
    const json_value *feats;
    size_t i;
    if (!doc)
        return -1;
    feats = json_get(doc, "features");
    for (i = 0; feats && feats->type == JSON_ARRAY && i < feats->n; i++) {
        const json_value *feat = &feats->items[i];
        const json_value *props = json_get(feat, "properties");
        const json_value *geom = json_get(feat, "geometry");
        const char *cc = json_str(json_get(props, "ISO_A2_EH"));
        const char *sov = json_str(json_get(props, "SOV_A3"));
        const char *type = json_str(json_get(geom, "type"));
        const json_value *coords = json_get(geom, "coordinates");
        char ccu[3];
        size_t p, np;
        if (!cc || !*cc)
            cc = json_str(json_get(props, "ISO_A2"));
        if (!cc || strlen(cc) != 2 || !isalpha((unsigned char)cc[0]) ||
            !isalpha((unsigned char)cc[1]))
            continue;
        ccu[0] = (char)toupper((unsigned char)cc[0]);
        ccu[1] = (char)toupper((unsigned char)cc[1]);
        ccu[2] = '\0';
        if (sov && *sov) {
            char **ns = (char **)realloc(g->sov_names, (size_t)(g->n_sov + 1) * sizeof(char *));
            if (ns) {
                g->sov_names = ns;
                g->sov_names[g->n_sov] = strdup(sov);
                strmap_put(&g->sovereign, ccu, g->n_sov++);
            }
        }
        {
            ustr nm;
            int have;
            ustr_init(&nm);
            country_name_from(props, "ADMIN", &nm);
            {
                double lx, ly;
                int hl;
                if (coord(json_get(props, "LABEL_X"), -180, 180, &lx) &&
                    coord(json_get(props, "LABEL_Y"), -90, 90, &ly) &&
                    !strmap_get(&g->labels, ccu, &hl)) {
                    double(*nl)[2] =
                        (double(*)[2])realloc(g->lab, (size_t)(g->n_lab + 1) * sizeof(*nl));
                    if (nl) {
                        g->lab = nl;
                        g->lab[g->n_lab][0] = ly;
                        g->lab[g->n_lab][1] = lx;
                        strmap_put(&g->labels, ccu, g->n_lab++);
                    }
                }
            }
            if (nm.n && !strmap_get(&g->border_names, ccu, &have)) {
                char **nb =
                    (char **)realloc(g->bn_names, (size_t)(g->n_bn + 1) * sizeof(char *));
                if (nb) {
                    g->bn_names = nb;
                    g->bn_names[g->n_bn] = ustr_utf8(&nm);
                    strmap_put(&g->border_names, ccu, g->n_bn++);
                }
            }
            ustr_free(&nm);
        }
        if (!type || !coords || coords->type != JSON_ARRAY)
            continue;
        if (!strcmp(type, "Polygon"))
            np = 1;
        else if (!strcmp(type, "MultiPolygon"))
            np = coords->n;
        else
            continue;
        for (p = 0; p < np; p++) {
            const json_value *poly = !strcmp(type, "Polygon") ? coords : &coords->items[p];
            const json_value *outer;
            shape *s;
            size_t r, k;
            if (poly->type != JSON_ARRAY || poly->n == 0)
                continue;
            outer = &poly->items[0];
            if (outer->type != JSON_ARRAY || outer->n == 0)
                continue;
            {
                shape *ns = (shape *)realloc(g->shapes, (size_t)(g->nshapes + 1) * sizeof(shape));
                if (!ns) {
                    json_free(doc);
                    snprintf(err, errn, "out of memory");
                    return -1;
                }
                g->shapes = ns;
            }
            s = &g->shapes[g->nshapes++];
            memset(s, 0, sizeof(*s));
            memcpy(s->cc, ccu, 3);
            for (r = 0; r < poly->n; r++)
                add_ring(s, &poly->items[r]);
            s->x0 = s->x1 = s->rings[0][0];
            s->y0 = s->y1 = s->rings[0][1];
            for (k = 0; k < (size_t)s->ring_len[0]; k++) {
                double x = s->rings[0][2 * k], y = s->rings[0][2 * k + 1];
                if (x < s->x0) s->x0 = x;
                if (x > s->x1) s->x1 = x;
                if (y < s->y0) s->y0 = y;
                if (y > s->y1) s->y1 = y;
            }
        }
    }
    json_free(doc);
    g->has_borders = 1;
    return g->nshapes;
}

static int in_ring(const double *ring, int n, double x, double y)
{
    int inside = 0, i, j = n - 1;
    for (i = 0; i < n; i++) {
        double xi = ring[2 * i], yi = ring[2 * i + 1];
        double xj = ring[2 * j], yj = ring[2 * j + 1];
        if (((yi > y) != (yj > y)) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
            inside = !inside;
        j = i;
    }
    return inside;
}

/* Borders.at(): the country code, or NULL (the sea). */
static const char *border_at(const rb_geo *g, double lat, double lon)
{
    long i;
    for (i = 0; i < g->nshapes; i++) {
        const shape *s = &g->shapes[i];
        int k, hole = 0;
        if (!(s->x0 <= lon && lon <= s->x1 && s->y0 <= lat && lat <= s->y1))
            continue;
        if (!in_ring(s->rings[0], s->ring_len[0], lon, lat))
            continue;
        for (k = 1; k < s->nrings && !hole; k++)
            if (in_ring(s->rings[k], s->ring_len[k], lon, lat))
                hole = 1;
        if (!hole)
            return s->cc;
    }
    return NULL;
}

static int same_state(const rb_geo *g, const char *a, const char *b)
{
    int ia, ib;
    if (!strcmp(a, b))
        return 1;
    if (!strmap_get(&g->sovereign, a, &ia))
        return 0;
    if (!strmap_get(&g->sovereign, b, &ib))
        return 0;
    return strcmp(g->sov_names[ia], g->sov_names[ib]) == 0;
}

/* ---- nearby towns ------------------------------------------------------- */

typedef struct {
    double d;
    int c;
} found;

typedef struct {
    found *f;
    size_t n, cap;
} found_list;

static void found_add(found_list *l, double d, int c)
{
    if (l->n == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 64;
        found *nf = (found *)realloc(l->f, nc * sizeof(found));
        if (!nf)
            abort();
        l->f = nf;
        l->cap = nc;
    }
    l->f[l->n].d = d;
    l->f[l->n].c = c;
    l->n++;
}

/* Cities._within(): every town within max_km, in the script's order. */
static void within(rb_geo *g, double lat, double lon, double max_km, found_list *out)
{
    long span = (long)(max_km / 111.0) + 1, lon_span, dy, dx;
    double a = fabs(lat) + (double)span, coslat, m;
    long flat = (long)floor(lat), flon = (long)floor(lon);
    out->n = 0;
    coslat = cos((a < 89.0 ? a : 89.0) * DEG2RAD);
    m = coslat > 0.01 ? coslat : 0.01;
    lon_span = (long)((double)span / m) + 1;
    if (lon_span > 180)
        lon_span = 180;
    for (dy = -span; dy <= span; dy++)
        for (dx = -lon_span; dx <= lon_span; dx++) {
            long cx = ((flon + dx + 180) % 360 + 360) % 360 - 180;
            cell *cl = grid_cell(g, flat + dy, cx);
            int i;
            if (!cl)
                continue;
            for (i = 0; i < cl->n; i++) {
                const city *c = &g->cities[cl->idx[i]];
                double d = km(lat, lon, c->lat, c->lon);
                if (d <= max_km)
                    found_add(out, d, cl->idx[i]);
            }
        }
}

static int near_town(rb_geo *g, double lat, double lon, double max_km, const char *cc)
{
    found_list l = { NULL, 0, 0 };
    size_t i;
    int hit = 0;
    within(g, lat, lon, max_km, &l);
    for (i = 0; i < l.n && !hit; i++)
        if (!cc || !strcmp(g->cities[l.f[i].c].cc, cc))
            hit = 1;
    free(l.f);
    return hit;
}

/* The first nearest of a list (optionally only towns in cc), or -1. */
static int nearest_of(const rb_geo *g, const found_list *l, const char *cc)
{
    size_t i;
    int best = -1;
    double bd = 0;
    for (i = 0; i < l->n; i++) {
        if (cc && strcmp(g->cities[l->f[i].c].cc, cc) != 0)
            continue;
        if (best < 0 || l->f[i].d < bd) {
            best = l->f[i].c;
            bd = l->f[i].d;
        }
    }
    return best;
}

/* Cities.territory(): the label where the territory rule applies. */
static int territory(rb_geo *g, double lat, double lon, char *cc_out, const char **label)
{
    found_list l = { NULL, 0, 0 };
    int town;
    const char *cc;
    size_t i;
    within(g, lat, lon, TERRITORY_TOWN_KM, &l);
    town = nearest_of(g, &l, NULL);
    if (town >= 0) {
        cc = g->cities[town].cc;
    } else {
        cc = g->has_borders ? border_at(g, lat, lon) : NULL;
        if ((!cc || !strcmp(cc, "RU")) && CRIMEA_BOX[0] <= lat && lat <= CRIMEA_BOX[2] &&
            CRIMEA_BOX[1] <= lon && lon <= CRIMEA_BOX[3])
            cc = "UA";
    }
    if (cc && !strcmp(cc, "UA")) {
        strcpy(cc_out, "UA");
        *label = "Ukraine";
        free(l.f);
        return 1;
    }
    if (cc && !strcmp(cc, "PS")) {
        strcpy(cc_out, "PS");
        *label = lon < GAZA_MAX_LON ? "Gaza" : "Occupied West Bank";
        free(l.f);
        return 1;
    }
    for (i = 0; cc && i < 3; i++)
        if (!strcmp(cc, CROWN_DEPENDENCIES[i][0])) {
            strcpy(cc_out, cc);
            *label = CROWN_DEPENDENCIES[i][1];
            free(l.f);
            return 1;
        }
    if (!cc || !strcmp(cc, "GB")) {
        if (town < 0) {
            within(g, lat, lon, UK_TOWN_KM, &l);
            town = nearest_of(g, &l, "GB");
        }
        if (town >= 0)
            for (i = 0; i < 4; i++)
                if (!strcmp(g->cities[town].admin1, UK_NATIONS[i][0])) {
                    strcpy(cc_out, "GB");
                    *label = UK_NATIONS[i][1];
                    free(l.f);
                    return 1;
                }
    }
    free(l.f);
    return 0;
}

/* Cities.named_town(): a big town named in the station's name, or -1;
 * *cc_out gets its country. */
static int named_town(rb_geo *g, const ustr *name, const char *cc, const char **cc_out)
{
    ustr w, key;
    size_t *ws, *we, nw = 0, i;
    int nlen, best = -1;
    ustr_init(&w);
    ustr_init(&key);
    ustr_copy(&w, name);
    alnum_words(&w);
    ustr_fold(&w);
    ws = (size_t *)malloc((w.n + 1) * sizeof(size_t));
    we = (size_t *)malloc((w.n + 1) * sizeof(size_t));
    if (!ws || !we)
        abort();
    for (i = 0; i < w.n;) {
        while (i < w.n && w.c[i] == ' ')
            i++;
        if (i >= w.n)
            break;
        ws[nw] = i;
        while (i < w.n && w.c[i] != ' ')
            i++;
        we[nw++] = i;
    }
    for (nlen = 3; nlen >= 1; nlen--) {
        size_t s;
        for (s = 0; s + (size_t)nlen <= nw; s++) {
            char kbuf[600], *fk;
            int local, big;
            size_t k;
            ustr_clear(&key);
            for (k = ws[s]; k < we[s + (size_t)nlen - 1]; k++)
                ustr_push(&key, w.c[k]);
            if (key.n < 4)
                continue;
            name_key(kbuf, sizeof(kbuf), cc, &key);
            if (strmap_get(&g->by_name, kbuf, &local) && g->cities[local].pop >= NAMED_TOWN_POP) {
                *cc_out = cc;
                free(ws);
                free(we);
                ustr_free(&w);
                ustr_free(&key);
                return local;
            }
            fk = ustr_utf8(&key);
            if (fk && strmap_get(&g->big_by_name, fk, &big) &&
                g->cities[big].pop >= NAMED_ABROAD_POP &&
                (best < 0 || g->cities[big].pop > g->cities[best].pop))
                best = big;
            free(fk);
        }
    }
    free(ws);
    free(we);
    ustr_free(&w);
    ustr_free(&key);
    if (best >= 0)
        *cc_out = g->cities[best].cc;
    return best;
}

typedef struct {
    double lat, lon;
    const char *how;
    char cc[8];
} position;

/* Cities.check_position(); 0 to drop the station. */
static int check_position(rb_geo *g, double lat, double lon, const char *cc, const ustr *region,
                          const ustr *name, position *out)
{
    const char *at = g->has_borders ? border_at(g, lat, lon) : NULL;
    int b = g->has_borders, dummy, i;
    static const char *const HOW[6] = { "swapped", "flipped", "flipped", "flipped", "swapped",
                                        "swapped" };
    double cand[6][2];
    found_list l = { NULL, 0, 0 };

    out->lat = lat;
    out->lon = lon;
    out->how = "ok";
    snprintf(out->cc, sizeof(out->cc), "%s", cc);
    if (!strmap_get(&g->countries, cc, &dummy))
        return at || near_town(g, lat, lon, b ? NEAR_OWN_KM : GEO_CHECK_KM, NULL);
    if (b) {
        if ((at && same_state(g, at, cc)) || near_town(g, lat, lon, NEAR_OWN_KM, cc))
            return 1;
        if (!at) {
            int town;
            within(g, lat, lon, GEO_CHECK_KM, &l);
            town = nearest_of(g, &l, cc);
            free(l.f);
            l.f = NULL;
            l.n = l.cap = 0;
            if (town >= 0) {
                out->lat = g->cities[town].lat;
                out->lon = g->cities[town].lon;
                out->how = "ashore";
                return 1;
            }
        }
    } else if (near_town(g, lat, lon, GEO_CHECK_KM, cc)) {
        return 1;
    }
    cand[0][0] = lon;  cand[0][1] = lat;
    cand[1][0] = lat;  cand[1][1] = -lon;
    cand[2][0] = -lat; cand[2][1] = lon;
    cand[3][0] = -lat; cand[3][1] = -lon;
    cand[4][0] = lon;  cand[4][1] = -lat;
    cand[5][0] = -lon; cand[5][1] = lat;
    for (i = 0; i < 6; i++) {
        double la = cand[i][0], lo = cand[i][1];
        if (-90 <= la && la <= 90 && -180 <= lo && lo <= 180 &&
            near_town(g, la, lo, GEO_FIX_KM, cc)) {
            out->lat = la;
            out->lon = lo;
            out->how = HOW[i];
            return 1;
        }
    }
    if (b && at && near_town(g, lat, lon, BORDER_AREA_KM, cc))
        return 1;
    if (name->n) {
        const char *tcc = NULL;
        int town = named_town(g, name, cc, &tcc);
        if (town >= 0) {
            out->lat = g->cities[town].lat;
            out->lon = g->cities[town].lon;
            out->how = "named";
            snprintf(out->cc, sizeof(out->cc), "%s", tcc);
            return 1;
        }
    }
    {
        int ci, stated_in = 0;
        ustr stated, fname;
        ustr_init(&stated);
        ustr_init(&fname);
        if (strmap_get(&g->country_names, cc, &ci)) {
            ustr_set_utf8(&stated, g->cn_names[ci], strlen(g->cn_names[ci]));
            ustr_fold(&stated);
        }
        if (stated.n) {
            ustr_copy(&fname, name);
            ustr_fold(&fname);
            stated_in = ustr_find(&fname, &stated, 0) >= 0;
        }
        ustr_free(&stated);
        ustr_free(&fname);
        if (!stated_in) {
            size_t k;
            int big = -1;
            within(g, lat, lon, TRUST_CITY_KM, &l);
            for (k = 0; k < l.n; k++) {
                const city *c = &g->cities[l.f[k].c];
                if (c->pop >= TRUST_CITY_POP && (big < 0 || c->pop > g->cities[big].pop))
                    big = l.f[k].c;
            }
            free(l.f);
            l.f = NULL;
            l.n = l.cap = 0;
            if (big >= 0) {
                out->how = "trusted";
                snprintf(out->cc, sizeof(out->cc), "%s", at ? at : g->cities[big].cc);
                return 1;
            }
        }
    }
    if (region->n) {
        ustr fr;
        char kbuf[600];
        int hit;
        ustr_init(&fr);
        ustr_copy(&fr, region);
        ustr_fold(&fr);
        name_key(kbuf, sizeof(kbuf), cc, &fr);
        ustr_free(&fr);
        if (strmap_get(&g->by_name, kbuf, &hit)) {
            out->lat = g->cities[hit].lat;
            out->lon = g->cities[hit].lon;
            out->how = "region";
            return 1;
        }
    }
    {
        int cap;
        if (strmap_get(&g->capitals, cc, &cap)) {
            out->lat = g->cities[cap].lat;
            out->lon = g->cities[cap].lon;
            out->how = "capital";
            return 1;
        }
    }
    return 0;
}

/* Cities.nearest(): the town for a station, or -1. */
static int nearest_city(rb_geo *g, double lat, double lon, double max_km, const char *cc)
{
    found_list l = { NULL, 0, 0 };
    size_t i;
    int same = 0, best = -1;
    double bd = 0;
    within(g, lat, lon, max_km, &l);
    if (cc && *cc)
        for (i = 0; i < l.n; i++)
            if (!strcmp(g->cities[l.f[i].c].cc, cc))
                same = 1;
    /* Within BIG_CITY_KM, the most populous (then the nearest) wins. */
    for (i = 0; i < l.n; i++) {
        const city *c = &g->cities[l.f[i].c];
        if (same && strcmp(c->cc, cc) != 0)
            continue;
        if (l.f[i].d <= BIG_CITY_KM &&
            (best < 0 || c->pop > g->cities[best].pop ||
             (c->pop == g->cities[best].pop && -l.f[i].d > -bd))) {
            best = l.f[i].c;
            bd = l.f[i].d;
        }
    }
    if (best < 0) {
        for (i = 0; i < l.n; i++) {
            const city *c = &g->cities[l.f[i].c];
            if (same && strcmp(c->cc, cc) != 0)
                continue;
            if (best < 0 || l.f[i].d < bd) {
                best = l.f[i].c;
                bd = l.f[i].d;
            }
        }
    }
    free(l.f);
    return best;
}

/* country_names(): each code's most used name, first seen winning ties. */
static void build_country_names(rb_geo *g, const json_value *const *st, size_t n)
{
    strmap pair_count; /* cc "\x1F" name -> count */
    char **order = NULL; /* pair keys in first-seen order */
    size_t norder = 0, i;
    ustr cc, name;
    clear_country_names(g);
    strmap_init(&pair_count);
    ustr_init(&cc);
    ustr_init(&name);
    for (i = 0; i < n; i++) {
        char key[512], *ccs, *nms;
        int cnt;
        clean_field(st[i], "countrycode", 2, &cc);
        ustr_upper_ascii(&cc);
        country_name(st[i], &name);
        if (!cc.n || !name.n)
            continue;
        ccs = ustr_utf8(&cc);
        nms = ustr_utf8(&name);
        snprintf(key, sizeof(key), "%s\x1F%s", ccs, nms);
        if (strmap_get(&pair_count, key, &cnt)) {
            strmap_put(&pair_count, key, cnt + 1);
        } else {
            char **no = (char **)realloc(order, (norder + 1) * sizeof(char *));
            if (no) {
                order = no;
                order[norder++] = strdup(key);
                strmap_put(&pair_count, key, 1);
            }
        }
        free(ccs);
        free(nms);
    }
    for (i = 0; i < norder; i++) {
        char *sep = strchr(order[i], '\x1F');
        int cnt, cur;
        *sep = '\0';
        *sep = '\x1F';
        strmap_get(&pair_count, order[i], &cnt);
        *sep = '\0';
        if (!strmap_get(&g->country_names, order[i], &cur)) {
            char **nn = (char **)realloc(g->cn_names, (size_t)(g->n_cn + 1) * sizeof(char *));
            if (nn) {
                g->cn_names = nn;
                g->cn_names[g->n_cn] = strdup(sep + 1);
                strmap_put(&g->country_names, order[i], g->n_cn);
                /* Remember the count alongside: the best so far. */
                g->n_cn++;
            }
        } else {
            char best_key[512];
            int best_cnt;
            snprintf(best_key, sizeof(best_key), "%s\x1F%s", order[i], g->cn_names[cur]);
            strmap_get(&pair_count, best_key, &best_cnt);
            if (cnt > best_cnt) {
                free(g->cn_names[cur]);
                g->cn_names[cur] = strdup(sep + 1);
            }
        }
        *sep = '\x1F';
        free(order[i]);
    }
    free(order);
    strmap_free(&pair_count);
    ustr_free(&cc);
    ustr_free(&name);
}

/* ---- convert ------------------------------------------------------------ */

/* ---- stations without coordinates ------------------------------------------ */

static void ckey(char *out, size_t n, const char *cc, const char *s)
{
    snprintf(out, n, "%s\x1F%s", cc, s);
}

static int town_get(const rb_geo *g, const char *cc, const char *folded)
{
    char key[1200];
    int v;
    ckey(key, sizeof(key), cc, folded);
    return strmap_get(&g->towns, key, &v) ? v : -1;
}

static const char *region_get(const rb_geo *g, const char *cc, const char *folded)
{
    char key[1200];
    int v;
    ckey(key, sizeof(key), cc, folded);
    return strmap_get(&g->regions, key, &v) ? g->reg_codes[v] : NULL;
}

static int seat_get(const rb_geo *g, const char *cc, const char *code)
{
    char key[64];
    int v;
    ckey(key, sizeof(key), cc, code);
    return strmap_get(&g->seats, key, &v) ? v : -1;
}

static void region_put(rb_geo *g, const char *cc, const char *folded, const char *code)
{
    char key[1200];
    int v;
    char **nr;
    ckey(key, sizeof(key), cc, folded);
    if (strmap_get(&g->regions, key, &v))
        return; /* setdefault */
    nr = (char **)realloc(g->reg_codes, (size_t)(g->n_reg + 1) * sizeof(char *));
    if (!nr)
        return;
    g->reg_codes = nr;
    g->reg_codes[g->n_reg] = strdup(code);
    strmap_put(&g->regions, key, g->n_reg++);
}

static int upper_code(const char *s, size_t lo, size_t hi)
{
    size_t i, n = strlen(s);
    if (n < lo || n > hi)
        return 0;
    for (i = 0; i < n; i++)
        if (s[i] < 'A' || s[i] > 'Z')
            return 0;
    return 1;
}

long rb_geo_load_admin1(rb_geo *g, const char *text, size_t n)
{
    size_t pos = 0, i;
    long count = 0;
    while (pos < n) {
        size_t e = pos, len;
        char *line, *f[8], *dot, cc[8], code[32], *fn;
        int nf, k;
        while (e < n && text[e] != '\n')
            e++;
        len = e - pos;
        line = (char *)malloc(len + 1);
        if (!line)
            return -1;
        memcpy(line, text + pos, len);
        line[len] = '\0';
        pos = e + 1;
        /* Python's split("\t") keeps every field; three are enough. */
        nf = 1;
        f[0] = line;
        for (k = 0; line[k] && nf < 8; k++)
            if (line[k] == '\t') {
                line[k] = '\0';
                f[nf++] = line + k + 1;
            }
        dot = strchr(f[0], '.');
        if (nf < 3 || !dot || (size_t)(dot - f[0]) >= sizeof(cc) ||
            strlen(dot + 1) >= sizeof(code)) {
            free(line);
            continue;
        }
        memcpy(cc, f[0], (size_t)(dot - f[0]));
        cc[dot - f[0]] = '\0';
        snprintf(code, sizeof(code), "%s", dot + 1);
        if (seat_get(g, cc, code) < 0) {
            free(line);
            continue;
        }
        count++;
        for (k = 1; k <= 2; k++) {
            fn = rfold_utf8(f[k], strlen(f[k]));
            if (fn)
                region_put(g, cc, fn, code);
            free(fn);
        }
        /* Letter codes are the usual abbreviations: US "TX", CH "ZH". */
        if (upper_code(code, 2, 3)) {
            char low[8];
            for (i = 0; code[i]; i++)
                low[i] = (char)tolower((unsigned char)code[i]);
            low[i] = '\0';
            region_put(g, cc, low, code);
        }
        free(line);
    }
    for (i = 0; i < sizeof(REGION_ALIASES) / sizeof(REGION_ALIASES[0]); i++)
        if (seat_get(g, REGION_ALIASES[i][0], REGION_ALIASES[i][2]) >= 0)
            region_put(g, REGION_ALIASES[i][0], REGION_ALIASES[i][1], REGION_ALIASES[i][2]);
    return count;
}

static const char *gb_area(const char *folded)
{
    size_t i;
    for (i = 0; i < sizeof(GB_AREAS) / sizeof(GB_AREAS[0]); i++)
        if (!strcmp(GB_AREAS[i][0], folded))
            return GB_AREAS[i][1];
    return NULL;
}

/* The town GB_AREAS gives for an area, or -1. */
static int gb_area_town(const rb_geo *g, const char *folded)
{
    const char *t = gb_area(folded);
    int r = -1;
    if (t) {
        char *ft = rfold_utf8(t, strlen(t));
        if (ft)
            r = town_get(g, "GB", ft);
        free(ft);
    }
    return r;
}

static int in_list(const char *const *list, size_t n, const char *s)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (!strcmp(list[i], s))
            return 1;
    return 0;
}

/* Words of rfold("".join(ch if ch.isalnum() else " " for ch in s)): the
 * folded text and where each word starts and ends. */
typedef struct {
    ustr w;
    size_t *ws, *we, n;
} words;

static void words_of(const ustr *s, words *out)
{
    size_t i;
    ustr_init(&out->w);
    ustr_copy(&out->w, s);
    alnum_words(&out->w);
    rfold(&out->w);
    out->ws = (size_t *)malloc((out->w.n + 1) * sizeof(size_t));
    out->we = (size_t *)malloc((out->w.n + 1) * sizeof(size_t));
    if (!out->ws || !out->we)
        abort();
    out->n = 0;
    for (i = 0; i < out->w.n;) {
        while (i < out->w.n && out->w.c[i] == ' ')
            i++;
        if (i >= out->w.n)
            break;
        out->ws[out->n] = i;
        while (i < out->w.n && out->w.c[i] != ' ')
            i++;
        out->we[out->n++] = i;
    }
}

static void words_free(words *w)
{
    ustr_free(&w->w);
    free(w->ws);
    free(w->we);
}

/* Words start..start+len-1 joined, as UTF-8 (malloc'd), and its length in
 * code points. */
static char *words_key(const words *w, size_t start, size_t len, size_t *cps)
{
    ustr k;
    size_t i;
    char *r;
    ustr_init(&k);
    for (i = w->ws[start]; i < w->we[start + len - 1]; i++)
        ustr_push(&k, w->w.c[i]);
    *cps = k.n;
    r = ustr_utf8(&k);
    ustr_free(&k);
    return r;
}

/* Cities.town_in_name(): a town of NOCOORD_TOWN_POP or more in cc (and in
 * region admin1, if given) named in text. Returns the city or -1. */
static int town_in_name(const rb_geo *g, const ustr *text, const char *cc, const char *admin1)
{
    words w;
    int nlen, hit = -1;
    words_of(text, &w);
    for (nlen = 3; nlen >= 1 && hit < 0; nlen--) {
        size_t st;
        for (st = 0; st + (size_t)nlen <= w.n && hit < 0; st++) {
            size_t cps;
            char *key = words_key(&w, st, (size_t)nlen, &cps);
            int t;
            if (key && cps >= 4 &&
                !in_list(TOWN_STOPWORDS, sizeof(TOWN_STOPWORDS) / sizeof(TOWN_STOPWORDS[0]), key)) {
                t = town_get(g, cc, key);
                if (t >= 0 && g->cities[t].pop >= NOCOORD_TOWN_POP &&
                    (!admin1 || !admin1[0] || !strcmp(g->cities[t].admin1, admin1)))
                    hit = t;
            }
            free(key);
        }
    }
    words_free(&w);
    return hit;
}

/* Cities.region_in_text(): a region (or a UK county) named in text.
 * Returns the city to place it at, or -1. */
static int region_in_text(const rb_geo *g, const ustr *text, const char *cc,
                          const char *country_folded)
{
    words w;
    int nlen, hit = -1;
    words_of(text, &w);
    for (nlen = 4; nlen >= 1 && hit < 0; nlen--) {
        size_t st;
        for (st = 0; st + (size_t)nlen <= w.n && hit < 0; st++) {
            size_t cps, k;
            char *key = words_key(&w, st, (size_t)nlen, &cps);
            int generic = 1;
            const char *code;
            if (!key)
                continue;
            for (k = 0; k < (size_t)nlen; k++) {
                size_t c2;
                char *one = words_key(&w, st + k, 1, &c2);
                if (!one || !in_list(REGION_GENERIC,
                                     sizeof(REGION_GENERIC) / sizeof(REGION_GENERIC[0]), one))
                    generic = 0;
                free(one);
            }
            if (cps < 5 || generic || !strcmp(key, country_folded)) {
                free(key);
                continue;
            }
            if (!strcmp(cc, "GB") && gb_area(key)) {
                int t = gb_area_town(g, key);
                if (t >= 0) {
                    hit = t;
                    free(key);
                    break;
                }
            }
            code = region_get(g, cc, key);
            if (code)
                hit = seat_get(g, cc, code);
            free(key);
        }
    }
    words_free(&w);
    return hit;
}

/* region_variants(): ways to read Radio Browser's region field, rfold()ed,
 * in order, without repeats. */
#define MAX_VARIANTS 24
typedef struct {
    char *v[MAX_VARIANTS];
    int n;
} variants;

static void var_add(variants *vs, const char *t, size_t len)
{
    char *f = rfold_utf8(t, len);
    int i;
    if (!f || !f[0] || vs->n >= MAX_VARIANTS) {
        free(f);
        return;
    }
    for (i = 0; i < vs->n; i++)
        if (!strcmp(vs->v[i], f)) {
            free(f);
            return;
        }
    vs->v[vs->n++] = f;
}

static void variants_free(variants *vs)
{
    int i;
    for (i = 0; i < vs->n; i++)
        free(vs->v[i]);
    vs->n = 0;
}

static int ends_with(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && !memcmp(s + a - b, suf, b);
}

static void region_variants(const ustr *text, const char *country_folded, variants *vs)
{
    static const char *const SUFFIXES[] = { " province", " state", " region", " oblast",
                                            " county", " prefecture", " city", " town" };
    static const char *const PREFIXES[] = { "province of ", "state of ", "region of ",
                                            "city of ", "provincia de ", "provincia di ",
                                            "region de ", "estado de ", "departamento de ",
                                            "grad " };
    ustr t;
    char *tu, *v, *sp;
    size_t i;
    long at;
    vs->n = 0;
    ustr_init(&t);
    ustr_copy(&t, text);
    clean_u(&t, 100);
    tu = ustr_utf8(&t);
    if (tu)
        var_add(vs, tu, strlen(tu));
    free(tu);
    at = ustr_find_ascii(&t, " (");
    if (at >= 0)
        ustr_truncate(&t, (size_t)at);
    ustr_strip(&t);
    tu = ustr_utf8(&t);
    if (!tu) {
        ustr_free(&t);
        return;
    }
    var_add(vs, tu, strlen(tu));
    /* clean() left single ASCII spaces: the last word starts after the
     * last one. */
    sp = strrchr(tu, ' ');
    if (sp && upper_code(sp + 1, 2, 3)) {
        *sp = '\0';
        var_add(vs, tu, strlen(tu));
    }
    v = rfold_utf8(tu, strlen(tu));
    free(tu);
    ustr_free(&t);
    if (!v)
        return;
    if (country_folded[0]) {
        size_t lv = strlen(v), lc = strlen(country_folded);
        if (lv > lc && v[lv - lc - 1] == ' ' && !strcmp(v + lv - lc, country_folded))
            var_add(vs, v, lv - lc - 1);
    }
    for (i = 0; i < sizeof(SUFFIXES) / sizeof(SUFFIXES[0]); i++)
        if (ends_with(v, SUFFIXES[i]))
            var_add(vs, v, strlen(v) - strlen(SUFFIXES[i]));
    for (i = 0; i < sizeof(PREFIXES) / sizeof(PREFIXES[0]); i++)
        if (!strncmp(v, PREFIXES[i], strlen(PREFIXES[i])))
            var_add(vs, v + strlen(PREFIXES[i]), strlen(v) - strlen(PREFIXES[i]));
    free(v);
}

/* Splits UTF-8 parts at an ASCII separator (safe: UTF-8 never has ASCII
 * bytes inside a character), as Python's str.split(sep). */
static void split_parts(char ***parts, int *n, const char *sep)
{
    char **out = NULL;
    int no = 0, i;
    size_t sl = strlen(sep);
    for (i = 0; i < *n; i++) {
        const char *p = (*parts)[i];
        while (1) {
            const char *e = strstr(p, sep);
            size_t len = e ? (size_t)(e - p) : strlen(p);
            char **nb = (char **)realloc(out, (size_t)(no + 1) * sizeof(char *));
            if (!nb)
                abort();
            out = nb;
            out[no] = (char *)malloc(len + 1);
            if (!out[no])
                abort();
            memcpy(out[no], p, len);
            out[no][len] = '\0';
            no++;
            if (!e)
                break;
            p = e + sl;
        }
        free((*parts)[i]);
    }
    free(*parts);
    *parts = out;
    *n = no;
}

typedef struct {
    int city;          /* where it goes */
    const char *how;   /* "region_town" or "admin1" */
    const char *code;  /* the admin1 code for "admin1" */
} region_hit;

/* Cities.region_place(). Returns 0 if nothing matched. */
static int region_place(const rb_geo *g, const char *cc, const ustr *text,
                        const char *country_folded, region_hit *out)
{
    static const char *const SEPS[] = { ",", "/", "(", " - " };
    char **parts, *tu;
    int nparts = 1, i, k, found = 0;
    ustr t;
    tu = ustr_utf8(text);
    if (!tu)
        return 0;
    parts = (char **)malloc(sizeof(char *));
    if (!parts)
        abort();
    parts[0] = strdup(tu);
    for (k = 0; k < 4; k++)
        split_parts(&parts, &nparts, SEPS[k]);
    ustr_init(&t);
    for (i = -1; i < (nparts > 1 ? nparts : 0) && !found; i++) {
        variants vs;
        int j;
        if (i < 0) {
            ustr_copy(&t, text);
        } else {
            /* p.strip(" )") */
            char *p = parts[i], *e = p + strlen(p);
            while (*p == ' ' || *p == ')')
                p++;
            while (e > p && (e[-1] == ' ' || e[-1] == ')'))
                e--;
            if (e == p)
                continue;
            ustr_set_utf8(&t, p, (size_t)(e - p));
        }
        region_variants(&t, country_folded, &vs);
        for (j = 0; j < vs.n && !found; j++) {
            const char *v = vs.v[j], *code;
            int town;
            if (!strcmp(v, country_folded))
                continue; /* "Mexico" is a state of Mexico too */
            town = town_get(g, cc, v);
            code = region_get(g, cc, v);
            if (code && !(town >= 0 && g->cities[town].pop >= REGION_TOWN_WINS_POP)) {
                out->city = seat_get(g, cc, code);
                out->how = "admin1";
                out->code = code;
                found = 1;
            } else if (town >= 0) {
                out->city = town;
                out->how = "region_town";
                out->code = "";
                found = 1;
            } else if (!strcmp(cc, "GB") && gb_area(v)) {
                town = gb_area_town(g, v);
                if (town >= 0) {
                    out->city = town;
                    out->how = "region_town";
                    out->code = "";
                    found = 1;
                }
            }
        }
        variants_free(&vs);
    }
    ustr_free(&t);
    for (i = 0; i < nparts; i++)
        free(parts[i]);
    free(parts);
    free(tu);
    if (!found) {
        /* "Seattle Washington", "Heraklion Crete": a town inside it. */
        int town = town_in_name(g, text, cc, NULL);
        if (town >= 0) {
            out->city = town;
            out->how = "region_town";
            out->code = "";
            found = 1;
        }
    }
    return found;
}

/* host_of(): a URL's host name, lower case, without "www."; "" if none. */
static void host_of(const json_value *v, char *out, size_t n)
{
    ustr u;
    char *s, *p, *e;
    out[0] = '\0';
    ustr_init(&u);
    if (!py_str(v, &u)) {
        ustr_free(&u);
        return;
    }
    ustr_strip(&u);
    ustr_lower(&u);
    s = ustr_utf8(&u);
    ustr_free(&u);
    if (!s)
        return;
    p = strstr(s, "://");
    if (p) {
        p += 3;
        for (e = p; *e && *e != '/' && *e != '?' && *e != '#'; e++)
            ;
        *e = '\0';
        if (strrchr(p, '@'))
            p = strrchr(p, '@') + 1;
        if (strchr(p, ':'))
            *strchr(p, ':') = '\0';
        if (!strncmp(p, "www.", 4))
            p += 4;
        snprintf(out, n, "%s", p);
    }
    free(s);
}

typedef struct {
    double lat, lon;
    char cc[8];
    char hosts[2][256];
} sister;

static void station_hosts(const json_value *st, char hosts[2][256])
{
    host_of(json_get(st, "homepage"), hosts[0], 256);
    host_of(json_get(st, "url_resolved"), hosts[1], 256);
}

static int hosts_meet(char a[2][256], char b[2][256])
{
    int i, j;
    for (i = 0; i < 2; i++)
        for (j = 0; j < 2; j++)
            if (a[i][0] && !strcmp(a[i], b[j]))
                return 1;
    return 0;
}

/* sister_positions(): (cc, folded name) -> the position of the first
 * station with coordinates by that name, as check_position() leaves it. */
static void sister_positions(rb_geo *g, const json_value *const *stations, size_t n,
                             strmap *map, sister **list, size_t *nlist)
{
    size_t i, cap = 0;
    ustr name, cc, region, k;
    ustr_init(&name);
    ustr_init(&cc);
    ustr_init(&region);
    ustr_init(&k);
    *list = NULL;
    *nlist = 0;
    for (i = 0; i < n; i++) {
        const json_value *st = stations[i];
        double lat, lon;
        char key[1200], *ccs, *kn;
        int dummy;
        position pos;
        if (!is_one(json_get(st, "lastcheckok")))
            continue;
        if (!coord(json_get(st, "geo_lat"), -90, 90, &lat) ||
            !coord(json_get(st, "geo_long"), -180, 180, &lon) ||
            (fabs(lat) < 1e-6 && fabs(lon) < 1e-6))
            continue;
        clean_name(st, &name);
        clean_field(st, "countrycode", 2, &cc);
        ustr_upper_ascii(&cc);
        if (!name.n || !cc.n)
            continue;
        ustr_copy(&k, &name);
        ustr_fold(&k);
        ccs = ustr_utf8(&cc);
        kn = ustr_utf8(&k);
        if (!ccs || !kn) {
            free(ccs);
            free(kn);
            continue;
        }
        ckey(key, sizeof(key), ccs, kn);
        free(kn);
        if (strmap_get(map, key, &dummy)) {
            free(ccs);
            continue;
        }
        clean_field(st, "state", 60, &region);
        if (check_position(g, lat, lon, ccs, &region, &name, &pos)) {
            if (*nlist == cap) {
                size_t nc = cap ? cap * 2 : 1024;
                sister *nl = (sister *)realloc(*list, nc * sizeof(sister));
                if (!nl)
                    abort();
                *list = nl;
                cap = nc;
            }
            (*list)[*nlist].lat = pos.lat;
            (*list)[*nlist].lon = pos.lon;
            snprintf((*list)[*nlist].cc, sizeof((*list)[0].cc), "%s", pos.cc);
            station_hosts(st, (*list)[*nlist].hosts);
            strmap_put(map, key, (int)(*nlist)++);
        }
        free(ccs);
    }
    ustr_free(&name);
    ustr_free(&cc);
    ustr_free(&region);
    ustr_free(&k);
}

/* Cities.place_without_coords(). Returns 0 if the station can't be placed. */
static int place_without_coords(rb_geo *g, const char *cc, const ustr *country, const ustr *name,
                                 const ustr *region, const ustr *tags, char hosts[2][256],
                                 const strmap *sisters, sister *slist, position *out)
{
    char key[1200], *kn, *cf;
    ustr t;
    region_hit rh;
    int have_region = 0, c, si;
    {
        ustr k;
        ustr_init(&k);
        ustr_copy(&k, name);
        ustr_fold(&k);
        kn = ustr_utf8(&k);
        ustr_free(&k);
    }
    ckey(key, sizeof(key), cc, kn ? kn : "");
    free(kn);
    snprintf(out->cc, sizeof(out->cc), "%s", cc);
    if (strmap_get(sisters, key, &si) && hosts_meet(slist[si].hosts, hosts)) {
        out->lat = slist[si].lat;
        out->lon = slist[si].lon;
        out->how = "sister";
        snprintf(out->cc, sizeof(out->cc), "%s", slist[si].cc);
        return 1;
    }
    {
        ustr cfu;
        ustr_init(&cfu);
        ustr_copy(&cfu, country);
        rfold(&cfu);
        cf = ustr_utf8(&cfu);
        ustr_free(&cfu);
        if (!cf)
            return 0;
    }
    if (region->n)
        have_region = region_place(g, cc, region, cf, &rh);
    if (have_region && !strcmp(rh.how, "region_town")) {
        out->lat = g->cities[rh.city].lat;
        out->lon = g->cities[rh.city].lon;
        out->how = "region_town";
        free(cf);
        return 1;
    }
    /* A town in the name is more precise than a region, but only one in
     * that region. */
    c = town_in_name(g, name, cc, have_region ? rh.code : NULL);
    if (c >= 0) {
        out->lat = g->cities[c].lat;
        out->lon = g->cities[c].lon;
        out->how = "name_town";
        free(cf);
        return 1;
    }
    if (have_region) {
        out->lat = g->cities[rh.city].lat;
        out->lon = g->cities[rh.city].lon;
        out->how = "admin1";
        free(cf);
        return 1;
    }
    ustr_init(&t);
    ustr_copy(&t, name);
    ustr_push(&t, ' ');
    ustr_append(&t, tags);
    c = region_in_text(g, &t, cc, cf);
    ustr_free(&t);
    free(cf);
    if (c >= 0) {
        out->lat = g->cities[c].lat;
        out->lon = g->cities[c].lon;
        out->how = "name_region";
        return 1;
    }
    if (strmap_get(&g->labels, cc, &c)) {
        out->lat = g->lab[c][0];
        out->lon = g->lab[c][1];
        out->how = "country";
        return 1;
    }
    return 0;
}

static int add_skip(rb_result *r, const char *why)
{
    size_t i;
    for (i = 0; i < r->nskipped; i++)
        if (!strcmp(r->skipped[i].why, why)) {
            r->skipped[i].count++;
            return 0;
        }
    {
        void *ns = realloc(r->skipped, (r->nskipped + 1) * sizeof(r->skipped[0]));
        if (!ns)
            return -1;
        r->skipped = ns;
    }
    snprintf(r->skipped[r->nskipped].why, sizeof(r->skipped[0].why), "%s", why);
    r->skipped[r->nskipped].count = 1;
    r->nskipped++;
    return 0;
}

static int contains_mark(const ustr *s)
{
    ustr low;
    int i, hit = 0;
    ustr_init(&low);
    ustr_copy(&low, s);
    ustr_lower(&low);
    for (i = 0; i < 4 && !hit; i++)
        if (ustr_find_ascii(&low, RADIO_GARDEN_MARKS[i]) >= 0)
            hit = 1;
    ustr_free(&low);
    return hit;
}

static void rstrip_slashes(ustr *s)
{
    while (s->n && s->c[s->n - 1] == '/')
        s->n--;
}

int rb_convert(const json_value *const *stations, size_t n, const rb_options *o, rb_geo *geo,
               rb_result *out)
{
    strmap seen_uuid, seen_url;
    ustr uuid, codec, url, home, name, country, cc, region, city, notes, genre, tmp, tmp2;
    size_t i, cap = 0, nsisters = 0;
    int rc = 0, without = o->without_coords && geo;
    strmap sisters;
    sister *slist = NULL;

    memset(out, 0, sizeof(*out));
    strmap_init(&sisters);
    strmap_init(&seen_uuid);
    strmap_init(&seen_url);
    ustr_init(&uuid);
    ustr_init(&codec);
    ustr_init(&url);
    ustr_init(&home);
    ustr_init(&name);
    ustr_init(&country);
    ustr_init(&cc);
    ustr_init(&region);
    ustr_init(&city);
    ustr_init(&notes);
    ustr_init(&genre);
    ustr_init(&tmp);
    ustr_init(&tmp2);
    if (geo)
        build_country_names(geo, stations, n);
    if (without)
        sister_positions(geo, stations, n, &sisters, &slist, &nsisters);

    for (i = 0; i < n && rc == 0; i++) {
        const json_value *st = stations[i];
        double lat, lon;
        int is_hls, dummy, have_home, relabelled = 0, no_coords;
        long bitrate;
        char *s_uuid, *s_url, buf[200];
        const char *geo_how = "ok", *place_from = "none";
        char codec_s[96];

        clean_field(st, "stationuuid", 40, &uuid);
        s_uuid = ustr_utf8(&uuid);
        if (strmap_get(&seen_uuid, s_uuid, &dummy)) {
            free(s_uuid);
            continue;
        }
        strmap_put(&seen_uuid, s_uuid, 1);
        is_hls = is_one(json_get(st, "hls"));
        if (is_hls && !o->hls) {
            rc = add_skip(out, "HLS stream");
            free(s_uuid);
            continue;
        }
        if (!is_one(json_get(st, "lastcheckok"))) {
            rc = add_skip(out, "last check failed");
            free(s_uuid);
            continue;
        }
        no_coords = !coord(json_get(st, "geo_lat"), -90, 90, &lat) ||
                    !coord(json_get(st, "geo_long"), -180, 180, &lon) ||
                    (fabs(lat) < 1e-6 && fabs(lon) < 1e-6);
        if (no_coords && !without) {
            rc = add_skip(out, "no usable coordinates");
            free(s_uuid);
            continue;
        }
        clean_field(st, "codec", 20, &codec);
        ustr_upper_ascii(&codec);
        {
            char *c = ustr_utf8(&codec);
            snprintf(codec_s, sizeof(codec_s), "%s", c ? c : "");
            free(c);
        }
        if (is_hls) {
            char t[96];
            if (!codec_s[0] || !strcmp(codec_s, "UNKNOWN"))
                snprintf(t, sizeof(t), "HLS");
            else
                snprintf(t, sizeof(t), "%.88s/HLS", codec_s);
            memcpy(codec_s, t, sizeof(t));
        } else {
            int k, ok = 0;
            for (k = 0; k < N_CODECS; k++)
                if ((o->codecs & (1u << k)) && !strcmp(codec_s, CODEC_RB[k]))
                    ok = 1;
            if (!ok) {
                snprintf(buf, sizeof(buf), "codec %s not selected",
                         codec_s[0] ? codec_s : "unknown");
                rc = add_skip(out, buf);
                free(s_uuid);
                continue;
            }
        }
        {
            const json_value *bv = json_get(st, "bitrate");
            int falsy = !bv || bv->type == JSON_NULL || bv->type == JSON_FALSE ||
                        (bv->type == JSON_NUMBER && bv->num == 0) ||
                        (bv->type == JSON_STRING && bv->len == 0);
            bitrate = falsy ? 0 : py_int(bv);
        }
        if (o->min_bitrate && bitrate < o->min_bitrate) {
            snprintf(buf, sizeof(buf), "bitrate below %dk or unknown", o->min_bitrate);
            rc = add_skip(out, buf);
            free(s_uuid);
            continue;
        }
        if (!py_str(json_get(st, "url_resolved"), &url) || !clean_url(&url)) {
            rc = add_skip(out, "no usable stream URL");
            free(s_uuid);
            continue;
        }
        have_home = py_str(json_get(st, "homepage"), &home) && clean_homepage(&home);
        if (contains_mark(&url) || (have_home && contains_mark(&home))) {
            rc = add_skip(out, "URL refers to Radio Garden");
            free(s_uuid);
            continue;
        }
        s_url = ustr_utf8(&url);
        if (strmap_get(&seen_url, s_url, &dummy)) {
            rc = add_skip(out, "duplicate stream URL");
            free(s_uuid);
            free(s_url);
            continue;
        }
        clean_name(st, &name);
        if (!name.n) {
            rc = add_skip(out, "no name");
            free(s_uuid);
            free(s_url);
            continue;
        }
        if (is_test(&name)) {
            rc = add_skip(out, "test broadcast");
            free(s_uuid);
            free(s_url);
            continue;
        }
        country_name(st, &country);
        clean_field(st, "countrycode", 2, &cc);
        ustr_upper_ascii(&cc);
        if (geo) {
            position pos;
            char *ccs = ustr_utf8(&cc), terr_cc[3];
            const char *label;
            int ci;
            clean_field(st, "state", 60, &region);
            if (no_coords) {
                char hosts[2][256];
                ustr tags;
                int ok;
                ustr_init(&tags);
                clean_field(st, "tags", 300, &tags);
                station_hosts(st, hosts);
                ok = cc.n && place_without_coords(geo, ccs, &country, &name, &region, &tags,
                                                  hosts, &sisters, slist, &pos);
                ustr_free(&tags);
                if (!ok) {
                    rc = add_skip(out, "no coordinates, and no country to place it in");
                    free(ccs);
                    free(s_uuid);
                    free(s_url);
                    continue;
                }
            } else if (!check_position(geo, lat, lon, ccs, &region, &name, &pos)) {
                rc = add_skip(out, "coordinates in the open sea, no country to fall back on");
                free(ccs);
                free(s_uuid);
                free(s_url);
                continue;
            }
            lat = pos.lat;
            lon = pos.lon;
            geo_how = pos.how;
            /* The position wins: Radio Browser's name for that country, or
             * the borders file's when no station in the snapshot is in it. */
            if (strcmp(pos.cc, ccs) != 0) {
                const char *nm = NULL;
                if (strmap_get(&geo->country_names, pos.cc, &ci))
                    nm = geo->cn_names[ci];
                else if (strmap_get(&geo->border_names, pos.cc, &ci))
                    nm = geo->bn_names[ci];
                if (nm) {
                    ustr_set_utf8(&country, nm, strlen(nm));
                    ustr_clear(&cc);
                    ustr_append_ascii(&cc, pos.cc);
                }
            }
            free(ccs);
            if (strcmp(geo_how, "country") != 0 && territory(geo, lat, lon, terr_cc, &label) &&
                !ustr_eq_ascii(&country, label)) {
                ustr_clear(&cc);
                ustr_append_ascii(&cc, terr_cc);
                ustr_set_utf8(&country, label, strlen(label));
                relabelled = 1;
            }
        }
        ustr_clear(&city);
        if (!strcmp(geo_how, "country")) {
            ustr_append_ascii(&city, COUNTRY_WIDE);
            place_from = "country";
        } else if (geo) {
            char *ccs = ustr_utf8(&cc);
            int c = nearest_city(geo, lat, lon, o->city_km, ccs);
            free(ccs);
            if (c >= 0) {
                ustr_set_utf8(&city, geo->cities[c].name, strlen(geo->cities[c].name));
                place_from = "city";
            }
        }
        if (!city.n && strcmp(geo_how, "country") != 0) {
            clean_field(st, "state", 60, &region);
            if (region.n) {
                size_t k;
                int digit = 0;
                ustr_copy(&tmp, &region);
                ustr_lower(&tmp);
                ustr_copy(&tmp2, &country);
                ustr_lower(&tmp2);
                for (k = 0; k < region.n; k++)
                    if (u_isdigit(region.c[k]))
                        digit = 1;
                if (!ustr_eq(&tmp, &tmp2) && !digit) {
                    ustr_copy(&city, &region);
                    place_from = "region";
                }
            }
        }
        ustr_clear(&notes);
        if (bitrate)
            snprintf(buf, sizeof(buf), "%s %ldk rb:", codec_s, bitrate);
        else
            snprintf(buf, sizeof(buf), "%s ?k rb:", codec_s);
        ustr_append_ascii(&notes, buf);
        ustr_append(&notes, &uuid);
        if (have_home) {
            ustr_copy(&tmp, &home);
            rstrip_slashes(&tmp);
            ustr_copy(&tmp2, &url);
            rstrip_slashes(&tmp2);
            if (!ustr_eq(&tmp, &tmp2)) {
                ustr_append_ascii(&notes, " web:");
                ustr_append(&notes, &home);
            }
        }
        if (strcmp(geo_how, "ok") != 0) {
            ustr_push(&notes, ' ');
            ustr_append_ascii(&notes, geo_note(geo_how));
        }
        if (relabelled) {
            ustr_append_ascii(&notes, " (country: ");
            ustr_append(&notes, &country);
            ustr_append_ascii(&notes, ", where the position is)");
        }
        genre_from_tags(st, &genre);
        {
            ustr line;
            char num[64];
            ustr_init(&line);
            ustr_append(&line, &name);
            ustr_append_ascii(&line, " | ");
            ustr_append(&line, &url);
            ustr_append_ascii(&line, " | ");
            ustr_append(&line, &country);
            ustr_append_ascii(&line, " | ");
            ustr_append(&line, &city);
            snprintf(num, sizeof(num), " | %.4f | %.4f | ", lat, lon);
            ustr_append_ascii(&line, num);
            ustr_append(&line, &genre);
            ustr_append_ascii(&line, " | ");
            ustr_append(&line, &notes);
            if (out->n == cap) {
                size_t nc = cap ? cap * 2 : 256;
                rb_line *nl = (rb_line *)realloc(out->lines, nc * sizeof(rb_line));
                if (!nl) {
                    ustr_free(&line);
                    free(s_uuid);
                    free(s_url);
                    rc = -1;
                    break;
                }
                out->lines = nl;
                cap = nc;
            }
            out->lines[out->n].line = ustr_utf8(&line);
            snprintf(out->lines[out->n].codec, sizeof(out->lines[0].codec), "%s", codec_s);
            out->lines[out->n].place_from = place_from;
            out->lines[out->n].geo = geo_how;
            out->n++;
            ustr_free(&line);
        }
        strmap_put(&seen_url, s_url, 1);
        free(s_uuid);
        free(s_url);
        if ((long)out->n >= o->max)
            break;
    }

    strmap_free(&sisters);
    free(slist);
    strmap_free(&seen_uuid);
    strmap_free(&seen_url);
    ustr_free(&uuid);
    ustr_free(&codec);
    ustr_free(&url);
    ustr_free(&home);
    ustr_free(&name);
    ustr_free(&country);
    ustr_free(&cc);
    ustr_free(&region);
    ustr_free(&city);
    ustr_free(&notes);
    ustr_free(&genre);
    ustr_free(&tmp);
    ustr_free(&tmp2);
    return rc;
}

void rb_result_free(rb_result *r)
{
    size_t i;
    for (i = 0; i < r->n; i++)
        free(r->lines[i].line);
    free(r->lines);
    free(r->skipped);
    memset(r, 0, sizeof(*r));
}

/* ---- text around the lines ---------------------------------------------- */

typedef struct {
    char *p;
    size_t n, cap;
} sbuf;

static void sb_add(sbuf *b, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

#include <stdarg.h>
static void sb_add(sbuf *b, const char *fmt, ...)
{
    va_list ap;
    int k;
    for (;;) {
        size_t room = b->cap - b->n;
        va_start(ap, fmt);
        k = vsnprintf(b->p ? b->p + b->n : NULL, b->p ? room : 0, fmt, ap);
        va_end(ap);
        if (k < 0)
            return;
        if (b->p && (size_t)k < room) {
            b->n += (size_t)k;
            return;
        }
        {
            size_t nc = (b->cap ? b->cap * 2 : 1024) + (size_t)k;
            char *np = (char *)realloc(b->p, nc);
            if (!np)
                return;
            b->p = np;
            b->cap = nc;
        }
    }
}

static void codec_list(const rb_options *o, char *out, size_t n)
{
    int k;
    out[0] = '\0';
    for (k = 0; k < N_CODECS; k++)
        if (o->codecs & (1u << k)) {
            if (out[0])
                strncat(out, ",", n - strlen(out) - 1);
            strncat(out, CODEC_OPT[k], n - strlen(out) - 1);
        }
}

char *rb_header(const rb_options *o, int with_cities, size_t count, time_t now, time_t snapshot,
                const char *made_by, const char *bundle)
{
    sbuf b = { NULL, 0, 0 };
    char when[32], snap[16], codecs[64], opts[600];
    struct tm tm;
    int k;
    gmtime_r(&now, &tm);
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M UTC", &tm);
    gmtime_r(&snapshot, &tm);
    strftime(snap, sizeof(snap), "%Y-%m-%d", &tm);
    codec_list(o, codecs, sizeof(codecs));
    snprintf(opts, sizeof(opts), "codecs %s", codecs);
    if (o->ncountries) {
        strncat(opts, ", countries ", sizeof(opts) - strlen(opts) - 1);
        for (k = 0; k < o->ncountries; k++) {
            if (k)
                strncat(opts, ",", sizeof(opts) - strlen(opts) - 1);
            strncat(opts, o->countries[k], sizeof(opts) - strlen(opts) - 1);
        }
    }
    if (o->min_bitrate) {
        char t[48];
        snprintf(t, sizeof(t), ", min bitrate %dk", o->min_bitrate);
        strncat(opts, t, sizeof(opts) - strlen(opts) - 1);
    }
    if (o->tag[0]) {
        char t[96];
        snprintf(t, sizeof(t), ", tag '%s'", o->tag);
        strncat(opts, t, sizeof(opts) - strlen(opts) - 1);
    }
    if (o->hls)
        strncat(opts, ", HLS included", sizeof(opts) - strlen(opts) - 1);
    if (bundle) {
        sb_add(&b, "# DX Garden default station list, shipped with DX Garden %s.\n", bundle);
        sb_add(&b, "# Correct as of %s. Stations come and go: any that are dead or wrong are\n",
               snap);
        sb_add(&b, "# yours to edit, delete or replace. This file is yours; the app never\n");
        sb_add(&b, "# overwrites it. Delete it to get the default list back on the next start.\n");
        sb_add(&b, "#\n");
    }
    sb_add(&b, "# DX Garden stations.txt generated by %s on %s.\n", made_by, when);
    sb_add(&b, "# Source: a snapshot of Radio Browser (radio-browser.info) taken %s, %lu "
               "stations, %s.\n", snap, (unsigned long)count, opts);
    sb_add(&b, "# Cities: nearest place within %g km from GeoNames (geonames.org, CC BY 4.0)%s;\n",
           o->city_km, with_cities ? "" : " - not used");
    sb_add(&b, "# otherwise Radio Browser's region, otherwise blank.\n");
    if (with_cities)
        sb_add(&b, "# Positions outside the station's country were checked and fixed (see "
                   "Notes).\n");
    if (with_cities && o->without_coords)
        sb_add(&b, "# Stations without coordinates are placed by their region, a town or "
                   "region in their name, or in a Country-wide place (see Notes).\n");
    sb_add(&b, "# Fields: Name | Stream URL | Country | City | Latitude | Longitude | Genre | "
               "Notes\n");
    sb_add(&b, "# Notes: codec, bitrate, the Radio Browser station id (rb:<uuid>) and\n");
    sb_add(&b, "# the station's website (web:<url>) when Radio Browser has one.\n");
    return b.p;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

char *rb_report(const rb_result *r, const rb_options *o, int with_cities, const char *output,
                size_t bytes)
{
    sbuf b = { NULL, 0, 0 };
    size_t i, k, with_city = 0, with_region = 0, with_country = 0;
    const char *codecs[64];
    long counts[64];
    size_t nc = 0;
#define N_GEO 14
    static const char *const GEO[N_GEO] = { "ok",     "swapped",     "flipped", "ashore",
                                            "named",  "trusted",     "region",  "capital",
                                            "sister", "region_town", "admin1",  "name_town",
                                            "name_region", "country" };
    long geo[N_GEO] = { 0 };

    for (i = 0; i < r->n; i++) {
        for (k = 0; k < nc; k++)
            if (!strcmp(codecs[k], r->lines[i].codec))
                break;
        if (k == nc && nc < 64) {
            codecs[nc] = r->lines[i].codec;
            counts[nc++] = 0;
        }
        if (k < 64)
            counts[k]++;
        if (!strcmp(r->lines[i].place_from, "city"))
            with_city++;
        else if (!strcmp(r->lines[i].place_from, "region"))
            with_region++;
        else if (!strcmp(r->lines[i].place_from, "country"))
            with_country++;
        for (k = 0; k < N_GEO; k++)
            if (!strcmp(r->lines[i].geo, GEO[k]))
                geo[k]++;
    }
    sb_add(&b, "Wrote %lu stations to %s (%lu KB).\n", (unsigned long)r->n, output,
           (unsigned long)(bytes / 1024));
    sb_add(&b, "  Place: nearest city within %g km %lu, Radio Browser region %lu, none (named by "
               "coordinates on the Vita) %lu", o->city_km, (unsigned long)with_city,
           (unsigned long)with_region,
           (unsigned long)(r->n - with_city - with_region - with_country));
    if (o->without_coords)
        sb_add(&b, ", country-wide %lu", (unsigned long)with_country);
    sb_add(&b, ".\n");
    {
        /* Sorted by codec name, as the script prints them. */
        const char *sorted[64];
        memcpy(sorted, codecs, nc * sizeof(char *));
        qsort(sorted, nc, sizeof(char *), cmp_str);
        sb_add(&b, "  Per codec: ");
        for (i = 0; i < nc; i++) {
            for (k = 0; k < nc; k++)
                if (codecs[k] == sorted[i])
                    break;
            sb_add(&b, "%s%s %ld", i ? ", " : "", sorted[i], counts[k]);
        }
        sb_add(&b, "\n");
    }
    if (with_cities)
        sb_add(&b, "  Positions: ok %ld, swapped %ld, sign flipped %ld, brought ashore %ld, town "
                   "in name %ld, country corrected %ld, moved to region %ld, moved to capital "
                   "%ld.\n", geo[0], geo[1], geo[2], geo[3], geo[4], geo[5], geo[6], geo[7]);
    if (with_cities && o->without_coords)
        sb_add(&b, "  Without coordinates: another stream %ld, region's town %ld, region %ld, "
                   "town in name %ld, region in name %ld, country-wide %ld.\n",
               geo[8], geo[9], geo[10], geo[11], geo[12], geo[13]);
    if (r->nskipped) {
        /* Most first; equal counts in the order first seen (a stable sort). */
        size_t *ord = (size_t *)malloc(r->nskipped * sizeof(size_t));
        if (ord) {
            for (i = 0; i < r->nskipped; i++)
                ord[i] = i;
            for (i = 1; i < r->nskipped; i++) {
                size_t v = ord[i];
                k = i;
                while (k > 0 && r->skipped[ord[k - 1]].count < r->skipped[v].count) {
                    ord[k] = ord[k - 1];
                    k--;
                }
                ord[k] = v;
            }
            sb_add(&b, "Skipped:\n");
            for (i = 0; i < r->nskipped; i++)
                sb_add(&b, "  %6ld  %s\n", r->skipped[ord[i]].count, r->skipped[ord[i]].why);
            free(ord);
        }
    }
    return b.p;
}

/* ---- the query ---------------------------------------------------------- */

/* urlencode's quote_plus for one value. */
static void quote_plus(const char *s, char *out, size_t n)
{
    size_t o = 0;
    for (; *s && o + 4 < n; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '_' || c == '.' || c == '-' || c == '~')
            out[o++] = (char)c;
        else if (c == ' ')
            out[o++] = '+';
        else
            o += (size_t)snprintf(out + o, n - o, "%%%02X", c);
    }
    out[o] = '\0';
}

void rb_query(const rb_options *o, const char *countrycode, long offset, char *out, size_t n)
{
    char t[200];
    int k, one = -1, count = 0;
    snprintf(out, n, "%shidebroken=true&order=clickcount&reverse=true&limit=%d",
             o->without_coords ? "" : "has_geo_info=true&", RB_PAGE_SIZE);
    for (k = 0; k < N_CODECS; k++)
        if (o->codecs & (1u << k)) {
            one = k;
            count++;
        }
    if (count == 1 && !o->hls) {
        quote_plus(CODEC_RB[one], t, sizeof(t));
        strncat(out, "&codec=", n - strlen(out) - 1);
        strncat(out, t, n - strlen(out) - 1);
        strncat(out, "&codecExact=true", n - strlen(out) - 1);
    }
    if (o->min_bitrate) {
        snprintf(t, sizeof(t), "&bitrateMin=%d", o->min_bitrate);
        strncat(out, t, n - strlen(out) - 1);
    }
    if (o->tag[0]) {
        quote_plus(o->tag, t, sizeof(t));
        strncat(out, "&tag=", n - strlen(out) - 1);
        strncat(out, t, n - strlen(out) - 1);
    }
    if (countrycode) {
        strncat(out, "&countrycode=", n - strlen(out) - 1);
        strncat(out, countrycode, n - strlen(out) - 1);
    }
    snprintf(t, sizeof(t), "&offset=%ld", offset);
    strncat(out, t, n - strlen(out) - 1);
}

long rb_wanted(const rb_options *o)
{
    long w = (long)o->max * 4;
    return w > 2000 ? w : 2000;
}
