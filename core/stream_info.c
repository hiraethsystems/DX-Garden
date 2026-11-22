#include "stream_info.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "load_report.h"
#include "playlist.h"
#include "station.h"
#include "text.h"

/* Lower-cased media type without parameters ("audio/mpeg"). */
static void media_type(const char *ct, char *out, size_t n)
{
    size_t i = 0;
    if (!ct)
        ct = "";
    while (*ct == ' ')
        ct++;
    while (*ct && *ct != ';' && *ct != ' ' && i + 1 < n)
        out[i++] = (char)tolower((unsigned char)*ct++);
    out[i] = '\0';
}

static int in_list(const char *s, const char *const *list)
{
    for (; *list; list++)
        if (strcmp(s, *list) == 0)
            return 1;
    return 0;
}

/* Case-insensitive search in a buffer that may not be NUL-terminated. */
static int contains_ci(const unsigned char *h, size_t n, const char *needle)
{
    size_t k = strlen(needle), i, j;
    if (k > n)
        return 0;
    for (i = 0; i + k <= n; i++) {
        for (j = 0; j < k; j++)
            if (tolower(h[i + j]) != tolower((unsigned char)needle[j]))
                break;
        if (j == k)
            return 1;
    }
    return 0;
}

/* Skips a UTF-8 byte order mark and leading white space. */
static const unsigned char *skip_lead(const unsigned char *p, size_t *n)
{
    if (*n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        p += 3;
        *n -= 3;
    }
    while (*n && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        p++;
        (*n)--;
    }
    return p;
}

static int starts_ci(const unsigned char *p, size_t n, const char *s)
{
    size_t k = strlen(s), i;
    if (n < k)
        return 0;
    for (i = 0; i < k; i++)
        if (tolower(p[i]) != tolower((unsigned char)s[i]))
            return 0;
    return 1;
}

static int sniff_playlist(const unsigned char *p, size_t n)
{
    p = skip_lead(p, &n);
    return starts_ci(p, n, "#EXTM3U") || starts_ci(p, n, "[playlist]") ||
           starts_ci(p, n, "<asx") || starts_ci(p, n, "http://") || starts_ci(p, n, "https://");
}

static int looks_html(const unsigned char *p, size_t n)
{
    p = skip_lead(p, &n);
    return starts_ci(p, n, "<!doctype html") || starts_ci(p, n, "<html");
}

static int sniff_mp3(const unsigned char *p, size_t n)
{
    if (n >= 3 && p[0] == 'I' && p[1] == 'D' && p[2] == '3')
        return 1;
    /* An MPEG audio frame header (layer bits non-zero, unlike ADTS AAC),
     * at the start or within the first few hundred bytes. */
    {
        size_t i;
        for (i = 0; i + 1 < n && i < 512; i++)
            if (p[i] == 0xFF && (p[i + 1] & 0xE0) == 0xE0 && ((p[i + 1] >> 1) & 3) != 0 &&
                (p[i + 1] & 0x18) != 0x08)
                return 1;
    }
    return 0;
}

static const char *sniff_other(const unsigned char *p, size_t n)
{
    if (n >= 4 && memcmp(p, "OggS", 4) == 0)
        return "Ogg";
    if (n >= 4 && memcmp(p, "fLaC", 4) == 0)
        return "FLAC";
    if (n >= 2 && p[0] == 0xFF && (p[1] & 0xF6) == 0xF0)
        return "AAC";
    return NULL;
}

