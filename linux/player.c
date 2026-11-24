#include "player.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <SDL3/SDL.h>
#include <curl/curl.h>

#include "decode.h"
#include "hls.h"
#include "icy.h"
#include "log.h"
#include "media_demux.h"
#include "stream_buffer.h"
#include "stream_info.h"
#include "track_info.h"

#define GRAIN 1024        /* samples per push to the audio stream */
#define USER_AGENT "DXGarden/" DXG_VERSION " (" DXG_PLATFORM_NAME ")"
/* Decoded audio queued ahead of the device. SDL pulls from this on its own
 * thread; more than this and the decoder waits, which keeps the stream
 * buffer (not the device queue) holding the slack. */
#define QUEUE_MS 250
#define PLAYLIST_MAX (64 * 1024)   /* largest playlist body read */
#define PLAYLIST_DEPTH 3           /* playlists pointing at playlists */
#define STABLE_BYTES (256 * 1024)  /* a connection this long resets the retry count */
/* A stream slower than this for STALL_S is treated as dropped. A full ring
 * still reads at the stream's own bitrate, and the slowest MP3 streams
 * (16 kbps) send 2 KB/s, so only a stall trickles below it. 00.21 waited
 * for 20 s of nothing, and a trickling server gave 45 s of silence. */
#define STALL_BYTES_PER_S 1000L
#define STALL_S 10L

typedef struct {
    SDL_Mutex *lock;
    SDL_Condition *data_cond;  /* decoder waits for data */
    SDL_Condition *space_cond; /* network waits for space */
    stream_buffer sb;

    /* Session. Everything below is guarded by `lock`. */
    int active;
    int stop;      /* threads should wind down */
    int user_stop; /* the stop was asked for, so it is not an error */
    int net_done, dec_done;
    SDL_Thread *net_thid, *dec_thid;
    char url[512];
    char pending_url[512];
    size_t pending_prebuffer;
    size_t base_prebuffer; /* the setting, for 128 kbps; scaled for faster streams */

    CURL *curl;
    int connected;     /* this connection has sent a reply */
    long http_status;
    char content_type[64];
    char icy_name[96];
    char cur_url[512]; /* what is being fetched (a playlist's target, say) */
    char title[ICY_TITLE_MAX]; /* now playing, as the stream gives it */
    char artist[128], song[ICY_TITLE_MAX]; /* split, or "" and the whole title */
    char icy_url[256];         /* the station's website, from icy-url */
    int reconnecting;  /* attempt number while reconnecting, else 0 */
    uint64_t retry_at; /* SDL_GetTicksNS() */
    char reconnect_why[128];

    /* Network-thread only, per connection. */
    size_t metaint;
    int mode;          /* MODE_* below, decided by the first data */
    char fatal[256];   /* set by callbacks: don't retry, show this */
    char *plbuf;
    size_t pl_len;
    uint64_t conn_bytes;

    codec_id codec_hint; /* from the Content-Type; the bytes may overrule it */
    int hls;             /* playing an HLS stream */
    char codec[32];      /* what the decoder found: "MP3", "HE-AAC", ... */
    long rate;
    int channels;
    int bitrate_kbps;
    uint64_t decode_us;
    uint64_t samples_out;

    char error[256];
} player;

static player P;
static char ca_file[512]; /* trusted certificates for curl (find_ca_bundle) */
static int ca_dir;        /* no bundle file found: use /etc/ssl/certs */
static int ca_found;      /* find_ca_bundle has run */

static void lock(void)
{
    SDL_LockMutex(P.lock);
}

static void unlock(void)
{
    SDL_UnlockMutex(P.lock);
}

/* Record the first error of a session; later ones are usually fallout.
 * Call with the lock held. */
static void set_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_error(const char *fmt, ...)
{
    va_list ap;

    if (P.error[0])
        return;
    va_start(ap, fmt);
    vsnprintf(P.error, sizeof(P.error), fmt, ap);
    va_end(ap);
    LOG("error: %s", P.error);
}

/* ---- network thread ---------------------------------------------------- */

enum { MODE_UNDECIDED, MODE_AUDIO, MODE_PLAYLIST, MODE_HLS, MODE_REFUSED };

static icy_demux icy;                       /* network thread only */
static unsigned char demux_out[16 * 1024];

/* Copies a header value (after "name:") without spaces or line ends. */
static void header_value(const char *v, size_t n, char *out, size_t outn)
{
    while (n && (*v == ' ' || *v == '\t')) {
        v++;
        n--;
    }
    while (n && (v[n - 1] == '\r' || v[n - 1] == '\n' || v[n - 1] == ' '))
        n--;
    if (n >= outn)
        n = outn - 1;
    memcpy(out, v, n);
    out[n] = '\0';
}

static size_t on_header(char *buf, size_t size, size_t nmemb, void *ud)
{
    size_t len = size * nmemb;
    char val[96];
    (void)ud;

    /* Each response (redirects included) starts with a status line. */
    if (len > 5 && (strncasecmp(buf, "HTTP/", 5) == 0 || strncasecmp(buf, "ICY ", 4) == 0)) {
        P.metaint = 0;
    } else if (len > 9 && strncasecmp(buf, "icy-name:", 9) == 0) {
        header_value(buf + 9, len - 9, val, sizeof(val));
        lock();
        snprintf(P.icy_name, sizeof(P.icy_name), "%s", val);
        unlock();
    } else if (len > 8 && strncasecmp(buf, "icy-url:", 8) == 0) {
        char u[256];
        header_value(buf + 8, len - 8, u, sizeof(u));
        lock();
        if (strncasecmp(u, "http://", 7) == 0 || strncasecmp(u, "https://", 8) == 0)
            snprintf(P.icy_url, sizeof(P.icy_url), "%s", u);
        unlock();
    } else if (len > 12 && strncasecmp(buf, "icy-metaint:", 12) == 0) {
        long m;
        header_value(buf + 12, len - 12, val, sizeof(val));
        m = strtol(val, NULL, 10);
        P.metaint = m > 0 && m < 1024 * 1024 ? (size_t)m : 0;
    }
    return len;
}

