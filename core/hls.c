#include "hls.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"

/* The value of attribute `name` in an attribute list (KEY=VALUE,
 * KEY="VALUE",...), without quotes, into out. Returns 1 if found. */
static int attr(const char *list, size_t len, const char *name, char *out, size_t n)
{
    size_t k = strlen(name), i = 0;
    while (i < len) {
        size_t j;
        int quoted;
        /* At the start of a KEY. */
        while (i < len && (list[i] == ',' || list[i] == ' '))
            i++;
        j = i;
        while (j < len && list[j] != '=' && list[j] != ',')
            j++;
        if (j >= len || list[j] != '=') {
            i = j;
            continue;
        }
        {
            int match = j - i == k && strncmp(list + i, name, k) == 0;
            size_t v = j + 1, e;
            quoted = v < len && list[v] == '"';
            if (quoted) {
                v++;
                for (e = v; e < len && list[e] != '"'; e++)
                    ;
            } else {
                for (e = v; e < len && list[e] != ','; e++)
                    ;
            }
            if (match) {
                size_t m = e - v < n - 1 ? e - v : n - 1;
                memcpy(out, list + v, m);
                out[m] = '\0';
                return 1;
            }
            i = e + (quoted ? 1 : 0);
        }
    }
    return 0;
}

static int starts(const char *line, size_t len, const char *tag)
{
    size_t k = strlen(tag);
    return len >= k && strncmp(line, tag, k) == 0;
}

/* Audio-only: every codec in the list is an audio one. */
static int codecs_audio_only(const char *codecs)
{
    static const char *const AUDIO[] = { "mp4a", "mp3", "ac-3", "ec-3", "opus", "flac", "fLaC",
                                         ".mp3", NULL };
    const char *p = codecs;
    while (*p) {
        size_t n = strcspn(p, ",");
        int ok = 0, i;
        while (*p == ' ')
            p++, n--;
        for (i = 0; AUDIO[i]; i++)
            if (strncmp(p, AUDIO[i], strlen(AUDIO[i])) == 0)
                ok = 1;
        if (!ok)
            return 0;
        p += n;
        if (*p == ',')
            p++;
    }
    return 1;
}

static void *grow(void *arr, int count, size_t size)
{
    /* Room for one more, doubling in steps of 16. */
    if (count % 16 == 0)
        return realloc(arr, (size_t)(count + 16) * size);
    return arr;
}

int hls_parse(const char *text, size_t len, hls_playlist *pl)
{
    line_iter it;
    const char *line;
    size_t n;
    int first = 1, pending_variant = 0, default_audio = 0;
    long pending_bw = 0;
    int pending_audio_only = 0, pending_has_codecs = 0;
    double pending_dur = 0;
    long long seq = 0;
    char *map = NULL;
    char val[1024];

    memset(pl, 0, sizeof(*pl));
    line_iter_init(&it, text, len);
    while (line_iter_next(&it, &line, &n)) {
        /* Trim (line_iter strips the line end; tolerate spaces and a BOM). */
        if (first && n >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF)
            line += 3, n -= 3;
        while (n && isspace((unsigned char)line[0]))
            line++, n--;
        while (n && isspace((unsigned char)line[n - 1]))
            n--;
        if (!n)
            continue;
        if (first) {
            first = 0;
            if (!starts(line, n, "#EXTM3U"))
                return -1;
            continue;
        }
        if (line[0] == '#') {
            if (starts(line, n, "#EXT-X-STREAM-INF:")) {
                const char *a = line + 18;
                size_t an = n - 18;
                pl->is_master = 1;
                pending_variant = 1;
                pending_bw = attr(a, an, "BANDWIDTH", val, sizeof(val)) ? atol(val) : 0;
                pending_has_codecs = attr(a, an, "CODECS", val, sizeof(val));
                pending_audio_only = pending_has_codecs && codecs_audio_only(val);
            } else if (starts(line, n, "#EXT-X-MEDIA:")) {
                const char *a = line + 13;
                size_t an = n - 13;
                char type[16], def[8];
                pl->is_master = 1;
                if (attr(a, an, "TYPE", type, sizeof(type)) && strcmp(type, "AUDIO") == 0 &&
                    attr(a, an, "URI", val, sizeof(val))) {
                    int is_def = attr(a, an, "DEFAULT", def, sizeof(def)) && strcmp(def, "YES") == 0;
                    if (!pl->audio_uri || (is_def && !default_audio)) {
                        free(pl->audio_uri);
                        pl->audio_uri = text_dup(val);
                        default_audio = is_def;
                    }
                }
            } else if (starts(line, n, "#EXTINF:")) {
                pending_dur = atof(line + 8);
            } else if (starts(line, n, "#EXT-X-TARGETDURATION:")) {
                pl->target_duration = atof(line + 22);
            } else if (starts(line, n, "#EXT-X-MEDIA-SEQUENCE:")) {
                seq = atoll(line + 22);
            } else if (starts(line, n, "#EXT-X-ENDLIST")) {
                pl->endlist = 1;
            } else if (starts(line, n, "#EXT-X-KEY:")) {
                if (attr(line + 11, n - 11, "METHOD", val, sizeof(val)) && strcmp(val, "NONE") != 0)
                    pl->encrypted = 1;
            } else if (starts(line, n, "#EXT-X-MAP:")) {
                free(map);
                map = attr(line + 11, n - 11, "URI", val, sizeof(val)) ? text_dup(val) : NULL;
            }
            continue;
        }
        /* A URI line. */
        if (pending_variant) {
            hls_variant *v;
            void *g = grow(pl->variants, pl->nvariants, sizeof(*pl->variants));
            if (!g)
                goto oom;
            pl->variants = g;
            v = &pl->variants[pl->nvariants];
            v->uri = text_ndup(line, n);
            if (!v->uri)
                goto oom;
            v->bandwidth = pending_bw;
            v->audio_only = pending_audio_only;
            v->has_codecs = pending_has_codecs;
            pl->nvariants++;
            pending_variant = 0;
        } else if (!pl->is_master) {
            hls_segment *s;
            void *g = grow(pl->segs, pl->nsegs, sizeof(*pl->segs));
            if (!g)
                goto oom;
            pl->segs = g;
            s = &pl->segs[pl->nsegs];
            s->uri = text_ndup(line, n);
            s->map = map ? text_dup(map) : NULL;
            if (!s->uri || (map && !s->map)) {
                free(s->uri);
                free(s->map);
                goto oom;
            }
            s->duration = pending_dur;
            s->seq = seq++;
            pl->nsegs++;
            pending_dur = 0;
        }
    }
    free(map);
    return 0;
oom:
    free(map);
    return -1;
}

