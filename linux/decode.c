#include "decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <FLAC/stream_decoder.h>
#include <mpg123.h>
#include <neaacdec.h>
#include <opusfile.h>
#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>

#include "log.h"
#include "stream_info.h"

#define OUT_FRAMES 2048 /* stereo frames per emit */

/* The compressed bytes: the head the caller already read, then io->read. */
typedef struct {
    const dec_io *io;
    const unsigned char *head;
    size_t hn, hpos;
    int eof;
    uint64_t bytes; /* compressed bytes handed to the decoder */
} src;

static size_t src_read(src *s, void *buf, size_t n)
{
    size_t k;
    if (s->hpos < s->hn) {
        k = s->hn - s->hpos < n ? s->hn - s->hpos : n;
        memcpy(buf, s->head + s->hpos, k);
        s->hpos += k;
    } else if (s->eof) {
        return 0;
    } else {
        k = s->io->read(s->io->ud, buf, n);
        if (!k)
            s->eof = 1;
    }
    s->bytes += k;
    return k;
}

static int16_t out[OUT_FRAMES * 2];

/* Interleaved 16-bit audio with `ch` channels to stereo: mono doubled, more
 * than two channels reduced to the front pair (l, r are their indexes). */
static int emit_s16(const dec_io *io, const int16_t *in, int ch, size_t frames, int l, int r,
                    long rate)
{
    while (frames) {
        size_t k = frames < OUT_FRAMES ? frames : OUT_FRAMES, i;
        if (ch == 2) {
            memcpy(out, in, k * 4);
        } else {
            for (i = 0; i < k; i++) {
                out[2 * i] = in[i * (size_t)ch + (size_t)l];
                out[2 * i + 1] = in[i * (size_t)ch + (size_t)r];
            }
        }
        if (!io->emit(io->ud, out, k, rate))
            return 0;
        in += k * (size_t)ch;
        frames -= k;
    }
    return 1;
}

/* The front pair's indexes for Vorbis/Opus channel order (L, C, R, ...
 * from three channels up; quad is FL, FR, RL, RR). */
static void vorbis_front(int ch, int *l, int *r)
{
    *l = 0;
    *r = ch == 1 ? 0 : ch == 2 || ch == 4 ? 1 : 2;
}

static void report_info(const dec_io *io, const char *codec, long rate, int ch, int kbps)
{
    dec_info di;
    memset(&di, 0, sizeof(di));
    snprintf(di.codec, sizeof(di.codec), "%s", codec);
    di.rate = rate;
    di.channels = ch;
    di.bitrate_kbps = kbps;
    if (io->info)
        io->info(io->ud, &di);
}

/* Average bitrate: compressed bytes decoded per second of audio. */
typedef struct {
    uint64_t bytes, frames;
    long rate;
} rate_meter;

static int meter_kbps(const rate_meter *m)
{
    if (!m->frames || !m->rate)
        return 0;
    return (int)(m->bytes * 8 * (uint64_t)m->rate / m->frames / 1000);
}

/* ---- MP3 --------------------------------------------------------------- */