/* First data of a connection: log it and decide what it is. Lock held. */
static void first_data(const unsigned char *buf, size_t n)
{
    char *ct = NULL, *eff = NULL, format[16];
    curl_off_t dns = 0, tcp = 0, tls = 0, first = 0;
    long redirects = 0;
    stream_kind kind;

    P.connected = 1;
    curl_easy_getinfo(P.curl, CURLINFO_RESPONSE_CODE, &P.http_status);
    curl_easy_getinfo(P.curl, CURLINFO_CONTENT_TYPE, &ct);
    snprintf(P.content_type, sizeof(P.content_type), "%s", ct ? ct : "");
    LOG("connected: HTTP %ld, type '%s', name '%s', metadata every %u bytes", P.http_status,
        P.content_type, P.icy_name, (unsigned)P.metaint);
    /* Where the time to first byte went, in ms. */
    curl_easy_getinfo(P.curl, CURLINFO_NAMELOOKUP_TIME_T, &dns);
    curl_easy_getinfo(P.curl, CURLINFO_CONNECT_TIME_T, &tcp);
    curl_easy_getinfo(P.curl, CURLINFO_APPCONNECT_TIME_T, &tls);
    curl_easy_getinfo(P.curl, CURLINFO_STARTTRANSFER_TIME_T, &first);
    curl_easy_getinfo(P.curl, CURLINFO_REDIRECT_COUNT, &redirects);
    curl_easy_getinfo(P.curl, CURLINFO_EFFECTIVE_URL, &eff);
    LOG("timing: dns %lld ms, tcp %lld ms, tls %lld ms, first byte %lld ms, redirects %ld",
        (long long)dns / 1000, (long long)tcp / 1000, (long long)tls / 1000,
        (long long)first / 1000, redirects);
    if (redirects && eff)
        LOG("redirected to %s", eff);

    kind = stream_classify(P.content_type, eff ? eff : P.cur_url, buf, n, format, sizeof(format));
    if (kind == SK_OTHER) {
        /* Formats the decoders take; anything else is refused below. */
        if (strcmp(format, "AAC") == 0)
            P.codec_hint = CODEC_AAC, kind = SK_MP3;
        else if (strcmp(format, "Ogg") == 0 || strcmp(format, "Opus") == 0)
            P.codec_hint = CODEC_OGG, kind = SK_MP3;
        else if (strcmp(format, "FLAC") == 0)
            P.codec_hint = CODEC_FLAC, kind = SK_MP3;
    } else if (kind == SK_MP3 || kind == SK_UNKNOWN) {
        P.codec_hint = CODEC_MP3;
    }
    switch (kind) {
    case SK_MP3:
    case SK_UNKNOWN:
        P.mode = MODE_AUDIO;
        icy_init(&icy, P.metaint);
        P.reconnecting = 0;
        break;
    case SK_PLAYLIST:
        P.mode = MODE_PLAYLIST;
        LOG("net: the address is a playlist, reading it");
        break;
    case SK_HLS:
        P.mode = MODE_HLS;
        LOG("net: the address is an HLS playlist");
        break;
    case SK_OTHER:
        P.mode = MODE_REFUSED;
        snprintf(P.fatal, sizeof(P.fatal),
                 "This station streams %s, which DX Garden doesn't play", format);
        break;
    case SK_HTML:
        P.mode = MODE_REFUSED;
        snprintf(P.fatal, sizeof(P.fatal), "That address is a web page, not a stream");
        break;
    }
}

/* Audio into the stream buffer, waiting for space. Lock held; returns 0 if
 * stopped. */
static int push_audio(const unsigned char *a, size_t n)
{
    size_t done = 0;
    while (done < n && !P.stop) {
        size_t k = sb_write(&P.sb, a + done, n - done);
        done += k;
        if (k)
            SDL_SignalCondition(P.data_cond);
        if (done < n)
            SDL_WaitCondition(P.space_cond, P.lock);
    }
    return !P.stop;
}

/* What's playing, from any source (ICY, Ogg/FLAC comments, HLS ID3 tags).
 * `artist` may be ""; then `title` is split if it reads "Artist - Title".
 * Lock held. */
static void set_track(const char *artist, const char *title)
{
    char a[sizeof(P.artist)], t[sizeof(P.song)], whole[sizeof(P.artist) + sizeof(P.song) + 4];
    if (artist[0]) {
        snprintf(a, sizeof(a), "%s", artist);
        snprintf(t, sizeof(t), "%s", title);
    } else {
        title_split(title, a, sizeof(a), t, sizeof(t));
    }
    if (a[0] && t[0])
        snprintf(whole, sizeof(whole), "%s - %s", a, t);
    else
        snprintf(whole, sizeof(whole), "%s", a[0] ? a : t);
    if (strncmp(whole, P.title, sizeof(P.title) - 1) == 0 && strcmp(a, P.artist) == 0)
        return;
    {
        /* Cut to fit without splitting a UTF-8 character. */
        size_t k = strlen(whole);
        if (k >= sizeof(P.title)) {
            k = sizeof(P.title) - 1;
            while (k && ((unsigned char)whole[k] & 0xC0) == 0x80)
                k--;
        }
        memcpy(P.title, whole, k);
        P.title[k] = '\0';
    }
    memcpy(P.artist, a, sizeof(P.artist));
    memcpy(P.song, t, sizeof(P.song));
    LOG("title: %s%s%s%s", whole[0] ? whole : "(none)", a[0] ? " [artist: " : "", a,
        a[0] ? "]" : "");
}

static size_t on_data(char *buf, size_t size, size_t nmemb, void *ud)
{
    size_t total = size * nmemb, off = 0;
    int ok = 1;
    (void)ud;

    lock();
    if (P.mode == MODE_UNDECIDED)
        first_data((const unsigned char *)buf, total);
    P.conn_bytes += total;

    if (P.mode == MODE_PLAYLIST || P.mode == MODE_HLS) {
        if (P.pl_len + total > PLAYLIST_MAX) {
            snprintf(P.fatal, sizeof(P.fatal), "The playlist is too large to read");
            ok = 0;
        } else {
            char *nb = realloc(P.plbuf, P.pl_len + total + 1);
            if (!nb) {
                ok = 0;
            } else {
                P.plbuf = nb;
                memcpy(P.plbuf + P.pl_len, buf, total);
                P.pl_len += total;
            }
        }
    } else if (P.mode == MODE_REFUSED) {
        ok = 0;
    } else {
        while (ok && off < total) {
            size_t chunk = total - off < sizeof(demux_out) ? total - off : sizeof(demux_out);
            size_t a = icy_feed(&icy, (const unsigned char *)buf + off, chunk, demux_out);
            off += chunk;
            if (icy.title_changed) {
                icy.title_changed = 0;
                set_track("", icy.title);
            }
            ok = push_audio(demux_out, a);
        }
        if (P.stop)
            ok = 0;
    }
    unlock();
    /* Returning less than `total` makes curl abort the transfer. */
    return ok ? total : 0;
}

