/*
 * What a stream URL returned, and what to do about failures:
 *   - classifying a response (MP3, another audio format, a playlist, HLS,
 *     a web page) from its Content-Type, URL and first bytes,
 *   - the first stream in a playlist (M3U, PLS or ASX),
 *   - plain-language messages for HTTP status codes,
 *   - which failures are worth retrying, and the reconnect back-off.
 */
#ifndef ENSO_STREAM_INFO_H
#define ENSO_STREAM_INFO_H

#include <stddef.h>

typedef enum {
    SK_MP3,      /* play it */
    SK_UNKNOWN,  /* no clear signs either way: try it as MP3 */
    SK_PLAYLIST, /* M3U/PLS/ASX: fetch the stream it points to */
    SK_HLS,      /* HTTP Live Streaming: not supported */
    SK_OTHER,    /* audio in a format Ensō Radio doesn't decode */
    SK_HTML      /* a web page */
} stream_kind;

/* `head` is the start of the body (may be short). For SK_OTHER, the
 * format's name ("AAC", "Ogg", ...) goes to `format`. */
stream_kind stream_classify(const char *content_type, const char *url,
                            const unsigned char *head, size_t n, char *format, size_t fn);

/* The audio format from the first bytes of a stream, whatever its
 * Content-Type says (servers often label AAC as audio/mpeg). Frame-based
 * formats count only when two frames in a row line up, so stray sync bytes
 * inside other data don't fool it. A leading ID3v2 tag is skipped. */
typedef enum {
    AF_UNKNOWN,
    AF_MP3,  /* MPEG audio layer I, II or III */
    AF_ADTS, /* AAC in ADTS frames */
    AF_LOAS, /* AAC in LOAS/LATM frames */
    AF_OGG,  /* an Ogg page (Vorbis, Opus or FLAC inside) */
    AF_FLAC  /* native FLAC */
} audio_format;

audio_format audio_sniff(const unsigned char *p, size_t n);
/* The same, and where the first frame (or page) starts in `at`. */
audio_format audio_sniff_at(const unsigned char *p, size_t n, size_t *at);

/* What an Ogg stream carries, from its first page. */
typedef enum { OGG_UNKNOWN, OGG_VORBIS, OGG_OPUS, OGG_FLAC } ogg_codec;
ogg_codec ogg_sniff(const unsigned char *p, size_t n);

/* First http(s) stream URL in a playlist body. Returns 1 if found, 0 if
 * none, -1 if it is an HLS playlist. */
int playlist_first_url(const char *body, size_t len, char *out, size_t n);

/* "Not found (HTTP 404)" and so on, into buf. Returns buf. */
const char *http_status_message(long code, char *buf, size_t n);
/* 1 if retrying might help (server busy or down), 0 if not (gone,
 * forbidden, needs a login). */
int http_status_retry(long code);

#define RECONNECT_MAX_ATTEMPTS 10
/* Wait before reconnect attempt `attempt` (1, 2, ...): 1, 2, 4, 8, then
 * 15 s. */
unsigned reconnect_delay_ms(int attempt);

#endif
