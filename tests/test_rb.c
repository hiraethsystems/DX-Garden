/* The JSON reader, Python-style text handling, zip reading and the
 * station-list builder (core/rb_build.c), with the cases of
 * tools/test_rb_to_stations.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "json.h"
#include "rb_build.h"
#include "test.h"
#include "ustr.h"
#include "zip_read.h"

static void test_json(void)
{
    char err[100];
    static const char doc[] =
        "\xEF\xBB\xBF [ {\"a\": 1, \"b\": -2.5e1, \"s\": \"x\\u00e9\\ud83d\\ude00\\n\", "
        "\"t\": true, \"f\": false, \"n\": null, \"a\": [1, [2, {}]]}, \"\\ud800x\" ]";
    json_value *v = json_parse(doc, sizeof(doc) - 1, err, sizeof(err));
    const json_value *o;
    CHECK(v && v->type == JSON_ARRAY && v->n == 2);
    if (!v)
        return;
    o = &v->items[0];
    CHECK(json_get(o, "a")->type == JSON_ARRAY); /* the last duplicate wins */
    CHECK(json_get(o, "b")->num == -25.0 && !json_get(o, "b")->is_int);
    CHECK(strcmp(json_str(json_get(o, "s")), "x\xC3\xA9\xF0\x9F\x98\x80\n") == 0);
    CHECK(json_get(o, "t")->type == JSON_TRUE && json_get(o, "n")->type == JSON_NULL);
    CHECK(json_get(o, "missing") == NULL);
    CHECK(strcmp(v->items[1].str, "\xEF\xBF\xBDx") == 0); /* lone surrogate */
    json_free(v);

    CHECK(json_parse("[1,]", 4, err, sizeof(err)) == NULL);
    CHECK(json_parse("{\"a\" 1}", 7, err, sizeof(err)) == NULL && strstr(err, "':'"));
    CHECK(json_parse("[1] x", 5, err, sizeof(err)) == NULL);
    CHECK(json_parse("\"abc", 4, err, sizeof(err)) == NULL);
    v = json_parse("  42  ", 6, err, sizeof(err));
    CHECK(v && v->is_int && v->inum == 42);
    json_free(v);
}

static int fold_is(const char *in, const char *want)
{
    ustr s;
    char *u;
    int ok;
    ustr_init(&s);
    ustr_set_utf8(&s, in, strlen(in));
    ustr_fold(&s);
    u = ustr_utf8(&s);
    ok = u && strcmp(u, want) == 0;
    if (!ok)
        fprintf(stderr, "fold(%s) = %s, want %s\n", in, u ? u : "(null)", want);
    free(u);
    ustr_free(&s);
    return ok;
}

static int lower_is(const char *in, const char *want)
{
    ustr s;
    char *u;
    int ok;
    ustr_init(&s);
    ustr_set_utf8(&s, in, strlen(in));
    ustr_lower(&s);
    u = ustr_utf8(&s);
    ok = u && strcmp(u, want) == 0;
    free(u);
    ustr_free(&s);
    return ok;
}

static void test_ustr(void)
{
    ustr s;
    char *u;
    CHECK(fold_is("Z\xC3\xBCrich", "zurich"));
    CHECK(fold_is("  K\xC3\xB8""benhavn\t \xC2\xA0Ost ", "k\xC3\xB8""benhavn ost"));
    CHECK(fold_is("\xEC\x84\x9C\xEC\x9A\xB8", "\xE1\x84\x89\xE1\x85\xA5\xE1\x84\x8B\xE1\x85\xAE\xE1\x86\xAF")); /* 서울 */
    CHECK(fold_is("\xEF\xAC\x81" "re", "fire")); /* the fi ligature */
    CHECK(lower_is("\xCE\x9F\xCE\x94\xCE\x9F\xCE\xA3", "\xCE\xBF\xCE\xB4\xCE\xBF\xCF\x82")); /* ΟΔΟΣ -> οδος (final) */
    CHECK(lower_is("\xCE\xA3\xCE\x91", "\xCF\x83\xCE\xB1")); /* ΣΑ -> σα */
    CHECK(lower_is("\xC4\xB0", "i\xCC\x87")); /* İ -> i + dot */
    CHECK(u_isspace(0x3000) && !u_isspace('x') && u_isalnum(0x4E2D) && u_isdigit(0xB2));
    ustr_init(&s);
    ustr_set_utf8(&s, "a\xFF" "b", 3);
    CHECK(s.n == 3 && s.c[1] == 0xFFFD);
    u = ustr_utf8(&s);
    CHECK(u && strcmp(u, "a\xEF\xBF\xBD" "b") == 0);
    free(u);
    ustr_free(&s);
}