static int on_progress(void *ud, curl_off_t dt, curl_off_t dn, curl_off_t ut,
                       curl_off_t un)
{
    int stopped;
    (void)ud; (void)dt; (void)dn; (void)ut; (void)un;

    lock();
    stopped = P.stop;
    unlock();
    return stopped;
}

typedef enum { OUT_STOPPED, OUT_PLAYLIST, OUT_HLS, OUT_DROPPED, OUT_FATAL } outcome;

/* The playlist body of an OUT_HLS, for hls_play (network thread only). */
static char *hls_body;
static size_t hls_body_len;

/* What every request shares: certificates, user agent, timeouts. */
static void net_setup(CURL *c, const char *url)
{
    curl_easy_setopt(c, CURLOPT_URL, url);
    if (ca_file[0])
        curl_easy_setopt(c, CURLOPT_CAINFO, ca_file);
    else if (ca_dir)
        curl_easy_setopt(c, CURLOPT_CAPATH, "/etc/ssl/certs");
    curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, STALL_BYTES_PER_S);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, STALL_S);
    curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
}

/* A failed request in plain words, and whether to retry. */
static outcome describe_failure(CURLcode rc, long code, int connected, const char *errbuf,
                                char *msg, size_t mn)
{
    outcome out = OUT_DROPPED;
    if (rc == CURLE_HTTP_RETURNED_ERROR) {
        http_status_message(code, msg, mn);
        return http_status_retry(code) ? OUT_DROPPED : OUT_FATAL;
    }
    switch (rc) {
    case CURLE_OK:
        snprintf(msg, mn, "The server ended the stream");
        break;
    case CURLE_COULDNT_RESOLVE_HOST:
        snprintf(msg, mn, "Server not found (DNS): check the network and the address");
        break;
    case CURLE_COULDNT_CONNECT:
        snprintf(msg, mn, "Couldn't connect to the server");
        break;
    case CURLE_OPERATION_TIMEDOUT:
        snprintf(msg, mn, connected ? "The stream stalled (under 1 KB/s for 10 s)"
                                    : "No answer from the server");
        break;
    case CURLE_RECV_ERROR:
    case CURLE_SEND_ERROR:
    case CURLE_PARTIAL_FILE:
    case CURLE_GOT_NOTHING:
        snprintf(msg, mn, "The connection dropped");
        break;
    case CURLE_PEER_FAILED_VERIFICATION:
        snprintf(msg, mn, "The server's security certificate isn't valid");
        out = OUT_FATAL;
        break;
    case CURLE_SSL_CACERT_BADFILE:
        snprintf(msg, mn, "Can't check secure servers: no certificate store");
        out = OUT_FATAL;
        break;
    case CURLE_TOO_MANY_REDIRECTS:
        snprintf(msg, mn, "Too many redirects");
        out = OUT_FATAL;
        break;
    case CURLE_UNSUPPORTED_PROTOCOL:
    case CURLE_URL_MALFORMAT:
        snprintf(msg, mn, "The stream address isn't valid");
        out = OUT_FATAL;
        break;
    default:
        snprintf(msg, mn, "%s%s%s", curl_easy_strerror(rc), errbuf && errbuf[0] ? ": " : "",
                 errbuf ? errbuf : "");
        break;
    }
    return out;
}

/* One request for P.cur_url. Puts a message in `msg` for DROPPED/FATAL and
 * the next URL in `next` for PLAYLIST. */
static outcome fetch_once(CURL *c, char *msg, size_t mn, char *next, size_t nn)
{
    char errbuf[CURL_ERROR_SIZE] = "";
    struct curl_slist *hdrs = curl_slist_append(NULL, "Icy-MetaData: 1");
    CURLcode rc;
    long code = 0;
    outcome out;

    lock();
    P.connected = 0;
    P.mode = MODE_UNDECIDED;
    P.fatal[0] = '\0';
    P.metaint = 0;
    P.conn_bytes = 0;
    free(P.plbuf);
    P.plbuf = NULL;
    P.pl_len = 0;
    unlock();

    net_setup(c, P.cur_url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data);

    LOG("net: GET %s", P.cur_url);
    rc = curl_easy_perform(c);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, NULL);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, NULL);
    curl_slist_free_all(hdrs);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    {
        /* Where it ended up: relative segment addresses resolve from it. */
        char *eff = NULL;
        curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &eff);
        snprintf(next, nn, "%s", eff ? eff : P.cur_url);
    }
    LOG("net: finished, curl code %d, %llu bytes", (int)rc, (unsigned long long)P.conn_bytes);

    lock();
    msg[0] = '\0';
    if (P.stop) {
        out = OUT_STOPPED;
    } else if (P.fatal[0]) {
        snprintf(msg, mn, "%s", P.fatal);
        out = OUT_FATAL;
    } else if (P.mode == MODE_HLS && rc == CURLE_OK && P.plbuf) {
        /* Handed to hls_play; `next` already has the playlist's address. */
        free(hls_body);
        hls_body = P.plbuf;
        hls_body_len = P.pl_len;
        hls_body[hls_body_len] = '\0';
        P.plbuf = NULL;
        out = OUT_HLS;
    } else if (P.mode == MODE_PLAYLIST && rc == CURLE_OK) {
        char eff[512];
        int r;
        snprintf(eff, sizeof(eff), "%s", next);
        r = P.plbuf ? (P.plbuf[P.pl_len] = '\0', playlist_first_url(P.plbuf, P.pl_len, next, nn))
                    : 0;
        if (r == 1) {
            out = OUT_PLAYLIST;
        } else if (r < 0) {
            /* An HLS playlist after all (served as a plain one). */
            snprintf(next, nn, "%s", eff);
            free(hls_body);
            hls_body = P.plbuf;
            hls_body_len = P.pl_len;
            P.plbuf = NULL;
            out = OUT_HLS;
        } else {
            snprintf(msg, mn, "The playlist has no stream address in it");
            out = OUT_FATAL;
        }
    } else {
        out = describe_failure(rc, code, P.connected, errbuf, msg, mn);
    }
    free(P.plbuf);
    P.plbuf = NULL;
    P.pl_len = 0;
    unlock();
    return out;
}