stream_kind stream_classify(const char *content_type, const char *url,
                            const unsigned char *head, size_t n, char *format, size_t fn)
{
    static const char *const MP3[] = { "audio/mpeg", "audio/mp3", "audio/mpeg3", "audio/x-mpeg",
                                       "audio/x-mp3", "audio/mpg", NULL };
    static const char *const PLAYLIST[] = {
        "audio/x-mpegurl", "audio/mpegurl", "application/x-mpegurl", "audio/x-scpls",
        "audio/scpls", "application/pls+xml", "application/pls", "video/x-ms-asf",
        "audio/x-ms-asx", "video/x-ms-asx", "application/vnd.apple.mpegurl", NULL
    };
    static const struct {
        const char *type, *name;
    } OTHER[] = {
        { "audio/aac", "AAC" },     { "audio/aacp", "AAC" },   { "audio/x-aac", "AAC" },
        { "audio/mp4", "AAC" },     { "audio/x-m4a", "AAC" },  { "application/ogg", "Ogg" },
        { "audio/ogg", "Ogg" },     { "audio/opus", "Opus" },  { "audio/flac", "FLAC" },
        { "audio/x-flac", "FLAC" }, { "audio/webm", "WebM" },  { "audio/wav", "WAV" },
        { NULL, NULL }
    };
    char mt[64];
    const char *other;
    size_t i, ul = url ? strlen(url) : 0;
    int url_playlist = 0;

    if (fn)
        format[0] = '\0';
    media_type(content_type, mt, sizeof(mt));
    if (contains_ci(head, n, "#EXT-X-"))
        return SK_HLS;
    if (url && (text_ends_with_ci(url, ".m3u8") || strstr(url, ".m3u8?")))
        url_playlist = 1;
    if (url && ul && (text_ends_with_ci(url, ".m3u") || text_ends_with_ci(url, ".pls") ||
                      text_ends_with_ci(url, ".asx")))
        url_playlist = 1;

    /* A web page, even at a playlist-looking address (sites often answer
     * a stale .pls or .m3u link with an HTML page). */
    if ((strcmp(mt, "text/html") == 0 || strcmp(mt, "application/xhtml+xml") == 0 ||
         looks_html(head, n)) &&
        !sniff_playlist(head, n))
        return SK_HTML;
    if (in_list(mt, PLAYLIST) || (url_playlist && !sniff_mp3(head, n)))
        return sniff_mp3(head, n) ? SK_MP3 : SK_PLAYLIST;
    if (in_list(mt, MP3))
        return SK_MP3;
    for (i = 0; OTHER[i].type; i++)
        if (strcmp(mt, OTHER[i].type) == 0) {
            snprintf(format, fn, "%s", OTHER[i].name);
            return SK_OTHER;
        }
    if (strcmp(mt, "text/html") == 0 || strcmp(mt, "application/xhtml+xml") == 0)
        return SK_PLAYLIST; /* a URL list served as a web page */
    /* Vague or missing type (application/octet-stream, text/plain, ""):
     * look at the bytes. */
    if (sniff_playlist(head, n))
        return SK_PLAYLIST;
    other = sniff_other(head, n);
    if (other) {
        snprintf(format, fn, "%s", other);
        return SK_OTHER;
    }
    if (sniff_mp3(head, n))
        return SK_MP3;
    return SK_UNKNOWN;
}

/* ---- audio format from the bytes ------------------------------------- */

/* Length of the MPEG audio frame starting at p, or 0 if it isn't one. */
static size_t mpa_frame_len(const unsigned char *p)
{
    static const unsigned short BR[2][3][16] = {
        { /* MPEG-1: layer I, II, III */
          { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0 },
          { 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0 },
          { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 } },
        { /* MPEG-2 and 2.5 */
          { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0 },
          { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 },
          { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 } }
    };
    static const unsigned SR[3] = { 44100, 48000, 32000 };
    unsigned ver, layer, bri, sri, pad, br, sr;

    if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0)
        return 0;
    ver = (p[1] >> 3) & 3; /* 0: 2.5, 1: reserved, 2: 2, 3: 1 */
    layer = (p[1] >> 1) & 3; /* 1: III, 2: II, 3: I, 0: reserved (ADTS) */
    bri = p[2] >> 4;
    sri = (p[2] >> 2) & 3;
    pad = (p[2] >> 1) & 1;
    if (ver == 1 || layer == 0 || bri == 0 || bri == 15 || sri == 3)
        return 0;
    br = BR[ver == 3 ? 0 : 1][3 - layer][bri] * 1000u;
    sr = SR[sri] >> (ver == 3 ? 0 : ver == 2 ? 1 : 2);
    if (layer == 3) /* layer I */
        return (12 * br / sr + pad) * 4;
    if (layer == 1 && ver != 3) /* layer III, MPEG-2/2.5 */
        return 72 * br / sr + pad;
    return 144 * br / sr + pad;
}

