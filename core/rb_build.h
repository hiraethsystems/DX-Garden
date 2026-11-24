/*
 * Building a stations.txt from a Radio Browser snapshot: the rules of
 * tools/rb_to_stations.py in C, for the Linux app's "Download Station
 * List" (the Vita doesn't use this). Given the same snapshot, GeoNames file
 * and borders, it writes the same station lines as the script; tests and
 * dxgarden_listtool check that.
 *
 *   - filters: codecs, HLS, minimum bitrate, a cap (and the countries and
 *     tag, which go into the query to Radio Browser);
 *   - cleaning of names, URLs, countries and tags;
 *   - with GeoNames (cities1000.txt): the nearest town, a check that the
 *     position is in the station's country (with Natural Earth's borders if
 *     given) and the usual fixes, and the project's country labels by
 *     territory (UK nations, Crown Dependencies, Ukraine, Gaza and the
 *     Occupied West Bank);
 *   - "web:" notes, and nothing that points back at Radio Garden.
 *
 * Portable C99; the caller fetches and unzips the files.
 */
#ifndef DXG_RB_BUILD_H
#define DXG_RB_BUILD_H

#include <stddef.h>
#include <time.h>

#include "json.h"

/* Codecs, as the script's --codecs names them. */
enum { RB_MP3 = 1, RB_AAC = 2, RB_AACP = 4, RB_OGG = 8, RB_OPUS = 16, RB_FLAC = 32, RB_ALL = 63 };
#define RB_MAX_COUNTRIES 64

typedef struct {
    unsigned codecs;      /* RB_* bits */
    int hls;              /* include HLS streams, whatever their codec */
    int min_bitrate;      /* kbps; 0 for none (else unknown bitrates are dropped) */
    int max;              /* most stations to write */
    double city_km;       /* farthest a town can be (default 30) */
    int without_coords;   /* place stations that have no coordinates (needs towns,
                             and rb_geo_load_admin1 for regions) */
    /* Only for the query and the header: */
    char tag[64];
    int ncountries;
    char countries[RB_MAX_COUNTRIES][3];
} rb_options;

void rb_options_default(rb_options *o); /* MP3 only, 2000 stations, 30 km */
/* "mp3,aac" or "all" into o->codecs. Returns 0, or -1 for an unknown name. */
int rb_parse_codecs(rb_options *o, const char *list);
/* "GB,IE" into o->countries. Returns 0, or -1 if one isn't two letters. */
int rb_parse_countries(rb_options *o, const char *list);

/* GeoNames towns and (optionally) country borders. */
typedef struct rb_geo rb_geo;
rb_geo *rb_geo_new(void);
void rb_geo_free(rb_geo *g);
/* The text of a GeoNames cities file (e.g. cities1000.txt). Returns the
 * number of towns, or -1 if out of memory. */
long rb_geo_load_cities(rb_geo *g, const char *text, size_t n);
/* Natural Earth's admin-0 countries GeoJSON. Returns the number of shapes,
 * or -1 with a message. */
long rb_geo_load_borders(rb_geo *g, const char *text, size_t n, char *err, size_t errn);
/* GeoNames' admin1CodesASCII.txt (region names), after the cities. Returns
 * the number of regions with a known capital, or -1 if out of memory. */
long rb_geo_load_admin1(rb_geo *g, const char *text, size_t n);

typedef struct {
    char *line;       /* without the newline */
    char codec[96];   /* "MP3", "AAC/HLS"... */
    const char *place_from; /* "city", "region", "country" (Country-wide) or "none" */
    const char *geo;  /* "ok", "swapped", "flipped", "ashore", "named", "trusted",
                         "region", "capital"; without coordinates "sister",
                         "region_town", "admin1", "name_town", "name_region",
                         "country" */
} rb_line;

typedef struct {
    rb_line *lines;
    size_t n;
    /* Why stations were left out, in the order first seen. */
    struct {
        char why[96];
        long count;
    } *skipped;
    size_t nskipped;
} rb_result;

/* Converts stations (Radio Browser station objects, most popular first)
 * into lines. geo may be NULL (no towns: every place from Radio Browser's
 * region). Returns 0, or -1 if out of memory. */
int rb_convert(const json_value *const *stations, size_t n, const rb_options *o, rb_geo *geo,
               rb_result *out);
void rb_result_free(rb_result *r);

/* The header comment for the file: what made it (e.g. "DX Garden 0.9.1
 * (build 45)"), when, from which snapshot and options. bundle: "for Linux
 * 0.9.2" adds the shipped-list preamble, or NULL. malloc'd. */
char *rb_header(const rb_options *o, int with_cities, size_t count, time_t now, time_t snapshot,
                const char *made_by, const char *bundle);

/* The summary the script prints after writing (stations per codec, how
 * places and positions were found, what was skipped). malloc'd. */
char *rb_report(const rb_result *r, const rb_options *o, int with_cities, const char *output,
                size_t bytes);

/* The query string for one page of /json/stations/search (without '?'):
 * countrycode may be NULL. */
void rb_query(const rb_options *o, const char *countrycode, long offset, char *out, size_t n);
#define RB_PAGE_SIZE 500
/* Pages to fetch per country (the script's bound): stop after this many
 * stations, or at a short page. */
long rb_wanted(const rb_options *o);

#endif