/* Sleeps up to ms, waking early on stop. Returns 0 if stopped. */
static int wait_or_stop(unsigned ms)
{
    unsigned waited = 0;
    while (waited < ms) {
        int stopped;
        lock();
        stopped = P.stop;
        unlock();
        if (stopped)
            return 0;
        SDL_Delay(100);
        waited += 100;
    }
    return 1;
}

/* Where curl finds trusted certificates. Windows: Schannel uses the
 * system store. Linux: a distribution's own libcurl knows its bundle; the
 * AppImage's static one has none built in (each distribution keeps it
 * somewhere else), so look for it once: $SSL_CERT_FILE, then the usual
 * places. */
static void find_ca_bundle(void)
{
    static const char *const FILES[] = {
        "/etc/ssl/certs/ca-certificates.crt",                /* Debian, Ubuntu, Arch, Gentoo */
        "/etc/pki/tls/certs/ca-bundle.crt",                  /* Fedora, RHEL */
        "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem", /* RHEL, CentOS */
        "/var/lib/ca-certificates/ca-bundle.pem",            /* openSUSE */
        "/etc/ssl/ca-bundle.pem",                            /* older openSUSE */
        "/etc/ssl/cert.pem",                                 /* Alpine, Void */
        "/etc/pki/tls/cacert.pem",
    };
    const curl_version_info_data *v = curl_version_info(CURLVERSION_NOW);
    const char *env = SDL_getenv("SSL_CERT_FILE");
    size_t i;
    if (ca_found)
        return;
    ca_found = 1;
    if (env && env[0] && SDL_GetPathInfo(env, NULL)) {
        snprintf(ca_file, sizeof(ca_file), "%s", env);
    } else if (v && v->cainfo && SDL_GetPathInfo(v->cainfo, NULL)) {
        LOG("net: certificates from %s (curl's own)", v->cainfo);
        return;
    } else {
        for (i = 0; i < sizeof(FILES) / sizeof(FILES[0]) && !ca_file[0]; i++)
            if (SDL_GetPathInfo(FILES[i], NULL))
                snprintf(ca_file, sizeof(ca_file), "%s", FILES[i]);
    }
    if (ca_file[0]) {
        LOG("net: certificates from %s", ca_file);
    } else {
        ca_dir = 1;
        LOG("net: no certificate bundle found; trying /etc/ssl/certs");
    }
}

void player_tls_setup(void *curl)
{
    find_ca_bundle();
    if (ca_file[0])
        curl_easy_setopt((CURL *)curl, CURLOPT_CAINFO, ca_file);
    else if (ca_dir)
        curl_easy_setopt((CURL *)curl, CURLOPT_CAPATH, "/etc/ssl/certs");
}

/* ---- HLS -------------------------------------------------------------- */

#define HLS_MAX_BPS 320000L      /* the best audio-only version up to this */
#define HLS_START_SEGMENTS 3     /* a live stream starts at least this far from its end */
#define HLS_START_S 20.0         /* ... and at least this many seconds */
#define HLS_MAX_BYTES (32u << 20) /* largest playlist or segment read */

typedef struct {
    unsigned char *p;
    size_t n, cap;
    int too_big;
} membuf;

static size_t mem_write(char *buf, size_t size, size_t nmemb, void *ud)
{
    membuf *m = ud;
    size_t k = size * nmemb;
    if (m->n + k + 1 > m->cap) {
        size_t cap = m->cap ? m->cap : 64 * 1024;
        unsigned char *np;
        while (cap < m->n + k + 1)
            cap *= 2;
        if (cap > HLS_MAX_BYTES) {
            m->too_big = 1;
            return 0;
        }
        np = realloc(m->p, cap);
        if (!np)
            return 0;
        m->p = np;
        m->cap = cap;
    }
    memcpy(m->p + m->n, buf, k);
    m->n += k;
    m->p[m->n] = 0;
    return k;
}

/* GETs url into m. Returns OUT_PLAYLIST on success (meaning "carry on"),
 * else what to do, with a message. The final address goes in eff. */
static outcome fetch_mem(CURL *c, const char *url, membuf *m, char *eff, size_t effn, long *code,
                         char *msg, size_t mn)
{
    char errbuf[CURL_ERROR_SIZE] = "";
    char *e = NULL;
    CURLcode rc;
    int stopped;

    m->n = 0;
    m->too_big = 0;
    net_setup(c, url);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, NULL);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mem_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, m);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
    rc = curl_easy_perform(c);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, NULL);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, NULL);
    *code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, code);
    curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &e);
    if (eff)
        snprintf(eff, effn, "%s", e ? e : url);
    lock();
    stopped = P.stop;
    unlock();
    if (stopped)
        return OUT_STOPPED;
    if (rc == CURLE_OK)
        return OUT_PLAYLIST;
    if (m->too_big) {
        snprintf(msg, mn, "An HLS file was too large");
        return OUT_FATAL;
    }
    return describe_failure(rc, *code, 1, errbuf, msg, mn);
}

/* Fetches and parses the playlist at url (updated to its final address). */
static outcome hls_load(CURL *c, char *url, size_t un, hls_playlist *pl, membuf *m, char *msg,
                        size_t mn)
{
    long code;
    char eff[1024];
    outcome o = fetch_mem(c, url, m, eff, sizeof(eff), &code, msg, mn);
    memset(pl, 0, sizeof(*pl));
    if (o != OUT_PLAYLIST)
        return o;
    snprintf(url, un, "%s", eff);
    if (hls_parse((const char *)m->p, m->n, pl) != 0) {
        hls_free(pl);
        snprintf(msg, mn, "The HLS playlist couldn't be read");
        return OUT_FATAL;
    }
    return OUT_PLAYLIST;
}

static int hls_stopped; /* push_audio saw a stop */

/* An ID3 tag from an HLS stream (timed metadata in TS, or at the start of
 * a packed-audio segment): artist and title if it has them. */
static void hls_meta(void *ud, const unsigned char *p, size_t n)
{
    char a[128], t[ICY_TITLE_MAX];
    (void)ud;
    if (id3_track(p, n, a, sizeof(a), t, sizeof(t))) {
        lock();
        set_track(a, t);
        unlock();
    }
}

static void hls_es(void *ud, const unsigned char *p, size_t n)
{
    (void)ud;
    if (hls_stopped || !n)
        return;
    lock();
    P.conn_bytes += n;
    if (!push_audio(p, n))
        hls_stopped = 1;
    unlock();
}

/* Plays an HLS stream: picks a version from a master playlist, then keeps
 * reloading the media playlist and feeding each new segment's audio into
 * the buffer. Returns when stopped or when the stream fails. */