static int run_mp3(src *s, char *err, size_t errn)
{
    static unsigned char in[4096];
    static int16_t pcm[OUT_FRAMES * 2];
    mpg123_handle *mh;
    const long *rates;
    size_t nrates, i;
    int e, rc = 0, ch = 2;
    long rate = 0;
    uint64_t frames = 0;

    mh = mpg123_new(NULL, &e);
    if (!mh) {
        snprintf(err, errn, "MP3 decoder init failed: %s", mpg123_plain_strerror(e));
        return -1;
    }
    /* Always 16-bit stereo out; mono streams get duplicated channels. */
    mpg123_param(mh, MPG123_FLAGS, MPG123_FORCE_STEREO | MPG123_QUIET, 0);
    mpg123_format_none(mh);
    mpg123_rates(&rates, &nrates);
    for (i = 0; i < nrates; i++)
        mpg123_format(mh, rates[i], MPG123_STEREO, MPG123_ENC_SIGNED_16);
    mpg123_open_feed(mh);

    for (;;) {
        size_t n = src_read(s, in, sizeof(in));
        int ret;
        if (!n)
            break;
        if (mpg123_feed(mh, in, n) != MPG123_OK) {
            snprintf(err, errn, "MP3 decoder: %s", mpg123_strerror(mh));
            rc = -1;
            break;
        }
        for (;;) {
            size_t done = 0;
            ret = mpg123_read(mh, (unsigned char *)pcm, sizeof(pcm), &done);
            if (ret == MPG123_NEW_FORMAT) {
                int enc;
                struct mpg123_frameinfo fi;
                mpg123_getformat(mh, &rate, &ch, &enc);
                LOG("decoder: MP3, %ld Hz", rate);
                if (mpg123_info(mh, &fi) == MPG123_OK)
                    report_info(s->io, fi.layer == 2 ? "MP2" : fi.layer == 1 ? "MP1" : "MP3",
                                fi.rate, fi.mode == MPG123_M_MONO ? 1 : 2, fi.bitrate);
                continue;
            }
            if (ret == MPG123_ERR) {
                snprintf(err, errn, "MP3 decoder: %s", mpg123_strerror(mh));
                rc = -1;
                break;
            }
            if (done && rate) {
                if (!s->io->emit(s->io->ud, pcm, done / 4, rate))
                    goto out;
                frames += done / 4;
            }
            if (ret == MPG123_NEED_MORE || (!done && ret != MPG123_OK))
                break;
        }
        if (rc)
            break;
        /* Every ~2 s of audio, the stream's own details. */
        if (frames >= (uint64_t)(rate ? rate : 44100) * 2) {
            struct mpg123_frameinfo fi;
            frames = 0;
            if (mpg123_info(mh, &fi) == MPG123_OK)
                report_info(s->io, fi.layer == 2 ? "MP2" : fi.layer == 1 ? "MP1" : "MP3",
                            fi.rate, fi.mode == MPG123_M_MONO ? 1 : 2, fi.bitrate);
        }
    }
out:
    mpg123_delete(mh);
    return rc;
}

/* ---- AAC (faad2) ------------------------------------------------------- */

#define AAC_BUF (64 * 1024)
#define AAC_MIN (16 * 1024) /* kept in the buffer: several whole frames */

