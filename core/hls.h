/*
 * HTTP Live Streaming playlists (RFC 8216): a master playlist lists
 * versions of a stream at different bitrates; a media playlist lists the
 * current segments of one version, which a live stream keeps appending to.
 *
 * Only what an audio player needs: variants with their bandwidth and
 * codecs, an alternative audio rendition, segments with their sequence
 * numbers, the fMP4 initialisation section (EXT-X-MAP), the end marker, and
 * whether the segments are encrypted.
 */
#ifndef DXG_HLS_H
#define DXG_HLS_H

#include <stddef.h>

typedef struct {
    char *uri;
    long bandwidth;  /* bits per second, 0 if not given */
    int audio_only;  /* CODECS lists audio codecs only */
    int has_codecs;  /* a CODECS attribute was given */
} hls_variant;

typedef struct {
    char *uri;
    char *map;       /* its EXT-X-MAP URI (fMP4), or NULL */
    double duration; /* seconds */
    long long seq;   /* media sequence number */
} hls_segment;

typedef struct {
    int is_master;
    hls_variant *variants;
    int nvariants;
    char *audio_uri; /* EXT-X-MEDIA TYPE=AUDIO with a URI (the default one if marked) */

    hls_segment *segs;
    int nsegs;
    double target_duration;
    int endlist;
    int encrypted; /* EXT-X-KEY with a METHOD other than NONE */
} hls_playlist;

/* Parses a playlist body. Returns 0, or -1 if it isn't an HLS playlist
 * (no #EXTM3U) or memory runs out. Free with hls_free either way. */
int hls_parse(const char *text, size_t len, hls_playlist *pl);
void hls_free(hls_playlist *pl);

/* The variant to play from a master playlist: the highest bandwidth up to
 * `max_bps` among audio-only ones, else the lowest audio-only one above it;
 * with no audio-only variants, the lowest bandwidth one (its segments carry
 * the audio too). -1 if there are none. */
int hls_pick_variant(const hls_playlist *pl, long max_bps);

/* `ref` resolved against the URL `base` (RFC 3986, enough for playlists:
 * absolute, scheme-relative, host-relative and path-relative references,
 * with "." and ".." segments). Returns 0, or -1 if it doesn't fit. */
int url_resolve(const char *base, const char *ref, char *out, size_t n);

#endif