static outcome hls_play(CURL *c, const char *body, size_t blen, const char *url, char *msg,
                        size_t mn)
{
    char pl_url[1024], seg_url[1024], map_url[1024] = "", ref_url[1024];
    hls_playlist pl;
    membuf m = { NULL, 0, 0, 0 };
    ts_demux ts;
    mp4_audio mp4;
    long long last = -1;
    int fails = 0, first_audio = 1, i;
    outcome o = OUT_DROPPED;

    hls_stopped = 0;
    snprintf(pl_url, sizeof(pl_url), "%s", url);
    if (hls_parse(body, blen, &pl) != 0) {
        hls_free(&pl);
        snprintf(msg, mn, "The HLS playlist couldn't be read");
        return OUT_FATAL;
    }
    if (pl.is_master) {
        int v = hls_pick_variant(&pl, HLS_MAX_BPS);
        const char *ref;
        /* A video version with the audio on its own: take the audio. */
        if (pl.audio_uri && (v < 0 || (!pl.variants[v].audio_only && pl.variants[v].has_codecs)))
            ref = pl.audio_uri;
        else
            ref = v >= 0 ? pl.variants[v].uri : NULL;
        if (!ref || url_resolve(pl_url, ref, ref_url, sizeof(ref_url)) != 0) {
            hls_free(&pl);
            snprintf(msg, mn, "The HLS playlist lists no usable stream");
            return OUT_FATAL;
        }
        LOG("hls: %d versions; playing %s (%ld bps)", pl.nvariants, ref_url,
            v >= 0 && ref != pl.audio_uri ? pl.variants[v].bandwidth : 0L);
        hls_free(&pl);
        snprintf(pl_url, sizeof(pl_url), "%s", ref_url);
        o = hls_load(c, pl_url, sizeof(pl_url), &pl, &m, msg, mn);
        if (o != OUT_PLAYLIST)
            goto out;
        if (pl.is_master) {
            snprintf(msg, mn, "The HLS playlist points to another list of versions");
            o = OUT_FATAL;
            goto out;
        }
    }
    ts_init(&ts);
    ts.meta_out = hls_meta;
    memset(&mp4, 0, sizeof(mp4));
    lock();
    P.hls = 1;
    P.codec_hint = CODEC_AAC;
    snprintf(P.content_type, sizeof(P.content_type), "HLS");
    unlock();

    for (;;) {
        int start = 0, did = 0;

        if (pl.encrypted) {
            snprintf(msg, mn, "This HLS stream is encrypted, which DX Garden can't play");
            o = OUT_FATAL;
            goto out;
        }
        if (pl.nsegs && last >= 0 && pl.segs[pl.nsegs - 1].seq + 10 < last) {
            LOG("hls: the stream restarted its numbering");
            last = -1;
        }
        if (last < 0 && !pl.endlist) {
            /* Start a little behind the live edge: at least three segments
             * and HLS_START_S of audio, so the buffer has a cushion. */
            double behind = 0;
            start = pl.nsegs;
            while (start > 0 && (pl.nsegs - start < HLS_START_SEGMENTS || behind < HLS_START_S)) {
                start--;
                behind += pl.segs[start].duration > 0 ? pl.segs[start].duration
                                                      : pl.target_duration;
            }
        }
        for (i = start; i < pl.nsegs; i++) {
            const hls_segment *sg = &pl.segs[i];
            long code;
            if (last >= 0 && sg->seq <= last)
                continue;
            if (last >= 0 && sg->seq > last + 1)
                LOG("hls: %lld segments missed", sg->seq - last - 1);
            /* fMP4: the initialisation section, when it changes. */
            if (sg->map) {
                char mu[1024];
                if (url_resolve(pl_url, sg->map, mu, sizeof(mu)) == 0 && strcmp(mu, map_url) != 0) {
                    o = fetch_mem(c, mu, &m, NULL, 0, &code, msg, mn);
                    if (o != OUT_PLAYLIST)
                        goto out;
                    snprintf(map_url, sizeof(map_url), "%s", mu);
                    if (mp4_init(&mp4, m.p, m.n) != 0) {
                        snprintf(msg, mn, "This HLS stream's audio (%s) isn't one DX Garden plays",
                                 mp4.codec[0] ? mp4.codec : "fMP4");
                        o = OUT_FATAL;
                        goto out;
                    }
                }
            }
            if (url_resolve(pl_url, sg->uri, seg_url, sizeof(seg_url)) != 0)
                continue;
            o = fetch_mem(c, seg_url, &m, NULL, 0, &code, msg, mn);
            if (o == OUT_STOPPED)
                goto out;
            if (o != OUT_PLAYLIST) {
                if (code == 404 || code == 410) {
                    /* Gone from the server already: skip it. */
                    LOG("hls: segment %lld missing (HTTP %ld)", sg->seq, code);
                    last = sg->seq;
                    continue;
                }
                goto out;
            }
            switch (segment_sniff(m.p, m.n)) {
            case SEG_TS:
                ts_feed(&ts, m.p, m.n, hls_es, NULL);
                if (ts.audio_pid < 0) {
                    if (ts.unsupported)
                        snprintf(msg, mn, "This HLS stream's audio is AC-3, which DX Garden "
                                          "doesn't play");
                    else
                        snprintf(msg, mn, "This HLS stream has no audio DX Garden can play");
                    o = OUT_FATAL;
                    goto out;
                }
                break;
            case SEG_PACKED: {
                size_t off = id3_skip(m.p, m.n);
                if (off)
                    hls_meta(NULL, m.p, off);
                hls_es(NULL, m.p + off, m.n - off);
                break;
            }
            case SEG_FMP4:
                if (!mp4.ok && mp4_init(&mp4, m.p, m.n) != 0) {
                    snprintf(msg, mn, "This HLS stream's audio isn't one DX Garden plays");
                    o = OUT_FATAL;
                    goto out;
                }
                mp4_fragment(&mp4, m.p, m.n, hls_es, NULL);
                break;
            case SEG_UNKNOWN:
                LOG("hls: segment %lld isn't audio DX Garden knows (%u bytes)", sg->seq,
                    (unsigned)m.n);
                break;
            }
            if (hls_stopped) {
                o = OUT_STOPPED;
                goto out;
            }
            if (first_audio) {
                first_audio = 0;
                LOG("hls: playing from segment %lld of %lld-%lld", sg->seq, pl.segs[0].seq,
                    pl.segs[pl.nsegs - 1].seq);
                lock();
                P.reconnecting = 0;
                unlock();
            }
            last = sg->seq;
            did++;
        }
        if (pl.endlist && (!pl.nsegs || last >= pl.segs[pl.nsegs - 1].seq)) {
            snprintf(msg, mn, "The stream ended");
            o = OUT_DROPPED;
            goto out;
        }
        /* Nothing new: wait for the server to add a segment. */
        if (!did) {
            unsigned ms = (unsigned)(pl.target_duration * 500);
            if (ms < 1000)
                ms = 1000;
            if (ms > 10000)
                ms = 10000;
            if (!wait_or_stop(ms)) {
                o = OUT_STOPPED;
                goto out;
            }
        }
        hls_free(&pl);
        o = hls_load(c, pl_url, sizeof(pl_url), &pl, &m, msg, mn);
        if (o == OUT_STOPPED)
            goto out;
        if (o != OUT_PLAYLIST) {
            LOG("hls: reloading the playlist failed: %s", msg);
            if (o == OUT_FATAL || ++fails >= 3)
                goto out;
            if (!wait_or_stop(2000)) {
                o = OUT_STOPPED;
                goto out;
            }
        } else {
            fails = 0;
        }
    }
out:
    hls_free(&pl);
    free(m.p);
    return o == OUT_PLAYLIST ? OUT_DROPPED : o;
}

