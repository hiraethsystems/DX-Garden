/*
 * Audio out of HLS segments, as a plain stream the decoders take:
 *
 *   - MPEG-TS: the first audio stream's bytes (ADTS AAC, LOAS AAC or MPEG
 *     audio), PES headers removed.
 *   - Packed audio (.aac/.mp3 segments): the leading ID3 tags removed.
 *   - Fragmented MP4 (CMAF): AAC samples with an ADTS header added to each,
 *     or MP3 frames as they are.
 *
 * Portable, no allocation: whole segments in, bytes out through a callback.
 */
#ifndef DXG_MEDIA_DEMUX_H
#define DXG_MEDIA_DEMUX_H

#include <stddef.h>

typedef void (*es_out)(void *ud, const unsigned char *p, size_t n);

typedef enum { SEG_UNKNOWN, SEG_TS, SEG_PACKED, SEG_FMP4 } segment_kind;

segment_kind segment_sniff(const unsigned char *p, size_t n);

/* Bytes taken up by ID3v2 tags at the start of p (there may be several). */
size_t id3_skip(const unsigned char *p, size_t n);

/* ---- MPEG-TS -------------------------------------------------------- */

typedef struct {
    int pmt_pid;     /* -1 until the PAT is seen */
    int audio_pid;   /* -1 until the PMT names an audio stream */
    int audio_type;  /* its stream_type: 0x0F ADTS, 0x11 LOAS, 0x03/0x04 MPEG audio */
    int unsupported; /* stream_type of an audio stream we can't play (AC-3...), or 0 */
    int pes_skip;    /* PES header bytes still to skip in the next packets */
    /* Timed ID3 metadata (stream_type 0x15), whole tags through meta_out
     * if the caller sets it. */
    int id3_pid;
    es_out meta_out;
    void *meta_ud;
    unsigned char meta[4096];
    size_t meta_n;
    int meta_skip;
} ts_demux;

void ts_init(ts_demux *t);
/* Feeds whole 188-byte packets (a segment); the state carries over to the
 * next segment. Returns the number of audio bytes written. Metadata tags
 * are handed out when complete, at the latest at the end of the call. */
size_t ts_feed(ts_demux *t, const unsigned char *p, size_t n, es_out out, void *ud);

/* ---- fragmented MP4 ------------------------------------------------- */

typedef struct {
    int ok;               /* an audio track was found */
    unsigned track_id;
    unsigned oti;         /* MPEG-4 object type: 0x40 AAC, 0x69/0x6B MP3 */
    unsigned default_size;
    /* For the ADTS headers: the core AAC object type, sampling frequency
     * index and channel configuration from the AudioSpecificConfig. */
    unsigned aot, freq_index, channels;
    char codec[8];        /* the sample entry's type: "mp4a", "enca", "Opus", ... */
} mp4_audio;

/* Reads the initialisation section (ftyp + moov). Returns 0 if it has an
 * AAC or MP3 audio track. */
int mp4_init(mp4_audio *m, const unsigned char *p, size_t n);
/* A media segment (styp/moof/mdat...): the track's samples through out.
 * Returns the number of samples. */
size_t mp4_fragment(const mp4_audio *m, const unsigned char *p, size_t n, es_out out, void *ud);

/* The 7-byte ADTS header for an AAC frame of `payload` bytes. */
void adts_header(unsigned char h[7], unsigned aot, unsigned freq_index, unsigned channels,
                 size_t payload);

#endif