/* Length of the ADTS frame starting at p (7 bytes readable), or 0. */
static size_t adts_frame_len(const unsigned char *p)
{
    size_t len;
    if (p[0] != 0xFF || (p[1] & 0xF6) != 0xF0) /* sync, layer 0 */
        return 0;
    if (((p[2] >> 2) & 0xF) > 12) /* sampling frequency index */
        return 0;
    len = ((size_t)(p[3] & 3) << 11) | ((size_t)p[4] << 3) | (p[5] >> 5);
    return len >= 7 ? len : 0;
}

/* Length of the LOAS (AudioSyncStream) frame starting at p, or 0. */
static size_t loas_frame_len(const unsigned char *p)
{
    if (p[0] != 0x56 || (p[1] & 0xE0) != 0xE0)
        return 0;
    return 3 + (((size_t)(p[1] & 0x1F) << 8) | p[2]);
}

/* Two frames in a row starting at i, within n bytes. */
static int frames_line_up(const unsigned char *p, size_t n, size_t i, size_t (*len)(const unsigned char *))
{
    size_t a, b;
    if (i + 7 > n)
        return 0;
    a = len(p + i);
    if (!a || i + a + 7 > n)
        return 0;
    b = len(p + i + a);
    return b != 0;
}

audio_format audio_sniff(const unsigned char *p, size_t n)
{
    size_t at;
    return audio_sniff_at(p, n, &at);
}

audio_format audio_sniff_at(const unsigned char *p, size_t n, size_t *at)
{
    size_t i, start = 0, limit;

    *at = 0;
    if (!p)
        return AF_UNKNOWN;
    /* ID3v2: "ID3", version, flags, then a 28-bit size. */
    if (n >= 10 && p[0] == 'I' && p[1] == 'D' && p[2] == '3' && !(p[6] & 0x80) &&
        !(p[7] & 0x80) && !(p[8] & 0x80) && !(p[9] & 0x80)) {
        size_t sz = ((size_t)p[6] << 21) | ((size_t)p[7] << 14) | ((size_t)p[8] << 7) | p[9];
        start = 10 + sz + ((p[5] & 0x10) ? 10 : 0);
        if (start >= n)
            return AF_MP3; /* only the tag so far: as before, MP3 */
    }
    *at = start;
    if (n - start >= 4 && memcmp(p + start, "OggS", 4) == 0)
        return AF_OGG;
    if (n - start >= 4 && memcmp(p + start, "fLaC", 4) == 0)
        return AF_FLAC;
    /* Streams joined mid-frame: look a little way in. */
    limit = n - start < 8192 ? n : start + 8192;
    for (i = start; i + 7 <= limit; i++) {
        *at = i;
        if (p[i] == 0xFF) {
            if (frames_line_up(p, n, i, adts_frame_len))
                return AF_ADTS;
            if (frames_line_up(p, n, i, mpa_frame_len))
                return AF_MP3;
        } else if (p[i] == 0x56 && frames_line_up(p, n, i, loas_frame_len)) {
            return AF_LOAS;
        }
    }
    *at = 0;
    return AF_UNKNOWN;
}