static int SDLCALL net_thread(void *argp)
{
    char msg[256], next[512];
    int attempt = 0, depth = 0;
    CURL *c;
    (void)argp;

    c = curl_easy_init();
    if (!c) {
        lock();
        set_error("Could not start network (curl init failed)");
        sb_set_eof(&P.sb);
        P.net_done = 1;
        SDL_BroadcastCondition(P.data_cond);
        unlock();
        return 0;
    }
    lock();
    P.curl = c;
    snprintf(P.cur_url, sizeof(P.cur_url), "%s", P.url);
    unlock();

    for (;;) {
        outcome o = fetch_once(c, msg, sizeof(msg), next, sizeof(next));

        if (o == OUT_HLS) {
            o = hls_play(c, hls_body, hls_body_len, next, msg, sizeof(msg));
            free(hls_body);
            hls_body = NULL;
            /* A stream that played a while starts the retry count again. */
        }
        if (o == OUT_STOPPED)
            break;
        if (o == OUT_PLAYLIST) {
            if (++depth > PLAYLIST_DEPTH) {
                lock();
                set_error("Playlists point to more playlists; giving up");
                unlock();
                break;
            }
            LOG("net: playlist points to %s", next);
            lock();
            snprintf(P.cur_url, sizeof(P.cur_url), "%s", next);
            unlock();
            continue;
        }
        if (o == OUT_FATAL) {
            lock();
            set_error("%s", msg);
            unlock();
            break;
        }
        /* Dropped: try again after a pause, while the buffer plays on. A
         * connection that ran for a while starts the count again. */
        if (P.conn_bytes >= STABLE_BYTES)
            attempt = 0;
        if (++attempt > RECONNECT_MAX_ATTEMPTS) {
            lock();
            set_error("%s (gave up after %d tries)", msg, RECONNECT_MAX_ATTEMPTS);
            unlock();
            break;
        }
        {
            unsigned delay = reconnect_delay_ms(attempt);
            LOG("net: %s; reconnecting in %u s (attempt %d of %d)", msg, delay / 1000, attempt,
                RECONNECT_MAX_ATTEMPTS);
            lock();
            P.reconnecting = attempt;
            P.retry_at = SDL_GetTicksNS() + (uint64_t)delay * 1000000;
            snprintf(P.reconnect_why, sizeof(P.reconnect_why), "%s", msg);
            unlock();
            if (!wait_or_stop(delay))
                break;
        }
        /* Reconnect to the station's own address, which may have been a
         * playlist with a fresh target. */
        depth = 0;
        lock();
        snprintf(P.cur_url, sizeof(P.cur_url), "%s", P.url);
        unlock();
    }

    lock();
    P.curl = NULL;
    P.reconnecting = 0;
    sb_set_eof(&P.sb);
    P.net_done = 1;
    SDL_BroadcastCondition(P.data_cond);
    unlock();

    curl_easy_cleanup(c);
    return 0;
}

/* ---- decode thread ----------------------------------------------------- */

#define HEAD_BYTES (16 * 1024) /* read before choosing a decoder */

static int16_t grain[GRAIN * 2];
static unsigned char head[HEAD_BYTES];

/* Decode-thread state for the callbacks below. */
static struct {
    SDL_AudioStream *as;
    long out_rate;
    size_t filled; /* frames in `grain` */
    int applied_volume;
    int failed;
    uint64_t busy_since; /* decoding (not waiting) since then */
} D;

/* Output volume in percent, set by the UI thread (sleep timer fade). */
static SDL_AtomicInt volume_pct = {100};

/* One stream to the default device for the whole session. SDL converts
 * from the station's rate, so a rate change only changes the input side. */
static SDL_AudioStream *open_output(long rate)
{
    SDL_AudioSpec spec;
    SDL_AudioStream *as;

    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = (int)rate;
    as = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!as) {
        lock();
        set_error("Audio output refused %ld Hz: %s", rate, SDL_GetError());
        unlock();
        return NULL;
    }
    SDL_ResumeAudioStreamDevice(as);
    LOG("audio: default device open at %ld Hz", rate);
    return as;
}

/* Hands `frames` of `grain` to SDL, first waiting while QUEUE_MS is already
 * queued, so the decoder runs at the stream's own pace. Returns 0 if
 * stopped. */
static int output_grain(size_t frames)
{
    int max_bytes = (int)(D.out_rate * 4 * QUEUE_MS / 1000);
    int v = SDL_GetAtomicInt(&volume_pct);
    for (;;) {
        int stopped;
        lock();
        stopped = P.stop;
        unlock();
        if (stopped)
            return 0;
        if (SDL_GetAudioStreamQueued(D.as) < max_bytes)
            break;
        SDL_Delay(5);
    }
    if (v != D.applied_volume) {
        D.applied_volume = v;
        SDL_SetAudioStreamGain(D.as, v / 100.0f);
    }
    return SDL_PutAudioStreamData(D.as, grain, (int)(frames * 4));
}

/* Time spent decoding since the last read or emit (not waiting for data
 * or for the device), for the decode load figure. */
static void count_busy(void)
{
    uint64_t now = SDL_GetTicksNS();
    lock();
    P.decode_us += (now - D.busy_since) / 1000;
    unlock();
}