void hls_free(hls_playlist *pl)
{
    int i;
    for (i = 0; i < pl->nvariants; i++)
        free(pl->variants[i].uri);
    for (i = 0; i < pl->nsegs; i++) {
        free(pl->segs[i].uri);
        free(pl->segs[i].map);
    }
    free(pl->variants);
    free(pl->segs);
    free(pl->audio_uri);
    memset(pl, 0, sizeof(*pl));
}

int hls_pick_variant(const hls_playlist *pl, long max_bps)
{
    int i, best = -1, above = -1, lowest = -1;
    for (i = 0; i < pl->nvariants; i++) {
        const hls_variant *v = &pl->variants[i];
        if (lowest < 0 || v->bandwidth < pl->variants[lowest].bandwidth)
            lowest = i;
        /* No CODECS: most likely audio on an audio-only service. */
        if (!v->audio_only && v->has_codecs)
            continue;
        if (v->bandwidth <= max_bps) {
            if (best < 0 || v->bandwidth > pl->variants[best].bandwidth)
                best = i;
        } else if (above < 0 || v->bandwidth < pl->variants[above].bandwidth) {
            above = i;
        }
    }
    return best >= 0 ? best : above >= 0 ? above : lowest;
}

/* ---- URL resolution ------------------------------------------------------ */

/* Removes "." and ".." segments from a path, in place. */
static void remove_dots(char *path)
{
    char *out = path, *in = path;
    while (*in) {
        if (in[0] == '.' && (in[1] == '/' || in[1] == '\0')) {
            in += in[1] ? 2 : 1;
        } else if (in[0] == '.' && in[1] == '.' && (in[2] == '/' || in[2] == '\0')) {
            in += in[2] ? 3 : 2;
            /* Back up over the last output segment. */
            if (out > path) {
                out--;
                while (out > path && out[-1] != '/')
                    out--;
            }
        } else {
            while (*in && *in != '/')
                *out++ = *in++;
            if (*in == '/')
                *out++ = *in++;
        }
    }
    *out = '\0';
}

int url_resolve(const char *base, const char *ref, char *out, size_t n)
{
    const char *scheme_end, *auth_end, *path_end;
    char path[2048];
    size_t pre;
    int r;

    /* Absolute: a scheme first. */
    {
        const char *p = ref;
        while (isalnum((unsigned char)*p) || *p == '+' || *p == '-' || *p == '.')
            p++;
        if (p > ref && *p == ':') {
            r = snprintf(out, n, "%s", ref);
            return r < 0 || (size_t)r >= n ? -1 : 0;
        }
    }
    scheme_end = strstr(base, "://");
    if (!scheme_end)
        return -1;
    auth_end = scheme_end + 3;
    while (*auth_end && *auth_end != '/' && *auth_end != '?' && *auth_end != '#')
        auth_end++;

    if (ref[0] == '/' && ref[1] == '/') { /* scheme-relative */
        r = snprintf(out, n, "%.*s:%s", (int)(scheme_end - base), base, ref);
        return r < 0 || (size_t)r >= n ? -1 : 0;
    }
    pre = (size_t)(auth_end - base);
    if (ref[0] == '?') { /* same path, new query */
        path_end = auth_end + strcspn(auth_end, "?#");
        r = snprintf(out, n, "%.*s%s", (int)(path_end - base), base, ref);
        return r < 0 || (size_t)r >= n ? -1 : 0;
    }
    if (ref[0] == '/') {
        r = snprintf(path, sizeof(path), "%s", ref);
    } else {
        /* The base's directory, then ref. */
        const char *bp = auth_end, *q = bp + strcspn(bp, "?#"), *slash = NULL, *s;
        for (s = bp; s < q; s++)
            if (*s == '/')
                slash = s;
        if (!slash)
            r = snprintf(path, sizeof(path), "/%s", ref);
        else
            r = snprintf(path, sizeof(path), "%.*s%s", (int)(slash - bp + 1), bp, ref);
    }
    if (r < 0 || (size_t)r >= sizeof(path))
        return -1;
    {
        /* Dots only in the path part, not the query. */
        char *q = path + strcspn(path, "?#"), keep[1024];
        snprintf(keep, sizeof(keep), "%s", q);
        *q = '\0';
        remove_dots(path);
        if (path[0] != '/') {
            memmove(path + 1, path, strlen(path) + 1);
            path[0] = '/';
        }
        r = snprintf(out, n, "%.*s%s%s", (int)pre, base, path, keep);
    }
    return r < 0 || (size_t)r >= n ? -1 : 0;
}