static int run_aac(src *s, char *err, size_t errn)
{
    static unsigned char buf[AAC_BUF];
    size_t len = 0, pos = 0, at;
    NeAACDecHandle h = NULL;
    NeAACDecConfigurationPtr cfg;
    unsigned long rate0;
    unsigned char ch0;
    int rc = 0, errors = 0, have_info = 0;
    unsigned char last_sbr = 255, last_ps = 255, last_ch = 0;
    unsigned long last_rate = 0;
    rate_meter m = { 0, 0, 0 };
    char last_err[96] = "";

#define FILL()                                                                     \
    do {                                                                           \
        if (pos) {                                                                 \
            memmove(buf, buf + pos, len - pos);                                    \
            len -= pos;                                                            \
            pos = 0;                                                               \
        }                                                                          \
        while (len < AAC_MIN && !s->eof) {                                         \
            size_t k_ = src_read(s, buf + len, AAC_BUF - len);                     \
            len += k_;                                                             \
        }                                                                          \
    } while (0)

    FILL();
    /* Start at the first whole frame (the stream may join mid-frame). */
    if (audio_sniff_at(buf, len, &at) == AF_UNKNOWN) {
        snprintf(err, errn, "No AAC frames found in the stream");
        return -1;
    }
    pos = at;

    h = NeAACDecOpen();
    cfg = NeAACDecGetCurrentConfiguration(h);
    cfg->outputFormat = FAAD_FMT_16BIT;
    cfg->downMatrix = 1; /* 5.1 to stereo */
    cfg->defObjectType = LC;
    NeAACDecSetConfiguration(h, cfg);
    {
        long r = NeAACDecInit(h, buf + pos, (unsigned long)(len - pos), &rate0, &ch0);
        if (r < 0) {
            snprintf(err, errn, "The AAC decoder didn't recognise the stream");
            NeAACDecClose(h);
            return -1;
        }
        pos += (size_t)r;
    }
    LOG("decoder: AAC, %lu Hz, %u ch at start", rate0, ch0);

    for (;;) {
        NeAACDecFrameInfo fi;
        void *pcm;

        if (len - pos < AAC_MIN && !s->eof)
            FILL();
        if (len - pos < 7)
            break; /* the end */
        pcm = NeAACDecDecode(h, &fi, buf + pos, (unsigned long)(len - pos));
        if (fi.error) {
            const char *msg = NeAACDecGetErrorMessage(fi.error);
            snprintf(last_err, sizeof(last_err), "%s", msg ? msg : "error");
            if (++errors > 200) {
                snprintf(err, errn, "AAC decoder: %s", last_err);
                rc = -1;
                break;
            }
            if (errors == 1)
                LOG("decoder: AAC error (%s); finding the next frame", last_err);
            /* Skip to the next place two frames line up. */
            pos += 1;
            if (audio_sniff_at(buf + pos, len - pos, &at) != AF_UNKNOWN)
                pos += at;
            else
                pos = len > 7 ? len - 7 : len;
            continue;
        }
        errors = 0;
        if (!fi.bytesconsumed && !fi.samples) {
            if (s->eof)
                break;
            FILL();
            continue;
        }
        pos += fi.bytesconsumed;
        m.bytes += fi.bytesconsumed;
        if (fi.samples && fi.channels) {
            size_t frames = fi.samples / fi.channels;
            int l = 0, r = fi.channels == 1 ? 0 : 1;
            if (fi.sbr != last_sbr || fi.ps != last_ps || fi.channels != last_ch ||
                fi.samplerate != last_rate) {
                const int he = fi.sbr == SBR_UPSAMPLED || fi.sbr == SBR_DOWNSAMPLED;
                last_sbr = fi.sbr;
                last_ps = fi.ps;
                last_ch = fi.channels;
                last_rate = fi.samplerate;
                LOG("decoder: %s, %lu Hz, %u ch", fi.ps ? "HE-AAC v2" : he ? "HE-AAC" : "AAC",
                    fi.samplerate, fi.channels);
                m.rate = (long)fi.samplerate;
                m.frames = 0;
                m.bytes = fi.bytesconsumed;
                have_info = 0;
            }
            /* After downmixing, faad2 hands out stereo for 5.1. */
            if (!emit_s16(s->io, (const int16_t *)pcm, fi.channels, frames, l, r,
                          (long)fi.samplerate))
                break;
            m.frames += frames;
            if (!have_info || m.frames % ((uint64_t)fi.samplerate * 4) < frames) {
                const int he = fi.sbr == SBR_UPSAMPLED || fi.sbr == SBR_DOWNSAMPLED;
                report_info(s->io, fi.ps ? "HE-AAC v2" : he ? "HE-AAC" : "AAC",
                            (long)fi.samplerate, fi.channels,
                            m.frames >= fi.samplerate ? meter_kbps(&m) : 0);
                have_info = 1;
            }
        }
    }
#undef FILL
    NeAACDecClose(h);
    return rc;
}

/* ---- Ogg Vorbis (vorbisfile) ------------------------------------------- */

static size_t vf_read(void *ptr, size_t size, size_t nmemb, void *ds)
{
    return src_read(ds, ptr, size * nmemb) / (size ? size : 1);
}