static size_t dec_read(void *ud, void *buf, size_t n)
{
    size_t k = 0;
    (void)ud;
    count_busy();
    lock();
    while (!P.stop && sb_get_state(&P.sb) == SB_BUFFERING)
        SDL_WaitCondition(P.data_cond, P.lock);
    if (!P.stop && sb_get_state(&P.sb) != SB_DRAINED) {
        k = sb_read(&P.sb, buf, n);
        SDL_SignalCondition(P.space_cond);
    }
    unlock();
    D.busy_since = SDL_GetTicksNS();
    return k;
}

static int dec_emit(void *ud, const int16_t *pcm, size_t frames, long rate)
{
    int ok = 1;
    (void)ud;
    count_busy();
    if (rate != D.out_rate) {
        /* What's left at the old rate goes out first. */
        if (D.filled && D.as && !output_grain(D.filled))
            ok = 0;
        D.filled = 0;
        if (!D.as) {
            D.as = open_output(rate);
            if (!D.as) {
                D.failed = 1;
                return 0;
            }
        } else {
            SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, (int)rate };
            SDL_SetAudioStreamFormat(D.as, &spec, NULL);
        }
        D.out_rate = rate;
    }
    while (ok && frames) {
        size_t k = GRAIN - D.filled < frames ? GRAIN - D.filled : frames;
        memcpy(grain + D.filled * 2, pcm, k * 4);
        D.filled += k;
        pcm += k * 2;
        frames -= k;
        if (D.filled == GRAIN) {
            lock();
            P.samples_out += GRAIN;
            unlock();
            ok = output_grain(GRAIN);
            D.filled = 0;
        }
    }
    D.busy_since = SDL_GetTicksNS();
    return ok;
}

/* The prebuffer setting is in bytes, sized for 128 kbps MP3 (64 KB is
 * 4 s). A faster stream gets proportionally more, so FLAC or 320 kbps AAC
 * has as many seconds of cushion. Lock held. */
static void apply_prebuffer(void)
{
    size_t pb = P.base_prebuffer;
    if (P.bitrate_kbps > 128) {
        pb = (size_t)((uint64_t)pb * (unsigned)P.bitrate_kbps / 128);
        if (pb > sb_capacity(&P.sb) / 2)
            pb = sb_capacity(&P.sb) / 2;
    }
    /* Small changes in the measured bitrate don't count. */
    if (pb * 10 > P.sb.prebuffer * 11 || pb * 11 < P.sb.prebuffer * 10) {
        LOG("player: prebuffer %u KB for %d kbps", (unsigned)(pb / 1024), P.bitrate_kbps);
        sb_set_prebuffer(&P.sb, pb);
    }
}

static void dec_info_cb(void *ud, const dec_info *di)
{
    (void)ud;
    lock();
    snprintf(P.codec, sizeof(P.codec), "%s%s", di->codec, P.hls ? " (HLS)" : "");
    P.rate = di->rate;
    P.channels = di->channels;
    if (di->bitrate_kbps > 0) {
        P.bitrate_kbps = di->bitrate_kbps;
        apply_prebuffer();
    }
    unlock();
}

/* Ogg and FLAC streams carry their own track details. */
static void dec_tags_cb(void *ud, const char *artist, const char *title)
{
    (void)ud;
    lock();
    set_track(artist, title);
    unlock();
}

/* The decoder for this stream: from its bytes when they're clear (servers
 * often mislabel AAC as audio/mpeg), else from its Content-Type. */
static codec_id choose_codec(const unsigned char *h, size_t n, codec_id by_type)
{
    switch (audio_sniff(h, n)) {
    case AF_MP3: return CODEC_MP3;
    case AF_ADTS:
    case AF_LOAS: return CODEC_AAC;
    case AF_OGG: return CODEC_OGG;
    case AF_FLAC: return CODEC_FLAC;
    case AF_UNKNOWN: break;
    }
    return by_type;
}

static int SDLCALL dec_thread(void *argp)
{
    static const dec_io io = { NULL, dec_read, dec_emit, dec_info_cb, dec_tags_cb };
    char err[256];
    size_t n = 0;
    codec_id codec;
    int rc, stopped;
    (void)argp;

    memset(&D, 0, sizeof(D));
    D.applied_volume = -1;
    D.busy_since = SDL_GetTicksNS();

    /* The first bytes decide the decoder (the prebuffer is larger). */
    while (n < sizeof(head)) {
        size_t k = dec_read(NULL, head + n, sizeof(head) - n);
        if (!k)
            break;
        n += k;
    }
    lock();
    codec = choose_codec(head, n, P.codec_hint);
    stopped = P.stop;
    unlock();

    if (n && !stopped) {
        LOG("decoder: %s (%s by its first bytes)", codec_name(codec),
            audio_sniff(head, n) != AF_UNKNOWN ? "chosen" : "not clear; chosen by type");
        rc = decode_run(codec, head, n, &io, err, sizeof(err));
        if (D.filled && D.as)
            output_grain(D.filled);
        lock();
        if (rc < 0) {
            set_error("%s", err);
            D.failed = 1;
        } else if (!P.stop && sb_get_state(&P.sb) != SB_DRAINED) {
            set_error("The %s decoder lost the stream", codec_name(codec));
            D.failed = 1;
        }
        unlock();
    }

    if (D.as)
        SDL_DestroyAudioStream(D.as);
    lock();
    if (D.failed) {
        /* Take the network thread down with us. */
        P.stop = 1;
        SDL_BroadcastCondition(P.space_cond);
    }
    LOG("decoder: finished%s", D.failed ? " with error" : "");
    P.dec_done = 1;
    unlock();
    return 0;
}

/* ---- public API -------------------------------------------------------- */

int player_init(size_t ring_capacity)
{
    memset(&P, 0, sizeof(P));
    P.lock = SDL_CreateMutex();
    P.data_cond = SDL_CreateCondition();
    P.space_cond = SDL_CreateCondition();
    if (!P.lock || !P.data_cond || !P.space_cond) {
        LOG("player: could not create locks: %s", SDL_GetError());
        return -1;
    }
    if (sb_init(&P.sb, ring_capacity, ring_capacity / 8) != 0) {
        LOG("player: could not allocate %u byte buffer", (unsigned)ring_capacity);
        return -1;
    }
    if (decode_init() != 0)
        return -1;
    LOG("player: ring %u KB, %s", (unsigned)(ring_capacity / 1024),
        curl_version());
    find_ca_bundle();
    return 0;
}

