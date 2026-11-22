/*
 * The audio decoders: MP3 (mpg123), AAC and HE-AAC (faad2), Ogg Vorbis
 * (libvorbisfile), Opus (opusfile) and FLAC, native or in Ogg (libFLAC).
 *
 * Each runs on the player's decode thread, pulling compressed bytes
 * through `read` (which blocks until data comes, and returns 0 at the end
 * of the stream or on stop) and handing out 16-bit stereo through `emit`.
 * Mono is doubled to both sides; more than two channels are mixed down to
 * the front pair.
 */
#ifndef DXG_DECODE_H
#define DXG_DECODE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    CODEC_MP3,
    CODEC_AAC,  /* ADTS or LOAS/LATM */
    CODEC_OGG,  /* Vorbis, Opus or FLAC inside, decided from the first page */
    CODEC_FLAC  /* native FLAC */
} codec_id;

typedef struct {
    char codec[24]; /* "MP3", "HE-AAC v2", "Opus", ... */
    long rate;      /* output rate (with HE-AAC, the doubled one) */
    int channels;   /* the stream's channels, before mixing to stereo */
    int bitrate_kbps; /* 0 if unknown */
} dec_info;

typedef struct {
    void *ud;
    /* Up to n bytes into buf; blocks until there is data. 0: end or stop. */
    size_t (*read)(void *ud, void *buf, size_t n);
    /* `frames` stereo frames at `rate`. Returns 0 to stop decoding. */
    int (*emit)(void *ud, const int16_t *pcm, size_t frames, long rate);
    void (*info)(void *ud, const dec_info *info);
    /* Artist and title from the stream's own tags (Ogg and FLAC comments);
     * either may be "". */
    void (*tags)(void *ud, const char *artist, const char *title);
} dec_io;

/* One-time library set-up and tear-down (mpg123). */
int decode_init(void);
void decode_exit(void);

/* Decodes until the stream ends, emit asks to stop, or an error. Returns
 * 0, or -1 with a message in err. `head` (n bytes) is the start of the
 * stream, already read through `read`; it is decoded first. */
int decode_run(codec_id codec, const unsigned char *head, size_t n, const dec_io *io,
               char *err, size_t errn);

const char *codec_name(codec_id codec);

#endif