/* A zip with one deflated file, built here with zlib. */
static void test_zip(void)
{
    static const char text[] = "Swansea\t51.62\t-3.94\n";
    unsigned char comp[256], zip[512];
    uLongf clen = sizeof(comp);
    size_t n = 0, len;
    char err[100], *out;
    z_stream z;
    unsigned long crc = crc32(0, (const Bytef *)text, sizeof(text) - 1);
    memset(&z, 0, sizeof(z));
    deflateInit2(&z, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    z.next_in = (Bytef *)text;
    z.avail_in = sizeof(text) - 1;
    z.next_out = comp;
    z.avail_out = (uInt)clen;
    deflate(&z, Z_FINISH);
    clen = z.total_out;
    deflateEnd(&z);
#define P16(v) (zip[n++] = (unsigned char)((v) & 0xFF), zip[n++] = (unsigned char)(((v) >> 8) & 0xFF))
#define P32(v) (P16((v) & 0xFFFF), P16(((unsigned long)(v) >> 16) & 0xFFFF))
    /* local header */
    P32(0x04034b50UL); P16(20); P16(0); P16(8); P16(0); P16(0); P32(crc); P32(clen);
    P32(sizeof(text) - 1); P16(9); P16(0);
    memcpy(zip + n, "towns.txt", 9);
    n += 9;
    memcpy(zip + n, comp, clen);
    n += clen;
    {
        size_t cd = n;
        P32(0x02014b50UL); P16(20); P16(20); P16(0); P16(8); P16(0); P16(0); P32(crc);
        P32(clen); P32(sizeof(text) - 1); P16(9); P16(0); P16(0); P16(0); P16(0); P32(0);
        P32(0);
        memcpy(zip + n, "towns.txt", 9);
        n += 9;
        {
            size_t cdlen = n - cd;
            P32(0x06054b50UL); P16(0); P16(0); P16(1); P16(1); P32(cdlen); P32(cd); P16(0);
        }
    }
    out = zip_extract(zip, n, ".TXT", &len, err, sizeof(err));
    CHECK(out && len == sizeof(text) - 1 && memcmp(out, text, len) == 0);
    free(out);
    CHECK(zip_extract(zip, n, ".csv", &len, err, sizeof(err)) == NULL);
    CHECK(zip_extract((const unsigned char *)"PK", 2, ".txt", &len, err, sizeof(err)) == NULL);
}

/* Converts a JSON list with the options; returns the result. */
static rb_result convert(const char *json, const rb_options *o)
{
    char err[100];
    json_value *v = json_parse(json, strlen(json), err, sizeof(err));
    const json_value *st[32];
    rb_result r;
    size_t i;
    memset(&r, 0, sizeof(r));
    if (!v) {
        fprintf(stderr, "test JSON: %s\n", err);
        CHECK(0);
        return r;
    }
    for (i = 0; i < v->n && i < 32; i++)
        st[i] = &v->items[i];
    CHECK(rb_convert(st, v->n, o, NULL, &r) == 0);
    json_free(v);
    return r;
}

static long skipped(const rb_result *r, const char *why)
{
    size_t i;
    for (i = 0; i < r->nskipped; i++)
        if (!strcmp(r->skipped[i].why, why))
            return r->skipped[i].count;
    return 0;
}

#define ST(extra) \
    "{\"stationuuid\":\"u-x\",\"name\":\"Sample FM\",\"url_resolved\":\"http://example.com/stream\"," \
    "\"country\":\"Germany\",\"countrycode\":\"DE\",\"geo_lat\":52.52,\"geo_long\":13.4," \
    "\"codec\":\"MP3\",\"bitrate\":128,\"tags\":\"jazz,soul,funk\",\"hls\":0,\"lastcheckok\":1" extra "}"

static void test_convert(void)
{
    rb_options o;
    rb_result r;
    char *h, *rep;
    rb_options_default(&o);
    o.min_bitrate = 64;
    r = convert("[" ST(",\"name\":\"Good\",\"stationuuid\":\"u-Good\"")
                "," ST(",\"stationuuid\":\"u-Hls\",\"hls\":1")
                "," ST(",\"stationuuid\":\"u-Dead\",\"lastcheckok\":0")
                "," ST(",\"stationuuid\":\"u-NoGeo\",\"geo_lat\":null")
                "," ST(",\"stationuuid\":\"u-Null\",\"geo_lat\":0,\"geo_long\":0")
                "," ST(",\"stationuuid\":\"u-Aac\",\"codec\":\"AAC\"")
                "," ST(",\"stationuuid\":\"u-Dup\"")
                "," ST(",\"stationuuid\":\"u-Ftp\",\"url_resolved\":\"ftp://x\"")
                "," ST(",\"stationuuid\":\"u-low\",\"url_resolved\":\"http://e/low\",\"bitrate\":32")
                "," ST(",\"stationuuid\":\"u-rg\",\"url_resolved\":\"http://e/s?listening-from-radio-garden=1\"")
                "]", &o);
    CHECK(r.n == 1);
    if (r.n)
        CHECK(!strcmp(r.lines[0].line, "Good | http://example.com/stream | Germany |  | 52.5200 | "
                                        "13.4000 | jazz, soul | MP3 128k rb:u-Good"));
    CHECK(skipped(&r, "HLS stream") == 1);
    CHECK(skipped(&r, "last check failed") == 1);
    CHECK(skipped(&r, "no usable coordinates") == 2);
    CHECK(skipped(&r, "codec AAC not selected") == 1);
    CHECK(skipped(&r, "duplicate stream URL") == 1);
    CHECK(skipped(&r, "no usable stream URL") == 1);
    CHECK(skipped(&r, "bitrate below 64k or unknown") == 1);
    CHECK(skipped(&r, "URL refers to Radio Garden") == 1);
    rep = rb_report(&r, &o, 0, "x.txt", 2048);
    CHECK(rep && strstr(rep, "Wrote 1 stations to x.txt (2 KB).") &&
          strstr(rep, "Per codec: MP3 1"));
    free(rep);
    rb_result_free(&r);

    /* Region fallback; HLS and FLAC; homepages; names and countries. */
    rb_options_default(&o);
    o.codecs = RB_ALL;
    o.hls = 1;
    r = convert("[" ST(",\"stationuuid\":\"a\",\"url_resolved\":\"http://e/1\",\"state\":\"Saskatchewan\"")
                "," ST(",\"stationuuid\":\"b\",\"url_resolved\":\"http://e/2\",\"state\":\"R-04\"")
                "," ST(",\"stationuuid\":\"c\",\"url_resolved\":\"http://e/3.m3u8\",\"hls\":1,\"codec\":\"UNKNOWN\"")
                "," ST(",\"stationuuid\":\"d\",\"url_resolved\":\"http://e/4\",\"codec\":\"flac\",\"bitrate\":0,"
                       "\"homepage\":\" https://example.org/a b \"")
                "," ST(",\"stationuuid\":\"e\",\"url_resolved\":\"http://e/5/\",\"homepage\":\"http://e/5\"")
                "," ST(",\"stationuuid\":\"f\",\"url_resolved\":\"http://e/6\",\"name\":\"#  The | Best\\tTest FM\"")
                "," ST(",\"stationuuid\":\"g\",\"url_resolved\":\"http://e/7\",\"country\":\"The United Kingdom Of "
                       "Great Britain And Northern Ireland\",\"tags\":\"#Rock, rock ,POP,jazz\"")
                "," ST(",\"stationuuid\":\"h\",\"url_resolved\":\"http://e/8\",\"country\":\"Trinidad And Tobago\"")
                "," ST(",\"stationuuid\":\"i\",\"url_resolved\":\"http://e/9\",\"homepage\":\"https://radio.garden/x\"")
                "]", &o);
    CHECK(r.n == 7);
    if (r.n == 7) {
        CHECK(strstr(r.lines[0].line, "| Germany | Saskatchewan |") != NULL &&
              !strcmp(r.lines[0].place_from, "region"));
        CHECK(strstr(r.lines[1].line, "| Germany |  |") != NULL);
        CHECK(strstr(r.lines[2].line, "| HLS 128k rb:c") != NULL && !strcmp(r.lines[2].codec, "HLS"));
        CHECK(strstr(r.lines[3].line, "FLAC ?k rb:d web:https://example.org/a%20b") != NULL);
        CHECK(strstr(r.lines[4].line, "web:") == NULL);
        CHECK(strstr(r.lines[5].line, "| United Kingdom |") != NULL &&
              strstr(r.lines[5].line, "| Rock, POP |") != NULL);
        CHECK(strstr(r.lines[6].line, "| Trinidad and Tobago |") != NULL);
    }
    CHECK(skipped(&r, "test broadcast") == 1);
    CHECK(skipped(&r, "URL refers to Radio Garden") == 1);
    rb_result_free(&r);

    /* Long names are cut at a word, with an ellipsis. */
    rb_options_default(&o);
    r = convert("[" ST(",\"name\":\"Radio Alpha Bravo Charlie Delta Echo Foxtrot Golf Hotel India "
                       "Juliett Kilo Lima Mike November Oscar Papa Quebec\"") "]", &o);
    CHECK(r.n == 1);
    if (r.n)
        CHECK(strstr(r.lines[0].line, "November\xE2\x80\xA6 | http") != NULL);
    rb_result_free(&r);

    CHECK(rb_parse_codecs(&o, "mp3, AAC+") == 0 && o.codecs == (RB_MP3 | RB_AACP));
    CHECK(rb_parse_codecs(&o, "all") == 0 && o.codecs == RB_ALL);
    CHECK(rb_parse_codecs(&o, "wma") != 0);
    CHECK(rb_parse_countries(&o, "gb, ie") == 0 && o.ncountries == 2 &&
          !strcmp(o.countries[1], "IE"));
    CHECK(rb_parse_countries(&o, "GBR") != 0);

    h = rb_header(&o, 1, 5, 0, 0, "test", "for Linux 9.9.9");
    CHECK(h && strstr(h, "shipped with DX Garden for Linux 9.9.9") &&
          strstr(h, "generated by test on 1970-01-01 00:00 UTC") &&
          strstr(h, "Positions outside"));
    free(h);
    {
        char q[600];
        rb_options_default(&o);
        o.min_bitrate = 96;
        snprintf(o.tag, sizeof(o.tag), "hip hop");
        rb_query(&o, "GB", 500, q, sizeof(q));
        CHECK(!strcmp(q, "has_geo_info=true&hidebroken=true&order=clickcount&reverse=true&limit=500"
                         "&codec=MP3&codecExact=true&bitrateMin=96&tag=hip+hop&countrycode=GB"
                         "&offset=500"));
    }
}

static void test_geo(void)
{
    /* Three towns: two in Wales, one in England; Cardiff is bigger. */
    static const char towns[] =
        "1\tCardiff\tCardiff\t\t51.48\t-3.18\tP\tPPLA\tGB\t\tWLS\t\t\t\t362756\t\t\t\t\n"
        "2\tPenarth\tPenarth\t\t51.43\t-3.17\tP\tPPL\tGB\t\tWLS\t\t\t\t27226\t\t\t\t\n"
        "3\tBristol\tBristol\t\t51.45\t-2.59\tP\tPPLA2\tGB\t\tENG\t\t\t\t617280\t\t\t\t\n"
        "4\tLondon\tLondon\t\t51.51\t-0.13\tP\tPPLC\tGB\t\tENG\t\t\t\t8961989\t\t\t\t\n"
        "bad line\n";
    rb_geo *g = rb_geo_new();
    rb_options o;
    rb_result r;
    char err[100];
    json_value *v;
    const json_value *st[4];
    size_t i;
    static const char json[] =
        "[{\"stationuuid\":\"1\",\"name\":\"Penarth Radio\",\"url_resolved\":\"http://e/1\","
        "\"country\":\"The United Kingdom Of Great Britain And Northern Ireland\","
        "\"countrycode\":\"GB\",\"geo_lat\":51.44,\"geo_long\":-3.175,\"codec\":\"MP3\","
        "\"bitrate\":128,\"hls\":0,\"lastcheckok\":1},"
        "{\"stationuuid\":\"2\",\"name\":\"Swapped\",\"url_resolved\":\"http://e/2\","
        "\"country\":\"United Kingdom\",\"countrycode\":\"GB\",\"geo_lat\":-2.59,"
        "\"geo_long\":51.45,\"codec\":\"MP3\",\"bitrate\":128,\"hls\":0,\"lastcheckok\":1},"
        "{\"stationuuid\":\"3\",\"name\":\"Lost\",\"url_resolved\":\"http://e/3\","
        "\"country\":\"United Kingdom\",\"countrycode\":\"GB\",\"geo_lat\":10,"
        "\"geo_long\":10,\"codec\":\"MP3\",\"bitrate\":128,\"hls\":0,\"lastcheckok\":1}]";
    CHECK(g && rb_geo_load_cities(g, towns, sizeof(towns) - 1) == 4);
    v = json_parse(json, sizeof(json) - 1, err, sizeof(err));
    CHECK(v != NULL);
    if (!g || !v)
        return;
    for (i = 0; i < v->n; i++)
        st[i] = &v->items[i];
    rb_options_default(&o);
    CHECK(rb_convert(st, v->n, &o, g, &r) == 0);
    CHECK(r.n == 3);
    if (r.n == 3) {
        /* Cardiff is within 20 km and bigger, so the station joins it;
         * labelled by nation. */
        CHECK(strstr(r.lines[0].line, "| Wales | Cardiff | 51.4400 | -3.1750 |") != NULL);
        CHECK(strstr(r.lines[0].line, "(country: Wales, where the position is)") != NULL);
        CHECK(!strcmp(r.lines[1].geo, "swapped") &&
              strstr(r.lines[1].line, "| England | Bristol | 51.4500 | -2.5900 |") != NULL);
        CHECK(!strcmp(r.lines[2].geo, "capital") &&
              strstr(r.lines[2].line, "| England | London |") != NULL);
    }
    rb_result_free(&r);
    json_free(v);
    rb_geo_free(g);
}

/* A station whose position wins over its country, moved to a country no
 * other station is in (a download for some countries only): the borders
 * file names it. */
static void test_country_from_borders(void)
{
    static const char towns[] =
        "1\tBerlin\tBerlin\t\t52.52437\t13.41053\tP\tPPLC\tDE\t\t\t\t\t\t3426354\t\t\t\t\n"
        "2\tWashington\tWashington\t\t38.89511\t-77.03637\tP\tPPLC\tUS\t\t\t\t\t\t689545\t\t\t\t\n";
    static const char borders[] =
        "{\"features\":["
        "{\"properties\":{\"ISO_A2\":\"-99\",\"ISO_A2_EH\":\"DE\",\"ADMIN\":\"Germany\"},"
        "\"geometry\":{\"type\":\"Polygon\",\"coordinates\":"
        "[[[12,51],[15,51],[15,54],[12,54],[12,51]]]}},"
        "{\"properties\":{\"ISO_A2\":\"US\",\"ADMIN\":\"United States of America\"},"
        "\"geometry\":{\"type\":\"MultiPolygon\",\"coordinates\":"
        "[[[[-160,60],[-150,60],[-150,65],[-160,65],[-160,60]]]]}}]}";
    static const char json[] =
        "[{\"stationuuid\":\"1\",\"name\":\"EWTN\",\"url_resolved\":\"http://e/1\","
        "\"country\":\"Germany\",\"countrycode\":\"DE\",\"geo_lat\":38.90,"
        "\"geo_long\":-77.04,\"codec\":\"MP3\",\"bitrate\":128,\"hls\":0,\"lastcheckok\":1}]";
    rb_geo *g = rb_geo_new();
    rb_options o;
    rb_result r;
    char err[100];
    json_value *v;
    const json_value *st[1];
    CHECK(g && rb_geo_load_cities(g, towns, sizeof(towns) - 1) == 2);
    CHECK(g && rb_geo_load_borders(g, borders, sizeof(borders) - 1, err, sizeof(err)) == 2);
    v = json_parse(json, sizeof(json) - 1, err, sizeof(err));
    CHECK(v != NULL);
    if (!g || !v)
        return;
    st[0] = &v->items[0];
    rb_options_default(&o);
    CHECK(rb_convert(st, 1, &o, g, &r) == 0);
    CHECK(r.n == 1);
    if (r.n == 1) {
        CHECK(strstr(r.lines[0].line, "| United States | Washington |") != NULL);
        CHECK(strstr(r.lines[0].line, "(country corrected to match the position)") != NULL);
    }
    rb_result_free(&r);
    json_free(v);
    rb_geo_free(g);
}

/* Stations without coordinates: another stream of the station, the region,
 * a town or region in the name, else the country's Country-wide place. The
 * same case as the script's WithoutCoords.test_convert. */
static void test_without_coords(void)
{
    static const char towns[] =
        "1\tBerlin\tBerlin\t\t52.52437\t13.41053\tP\tPPLC\tDE\t\t16\t\t\t\t3426354\t\t\t\t\n"
        "2\tMunich\tMunich\t\t48.13743\t11.57549\tP\tPPLA\tDE\t\t02\t\t\t\t1260391\t\t\t\t\n"
        "3\tAugsburg\tAugsburg\t\t48.37154\t10.89851\tP\tPPLA2\tDE\t\t02\t\t\t\t259196\t\t\t\t\n"
        "4\tPotsdam\tPotsdam\t\t52.39886\t13.06566\tP\tPPLA\tDE\t\t11\t\t\t\t159456\t\t\t\t\n"
        "5\tSpringfield\tSpringfield\t\t39.80172\t-89.64371\tP\tPPLA\tUS\t\tIL\t\t\t\t116250"
        "\t\t\t\t\n"
        "6\tJoliet\tJoliet\t\t41.52519\t-88.0834\tP\tPPL\tUS\t\tIL\t\t\t\t147433\t\t\t\t\n";
    static const char admin1[] =
        "DE.16\tLand Berlin\tLand Berlin\t1\nDE.02\tBayern\tBavaria\t2\n"
        "DE.11\tBrandenburg\tBrandenburg\t3\nUS.IL\tIllinois\tIllinois\t4\n"
        "DE.99\tNowhere\tNowhere\t5\n";
    static const char borders[] =
        "{\"features\":["
        "{\"properties\":{\"ISO_A2\":\"DE\",\"ADMIN\":\"Germany\",\"LABEL_X\":10.4,"
        "\"LABEL_Y\":51.1},\"geometry\":{\"type\":\"Polygon\",\"coordinates\":"
        "[[[6,47],[15,47],[15,55],[6,55],[6,47]]]}},"
        "{\"properties\":{\"ISO_A2\":\"US\",\"ADMIN\":\"United States of America\","
        "\"LABEL_X\":-97.5,\"LABEL_Y\":39.5},\"geometry\":{\"type\":\"Polygon\","
        "\"coordinates\":[[[-125,25],[-67,25],[-67,49],[-125,49],[-125,25]]]}}]}";
#define NOGEO(name, url, extra)                                                              \
    "{\"stationuuid\":\"u-" url "\",\"name\":\"" name "\",\"url_resolved\":\"" url "\","     \
    "\"country\":\"Germany\",\"countrycode\":\"DE\",\"geo_lat\":null,\"geo_long\":null,"     \
    "\"codec\":\"MP3\",\"bitrate\":128,\"tags\":\"\",\"hls\":0,\"lastcheckok\":1" extra "}"
    static const char json[] =
        "[{\"stationuuid\":\"s1\",\"name\":\"Antenne Bayern\",\"url_resolved\":\"http://a.de/1\","
        "\"homepage\":\"http://www.antenne.de\",\"country\":\"Germany\",\"countrycode\":\"DE\","
        "\"geo_lat\":48.37,\"geo_long\":10.9,\"codec\":\"MP3\",\"bitrate\":128,\"hls\":0,"
        "\"lastcheckok\":1},"
        NOGEO("Antenne Bayern", "http://stream.antenne.de/2", ",\"homepage\":\"https://antenne.de/\"") ","
        NOGEO("Antenne Bayern", "http://other.example/3", ",\"homepage\":\"http://other.example\"") ","
        NOGEO("Radio Mix", "http://e/4", ",\"state\":\"Bavaria\"") ","
        NOGEO("Radio Potsdam", "http://e/5", "") ","
        NOGEO("Deutschlandfunk", "http://e/6", ",\"state\":\"Germany\"") ","
        "{\"stationuuid\":\"u7\",\"name\":\"Nowhere\",\"url_resolved\":\"http://e/7\","
        "\"country\":\"\",\"countrycode\":\"\",\"codec\":\"MP3\",\"bitrate\":128,\"hls\":0,"
        "\"lastcheckok\":1}]";
#undef NOGEO
    static const char *const kinds[] = { "ok", "sister", "name_region", "admin1", "name_town",
                                         "country" };
    rb_geo *g = rb_geo_new();
    rb_options o;
    rb_result r;
    char err[100], q[300];
    json_value *v;
    const json_value *st[8];
    size_t i;
    CHECK(g && rb_geo_load_cities(g, towns, sizeof(towns) - 1) == 6);
    CHECK(g && rb_geo_load_borders(g, borders, sizeof(borders) - 1, err, sizeof(err)) == 2);
    CHECK(g && rb_geo_load_admin1(g, admin1, sizeof(admin1) - 1) == 4);
    v = json_parse(json, sizeof(json) - 1, err, sizeof(err));
    CHECK(v != NULL && v->n == 7);
    if (!g || !v || v->n != 7)
        return;
    for (i = 0; i < v->n; i++)
        st[i] = &v->items[i];
    rb_options_default(&o);
    CHECK(rb_convert(st, v->n, &o, g, &r) == 0);
    CHECK(r.n == 1 && r.nskipped == 1 && r.skipped[0].count == 6 &&
          !strcmp(r.skipped[0].why, "no usable coordinates"));
    rb_result_free(&r);
    o.without_coords = 1;
    CHECK(rb_convert(st, v->n, &o, g, &r) == 0);
    CHECK(r.n == 6);
    if (r.n == 6) {
        for (i = 0; i < 6; i++)
            CHECK(!strcmp(r.lines[i].geo, kinds[i]));
        CHECK(strstr(r.lines[1].line, "| Augsburg | 48.3700 | 10.9000 |") != NULL);
        CHECK(strstr(r.lines[3].line, "| Germany | Munich |") != NULL);
        CHECK(strstr(r.lines[4].line, "| Germany | Potsdam |") != NULL);
        CHECK(strstr(r.lines[5].line, "| Germany | Country-wide | 51.1000 | 10.4000 |") != NULL);
        CHECK(strstr(r.lines[5].line, "Country-wide place") != NULL);
        CHECK(!strcmp(r.lines[5].place_from, "country"));
    }
    CHECK(r.nskipped == 1 &&
          !strcmp(r.skipped[0].why, "no coordinates, and no country to place it in"));
    rb_query(&o, NULL, 0, q, sizeof(q));
    CHECK(strstr(q, "has_geo_info") == NULL);
    rb_result_free(&r);
    json_free(v);
    rb_geo_free(g);
}

void run_rb_tests(void)
{
    test_json();
    test_ustr();
    test_zip();
    test_convert();
    test_geo();
    test_country_from_borders();
    test_without_coords();
}