static void start_session(const char *url, size_t prebuffer)
{
    snprintf(P.url, sizeof(P.url), "%s", url);
    sb_reset(&P.sb);
    P.base_prebuffer = prebuffer;
    sb_set_prebuffer(&P.sb, prebuffer);
    P.stop = P.user_stop = 0;
    P.net_done = P.dec_done = 0;
    P.connected = 0;
    P.http_status = 0;
    P.content_type[0] = P.icy_name[0] = P.error[0] = '\0';
    P.title[0] = P.reconnect_why[0] = '\0';
    P.artist[0] = P.song[0] = P.icy_url[0] = '\0';
    P.reconnecting = 0;
    P.rate = 0;
    P.channels = P.bitrate_kbps = 0;
    P.codec[0] = '\0';
    P.codec_hint = CODEC_MP3;
    P.hls = 0;
    P.decode_us = P.samples_out = 0;

    LOG("player: start %s (prebuffer %u KB)", url, (unsigned)(prebuffer / 1024));
    /* The caller holds the lock, so the threads can't run ahead of the
     * session state set here. */
    P.net_thid = SDL_CreateThread(net_thread, "dxg_net", NULL);
    P.dec_thid = SDL_CreateThread(dec_thread, "dxg_decode", NULL);
    if (!P.net_thid || !P.dec_thid) {
        LOG("player: thread create failed: %s", SDL_GetError());
        snprintf(P.error, sizeof(P.error), "Could not create player threads");
        /* Whichever thread did start sees P.stop and winds down; the
         * missing one counts as done so player_update() can reap. */
        P.stop = 1;
        P.net_done = !P.net_thid;
        P.dec_done = !P.dec_thid;
        P.active = P.net_thid || P.dec_thid;
        return;
    }
    P.active = 1;
}

static void request_stop(void)
{
    P.stop = 1;
    P.user_stop = 1;
    SDL_BroadcastCondition(P.data_cond);
    SDL_BroadcastCondition(P.space_cond);
}

void player_start(const char *url, size_t prebuffer)
{
    lock();
    if (P.active) {
        /* Queue it; player_update() starts it once the old threads exit. */
        LOG("player: switch requested");
        snprintf(P.pending_url, sizeof(P.pending_url), "%s", url);
        P.pending_prebuffer = prebuffer;
        request_stop();
    } else {
        start_session(url, prebuffer);
    }
    unlock();
}

void player_stop(void)
{
    lock();
    P.pending_url[0] = '\0';
    if (P.active) {
        LOG("player: stop requested");
        request_stop();
    }
    unlock();
}

void player_set_prebuffer(size_t prebuffer)
{
    lock();
    P.base_prebuffer = prebuffer;
    apply_prebuffer();
    SDL_BroadcastCondition(P.data_cond);
    unlock();
}

void player_set_volume(int pct)
{
    SDL_SetAtomicInt(&volume_pct, pct < 0 ? 0 : pct > 100 ? 100 : pct);
}

void player_update(void)
{
    SDL_Thread *net, *dec;

    lock();
    if (!P.active || !P.net_done || !P.dec_done) {
        unlock();
        return;
    }
    net = P.net_thid;
    dec = P.dec_thid;
    unlock();

    /* Both threads have set their done flag, so these return at once. */
    if (net)
        SDL_WaitThread(net, NULL);
    if (dec)
        SDL_WaitThread(dec, NULL);

    lock();
    P.active = 0;
    if (P.user_stop)
        P.error[0] = '\0';
    LOG("player: session ended");
    if (P.pending_url[0]) {
        char url[512];
        snprintf(url, sizeof(url), "%s", P.pending_url);
        P.pending_url[0] = '\0';
        start_session(url, P.pending_prebuffer);
    }
    unlock();
}

void player_get_status(player_status *s)
{
    memset(s, 0, sizeof(*s));
    lock();
    if (!P.active)
        s->state = P.error[0] ? PLAYER_ERROR : PLAYER_IDLE;
    else if (P.stop)
        s->state = P.user_stop ? PLAYER_STOPPING : PLAYER_ERROR;
    else if (P.reconnecting)
        s->state = PLAYER_RECONNECTING;
    else if (!P.connected)
        s->state = PLAYER_CONNECTING;
    else if (sb_get_state(&P.sb) == SB_BUFFERING)
        s->state = PLAYER_BUFFERING;
    else
        s->state = PLAYER_PLAYING;

    s->prebuffer_pct = sb_prebuffer_percent(&P.sb);
    s->fill_pct = sb_fill_percent(&P.sb);
    s->fill = sb_fill(&P.sb);
    s->capacity = sb_capacity(&P.sb);
    s->prebuffer = P.sb.prebuffer;
    s->underruns = P.sb.underruns;
    s->bytes_in = P.sb.total_in;
    s->http_status = P.http_status;
    memcpy(s->content_type, P.content_type, sizeof(s->content_type));
    memcpy(s->icy_name, P.icy_name, sizeof(s->icy_name));
    s->rate = P.rate;
    s->channels = P.channels;
    s->bitrate_kbps = P.bitrate_kbps;
    memcpy(s->codec, P.codec, sizeof(s->codec));
    if (P.samples_out && P.rate)
        s->decode_load_pct = (unsigned)(P.decode_us * P.rate / 10000 / P.samples_out);
    memcpy(s->error, P.error, sizeof(s->error));
    memcpy(s->title, P.title, sizeof(s->title));
    memcpy(s->artist, P.artist, sizeof(s->artist));
    memcpy(s->song, P.song, sizeof(s->song));
    memcpy(s->icy_url, P.icy_url, sizeof(s->icy_url));
    s->reconnect_attempt = P.reconnecting;
    s->reconnect_max = RECONNECT_MAX_ATTEMPTS;
    {
        uint64_t now = SDL_GetTicksNS();
        s->retry_in_s = P.reconnecting && P.retry_at > now
                            ? (unsigned)((P.retry_at - now + 999999999) / 1000000000) : 0;
    }
    memcpy(s->reconnect_why, P.reconnect_why, sizeof(s->reconnect_why));
    unlock();
}

void player_shutdown(void)
{
    int i;

    player_stop();
    /* Give the threads up to 3 s; a stuck DNS lookup can't be interrupted. */
    for (i = 0; i < 300; i++) {
        player_update();
        lock();
        if (!P.active) {
            unlock();
            break;
        }
        unlock();
        SDL_Delay(10);
    }
    decode_exit();
}
