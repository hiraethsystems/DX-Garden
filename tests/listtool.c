/*
 * dxgarden_listtool: core/rb_build.c from the command line, with the same
 * options as tools/rb_to_stations.py --from-raw, so the two can be
 * compared on the same snapshot (tests/compare_listtool.sh).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include "json.h"
#include "rb_build.h"
#include "text.h"
#include "zip_read.h"

static char *load(const char *path, size_t *len)
{
    read_status st;
    char *buf = read_file(path, 512u * 1024 * 1024, len, &st);
    if (!buf)
        fprintf(stderr, "cannot read %s\n", path);
    return buf;
}

static int ends_ci(const char *s, const char *suffix)
{
    return text_ends_with_ci(s, suffix);
}

int main(int argc, char **argv)
{
    rb_options o;
    const char *raws[8], *admin1 = NULL, *geonames = NULL, *borders = NULL, *output = "stations.txt";
    const char *bundle = NULL;
    int no_city = 0, i, nraw = 0;
    size_t len, k;
    char *buf, err[200];
    json_value *docs[8];
    const json_value **st = NULL;
    size_t nst = 0;
    rb_geo *geo = NULL;
    rb_result r;
    char *head, *report;
    FILE *f;
    size_t bytes;
    struct stat sb;

    rb_options_default(&o);
    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--hls")) {
            o.hls = 1;
            continue;
        }
        if (!strcmp(a, "--no-city")) {
            no_city = 1;
            continue;
        }
        if (!strcmp(a, "--without-coords")) {
            o.without_coords = 1;
            continue;
        }
        if (!v) {
            fprintf(stderr, "%s needs a value\n", a);
            return 2;
        }
        i++;
        if (!strcmp(a, "--from-raw")) {
            if (nraw < 8)
                raws[nraw++] = v;
        } else if (!strcmp(a, "--admin1"))
            admin1 = v;
        else if (!strcmp(a, "--geonames"))
            geonames = v;
        else if (!strcmp(a, "--borders"))
            borders = v;
        else if (!strcmp(a, "-o") || !strcmp(a, "--output"))
            output = v;
        else if (!strcmp(a, "--bundle"))
            bundle = v;
        else if (!strcmp(a, "--max"))
            o.max = atoi(v);
        else if (!strcmp(a, "--min-bitrate"))
            o.min_bitrate = atoi(v);
        else if (!strcmp(a, "--city-km"))
            o.city_km = atof(v);
        else if (!strcmp(a, "--tag"))
            snprintf(o.tag, sizeof(o.tag), "%s", v);
        else if (!strcmp(a, "--codecs")) {
            if (rb_parse_codecs(&o, v) != 0) {
                fprintf(stderr, "unknown codec in '%s'\n", v);
                return 2;
            }
        } else if (!strcmp(a, "--countries")) {
            if (rb_parse_countries(&o, v) != 0) {
                fprintf(stderr, "bad country list '%s'\n", v);
                return 2;
            }
        } else if (!strcmp(a, "--max-mb")) {
            /* accepted for compatibility */
        } else {
            fprintf(stderr, "unknown option %s\n", a);
            return 2;
        }
    }
    if (!nraw) {
        fprintf(stderr, "usage: dxgarden_listtool --from-raw FILE... [--geonames FILE] [--borders "
                        "FILE] [--admin1 FILE] [--without-coords] [--codecs LIST] [--hls] [--max N] [-o FILE]\n");
        return 2;
    }

    if (geonames && !no_city) {
        char *text;
        size_t tl;
        geo = rb_geo_new();
        buf = load(geonames, &len);
        if (!buf || !geo)
            return 1;
        if (ends_ci(geonames, ".zip")) {
            text = zip_extract((const unsigned char *)buf, len, ".txt", &tl, err, sizeof(err));
            free(buf);
            if (!text) {
                fprintf(stderr, "%s: %s\n", geonames, err);
                return 1;
            }
        } else {
            text = buf;
            tl = len;
        }
        fprintf(stderr, "GeoNames: %ld cities loaded from %s\n",
                rb_geo_load_cities(geo, text, tl), geonames);
        free(text);
        if (borders) {
            long ns;
            buf = load(borders, &len);
            if (!buf)
                return 1;
            ns = rb_geo_load_borders(geo, buf, len, err, sizeof(err));
            free(buf);
            if (ns < 0) {
                fprintf(stderr, "%s: %s\n", borders, err);
                return 1;
            }
            fprintf(stderr, "Borders: %ld shapes loaded from %s\n", ns, borders);
        }
        if (admin1) {
            buf = load(admin1, &len);
            if (!buf)
                return 1;
            fprintf(stderr, "Regions: %ld loaded from %s\n", rb_geo_load_admin1(geo, buf, len),
                    admin1);
            free(buf);
        }
    }

    for (i = 0; i < nraw; i++) {
        json_value *doc;
        buf = load(raws[i], &len);
        if (!buf)
            return 1;
        doc = json_parse(buf, len, err, sizeof(err));
        free(buf);
        if (!doc || doc->type != JSON_ARRAY) {
            fprintf(stderr, "%s: %s\n", raws[i], doc ? "not a list" : err);
            return 1;
        }
        fprintf(stderr, "%lu stations read from %s\n", (unsigned long)doc->n, raws[i]);
        docs[i] = doc;
        st = (const json_value **)realloc(st, (nst + doc->n + 1) * sizeof(*st));
        if (!st)
            return 1;
        for (k = 0; k < doc->n; k++)
            st[nst++] = &doc->items[k];
    }
    if (rb_convert(st, nst, &o, geo, &r) != 0) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    stat(raws[0], &sb);
    head = rb_header(&o, geo != NULL, r.n, time(NULL), sb.st_mtime, "dxgarden_listtool", bundle);
    f = fopen(output, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", output);
        return 1;
    }
    fputs(head, f);
    bytes = strlen(head);
    for (k = 0; k < r.n; k++) {
        fputs(r.lines[k].line, f);
        fputc('\n', f);
        bytes += strlen(r.lines[k].line) + 1;
    }
    fclose(f);
    report = rb_report(&r, &o, geo != NULL, output, bytes);
    fputs(report, stdout);
    free(report);
    free(head);
    rb_result_free(&r);
    free(st);
    for (i = 0; i < nraw; i++)
        json_free(docs[i]);
    rb_geo_free(geo);
    return 0;
}