static void comment_tags(const dec_io *io, char **comments, int n)
{
    char artist[128] = "", title[192] = "";
    int i;
    for (i = 0; i < n; i++) {
        const char *c = comments[i];
        if (!c)
            continue;
        if (!artist[0] && strncasecmp(c, "ARTIST=", 7) == 0)
            snprintf(artist, sizeof(artist), "%s", c + 7);
        else if (!title[0] && strncasecmp(c, "TITLE=", 6) == 0)
            snprintf(title, sizeof(title), "%s", c + 6);
    }
    if (io->tags)
        io->tags(io->ud, artist, title);
}

static int run_vorbis(src *s, char *err, size_t errn)
{
    static int16_t pcm[4096 * 2];
    ov_callbacks cb = { vf_read, NULL, NULL, NULL };
    OggVorbis_File vf;
    int rc = 0, link = -1, holes = 0;
    long rate = 0;
    int ch = 0, l = 0, r = 1;

    if (ov_open_callbacks(s, &vf, NULL, 0, cb) < 0) {
        snprintf(err, errn, "The Ogg Vorbis stream couldn't be opened");
        return -1;
    }
    for (;;) {
        int bs = 0;
        long n = ov_read(&vf, (char *)pcm, sizeof(pcm), 0, 2, 1, &bs);
        if (n == OV_HOLE) {
            if (++holes > 1000)
                break;
            continue;
        }
        if (n < 0) {
            snprintf(err, errn, "Ogg Vorbis decoder: error %ld", n);
            rc = -1;
            break;
        }
        if (n == 0)
            break;
        holes = 0;
        if (bs != link) {
            vorbis_info *vi = ov_info(&vf, bs);
            vorbis_comment *vc = ov_comment(&vf, bs);
            link = bs;
            rate = vi ? vi->rate : 44100;
            ch = vi ? vi->channels : 2;
            vorbis_front(ch, &l, &r);
            LOG("decoder: Ogg Vorbis, %ld Hz, %d ch", rate, ch);
            report_info(s->io, "Ogg Vorbis", rate, ch,
                        vi && vi->bitrate_nominal > 0 ? (int)(vi->bitrate_nominal / 1000) : 0);
            if (vc)
                comment_tags(s->io, vc->user_comments, vc->comments);
        }
        if (ch > 0 && !emit_s16(s->io, pcm, ch, (size_t)n / 2 / (size_t)ch, l, r, rate))
            break;
    }
    ov_clear(&vf);
    return rc;
}

/* ---- Opus (opusfile) --------------------------------------------------- */

static int of_read(void *stream, unsigned char *ptr, int nbytes)
{
    return nbytes > 0 ? (int)src_read(stream, ptr, (size_t)nbytes) : 0;
}

static int run_opus(src *s, char *err, size_t errn)
{
    static opus_int16 pcm[OUT_FRAMES * 2];
    OpusFileCallbacks cb = { of_read, NULL, NULL, NULL };
    OggOpusFile *of;
    int e = 0, rc = 0, link = -1, holes = 0;

    of = op_open_callbacks(s, &cb, NULL, 0, &e);
    if (!of) {
        snprintf(err, errn, "The Opus stream couldn't be opened (error %d)", e);
        return -1;
    }
    for (;;) {
        int n = op_read_stereo(of, pcm, OUT_FRAMES * 2), li;
        if (n == OP_HOLE) {
            if (++holes > 1000)
                break;
            continue;
        }
        if (n < 0) {
            snprintf(err, errn, "Opus decoder: error %d", n);
            rc = -1;
            break;
        }
        if (n == 0)
            break;
        holes = 0;
        li = op_current_link(of);
        if (li != link) {
            const OpusHead *hd = op_head(of, li);
            const OpusTags *t = op_tags(of, li);
            opus_int32 br = op_bitrate_instant(of);
            link = li;
            LOG("decoder: Opus, %d ch", hd ? hd->channel_count : 0);
            report_info(s->io, "Opus", 48000, hd ? hd->channel_count : 2,
                        br > 0 ? (int)(br / 1000) : 0);
            if (t)
                comment_tags(s->io, t->user_comments, t->comments);
        }
        if (!s->io->emit(s->io->ud, pcm, (size_t)n, 48000))
            break;
    }
    op_free(of);
    return rc;
}