ogg_codec ogg_sniff(const unsigned char *p, size_t n)
{
    size_t body;
    if (!p || n < 28 || memcmp(p, "OggS", 4) != 0)
        return OGG_UNKNOWN;
    body = 27 + (size_t)p[26]; /* header, then the segment table */
    if (body + 8 > n)
        return OGG_UNKNOWN;
    if (memcmp(p + body, "\x01vorbis", 7) == 0)
        return OGG_VORBIS;
    if (memcmp(p + body, "OpusHead", 8) == 0)
        return OGG_OPUS;
    if (memcmp(p + body, "\x7f" "FLAC", 5) == 0)
        return OGG_FLAC;
    return OGG_UNKNOWN;
}

static int ncase_eq(const char *a, const char *b, size_t k)
{
    size_t i;
    for (i = 0; i < k; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return 0;
    return 1;
}

/* ASX: the first <ref href="..."> (or <entryref href="...">). */
static int asx_first(const char *body, size_t len, char *out, size_t n)
{
    size_t i;
    for (i = 0; i + 6 < len; i++) {
        if (ncase_eq(body + i, "href", 4)) {
            size_t j = i + 4, k;
            while (j < len && (body[j] == ' ' || body[j] == '='))
                j++;
            if (j < len && (body[j] == '"' || body[j] == '\''))
                j++;
            for (k = j; k < len && body[k] != '"' && body[k] != '\'' && body[k] != '>' &&
                        body[k] != ' ';
                 k++)
                ;
            if (k > j && k - j < n) {
                char *u = text_ndup(body + j, k - j);
                int ok = u && text_is_http_url(u);
                if (ok)
                    snprintf(out, n, "%s", u);
                free(u);
                if (ok)
                    return 1;
            }
        }
    }
    return 0;
}

int playlist_first_url(const char *body, size_t len, char *out, size_t n)
{
    station_list l;
    load_report r;
    const unsigned char *p = (const unsigned char *)body;
    size_t m = len;
    int found = 0, i;

    if (n)
        out[0] = '\0';
    if (contains_ci(p, len, "#EXT-X-"))
        return -1;
    p = skip_lead(p, &m);
    if (starts_ci(p, m, "<asx") || contains_ci(p, m, "<ref "))
        return asx_first(body, len, out, n);

    station_list_init(&l);
    report_init(&r);
    if (starts_ci(p, m, "[playlist]"))
        pls_parse(body, len, "stream", &l, &r);
    else
        m3u_parse(body, len, "stream", &l, &r);
    for (i = 0; i < l.count && !found; i++)
        if (l.items[i].url && text_is_http_url(l.items[i].url) && strlen(l.items[i].url) < n) {
            snprintf(out, n, "%s", l.items[i].url);
            found = 1;
        }
    station_list_free(&l);
    report_free(&r);
    return found;
}

const char *http_status_message(long code, char *buf, size_t n)
{
    const char *what;
    switch (code) {
    case 400: what = "The server didn't understand the request"; break;
    case 401: what = "Needs a login"; break;
    case 402: what = "Needs payment"; break;
    case 403: what = "Refused, possibly blocked in your country"; break;
    case 404: what = "Not found: the stream may have moved"; break;
    case 405: what = "The server refused this kind of request"; break;
    case 408: what = "The server timed out"; break;
    case 410: what = "Gone: this stream has been shut down"; break;
    case 429: what = "Too many listeners or requests"; break;
    case 451: what = "Blocked in your country for legal reasons"; break;
    case 500: what = "Server error"; break;
    case 502: what = "Bad gateway: the stream's source is down"; break;
    case 503: what = "Server full or down"; break;
    case 504: what = "Gateway timeout: the stream's source isn't answering"; break;
    default:
        what = code >= 500 ? "Server problem" : code >= 400 ? "Refused by the server"
                                                            : "Unexpected reply";
    }
    snprintf(buf, n, "%s (HTTP %ld)", what, code);
    return buf;
}

int http_status_retry(long code)
{
    return code == 408 || code == 429 || code >= 500;
}

unsigned reconnect_delay_ms(int attempt)
{
    if (attempt < 1)
        attempt = 1;
    if (attempt >= 5)
        return 15000;
    return 1000u << (attempt - 1);
}