/* ---- FLAC (libFLAC), native or in Ogg ---------------------------------- */

typedef struct {
    src *s;
    int stopped;
    int bps, ch;
    long rate;
    unsigned errors;
    uint64_t frames, bytes0; /* since the format last changed, for the bitrate */
} flac_ctx;

static FLAC__StreamDecoderReadStatus flac_read(const FLAC__StreamDecoder *d, FLAC__byte buffer[],
                                               size_t *bytes, void *ud)
{
    flac_ctx *c = ud;
    (void)d;
    *bytes = *bytes ? src_read(c->s, buffer, *bytes) : 0;
    return *bytes ? FLAC__STREAM_DECODER_READ_STATUS_CONTINUE
                  : FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
}

static FLAC__StreamDecoderWriteStatus flac_write(const FLAC__StreamDecoder *d,
                                                 const FLAC__Frame *frame,
                                                 const FLAC__int32 *const buffer[], void *ud)
{
    flac_ctx *c = ud;
    unsigned ch = frame->header.channels, bps = frame->header.bits_per_sample, i = 0;
    unsigned n = frame->header.blocksize;
    const FLAC__int32 *L = buffer[0], *R = buffer[ch > 1 ? 1 : 0];
    long rate = (long)frame->header.sample_rate;
    (void)d;

    if (rate != c->rate || (int)ch != c->ch || (int)bps != c->bps) {
        c->rate = rate;
        c->ch = (int)ch;
        c->bps = (int)bps;
        LOG("decoder: FLAC, %ld Hz, %u ch, %u bit", rate, ch, bps);
        report_info(c->s->io, "FLAC", rate, (int)ch, 0);
        c->frames = 0;
        c->bytes0 = c->s->bytes;
    }
    /* The bitrate, every ~4 s of audio, from the compressed bytes read. */
    c->frames += n;
    if (rate > 0 && c->frames >= (uint64_t)rate &&
        c->frames % ((uint64_t)rate * 4) < n)
        report_info(c->s->io, "FLAC", rate, (int)ch,
                    (int)((c->s->bytes - c->bytes0) * 8 * (uint64_t)rate / c->frames / 1000));
    while (i < n) {
        unsigned k = n - i < OUT_FRAMES ? n - i : OUT_FRAMES, j;
        for (j = 0; j < k; j++) {
            FLAC__int32 a = L[i + j], b = R[i + j];
            if (bps > 16) {
                a >>= bps - 16;
                b >>= bps - 16;
            } else if (bps < 16) {
                a *= 1 << (16 - bps);
                b *= 1 << (16 - bps);
            }
            out[2 * j] = (int16_t)a;
            out[2 * j + 1] = (int16_t)b;
        }
        if (!c->s->io->emit(c->s->io->ud, out, k, rate)) {
            c->stopped = 1;
            return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
        }
        i += k;
    }
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void flac_meta(const FLAC__StreamDecoder *d, const FLAC__StreamMetadata *md, void *ud)
{
    flac_ctx *c = ud;
    (void)d;
    if (md->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
        const FLAC__StreamMetadata_VorbisComment *vc = &md->data.vorbis_comment;
        char *list[64];
        char store[64][256];
        unsigned i, n = vc->num_comments < 64 ? vc->num_comments : 64;
        for (i = 0; i < n; i++) {
            size_t len = vc->comments[i].length < 255 ? vc->comments[i].length : 255;
            memcpy(store[i], vc->comments[i].entry, len);
            store[i][len] = '\0';
            list[i] = store[i];
        }
        comment_tags(c->s->io, list, (int)n);
    }
}

static void flac_error(const FLAC__StreamDecoder *d, FLAC__StreamDecoderErrorStatus st, void *ud)
{
    flac_ctx *c = ud;
    (void)d;
    if (c->errors++ == 0)
        LOG("decoder: FLAC: %s", FLAC__StreamDecoderErrorStatusString[st]);
}

static int run_flac(src *s, int ogg, char *err, size_t errn)
{
    FLAC__StreamDecoder *d = FLAC__stream_decoder_new();
    FLAC__StreamDecoderInitStatus is;
    flac_ctx c;
    int rc = 0;

    if (!d) {
        snprintf(err, errn, "FLAC decoder init failed");
        return -1;
    }
    memset(&c, 0, sizeof(c));
    c.s = s;
    FLAC__stream_decoder_set_metadata_respond(d, FLAC__METADATA_TYPE_VORBIS_COMMENT);
    is = ogg ? FLAC__stream_decoder_init_ogg_stream(d, flac_read, NULL, NULL, NULL, NULL,
                                                    flac_write, flac_meta, flac_error, &c)
             : FLAC__stream_decoder_init_stream(d, flac_read, NULL, NULL, NULL, NULL, flac_write,
                                                flac_meta, flac_error, &c);
    if (is != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        snprintf(err, errn, "FLAC decoder: %s", FLAC__StreamDecoderInitStatusString[is]);
        FLAC__stream_decoder_delete(d);
        return -1;
    }
    if (!FLAC__stream_decoder_process_until_end_of_stream(d) && !c.stopped) {
        FLAC__StreamDecoderState st = FLAC__stream_decoder_get_state(d);
        if (st != FLAC__STREAM_DECODER_END_OF_STREAM && !s->eof) {
            snprintf(err, errn, "FLAC decoder: %s", FLAC__StreamDecoderStateString[st]);
            rc = -1;
        }
    }
    FLAC__stream_decoder_delete(d);
    return rc;
}

/* ---- public ------------------------------------------------------------ */

int decode_init(void)
{
    int err = mpg123_init();
    if (err != MPG123_OK) {
        LOG("decoder: mpg123_init failed: %s", mpg123_plain_strerror(err));
        return -1;
    }
    return 0;
}

void decode_exit(void)
{
    mpg123_exit();
}

const char *codec_name(codec_id codec)
{
    switch (codec) {
    case CODEC_MP3: return "MP3";
    case CODEC_AAC: return "AAC";
    case CODEC_OGG: return "Ogg";
    case CODEC_FLAC: return "FLAC";
    }
    return "?";
}

int decode_run(codec_id codec, const unsigned char *head, size_t n, const dec_io *io, char *err,
               size_t errn)
{
    src s;

    memset(&s, 0, sizeof(s));
    s.io = io;
    s.head = head;
    s.hn = n;
    err[0] = '\0';
    switch (codec) {
    case CODEC_MP3:
        return run_mp3(&s, err, errn);
    case CODEC_AAC:
        return run_aac(&s, err, errn);
    case CODEC_FLAC:
        return run_flac(&s, 0, err, errn);
    case CODEC_OGG: {
        /* Ogg streams start on a page with the codec's header in it. */
        size_t i = 0;
        while (i + 4 <= n && memcmp(head + i, "OggS", 4) != 0)
            i++;
        s.hpos = i < n ? i : 0;
        switch (ogg_sniff(head + s.hpos, n - s.hpos)) {
        case OGG_VORBIS: return run_vorbis(&s, err, errn);
        case OGG_OPUS: return run_opus(&s, err, errn);
        case OGG_FLAC: return run_flac(&s, 1, err, errn);
        case OGG_UNKNOWN: break;
        }
        snprintf(err, errn, "This Ogg stream isn't Vorbis, Opus or FLAC");
        return -1;
    }
    }
    snprintf(err, errn, "Unknown format");
    return -1;
}
