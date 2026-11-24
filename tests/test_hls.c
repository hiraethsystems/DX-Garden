/* HLS playlists, URL resolution and the segment demuxers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hls.h"
#include "media_demux.h"
#include "stream_info.h"
#include "track_info.h"
#include "test.h"

static void test_playlists(void)
{
    static const char master[] =
        "#EXTM3U\r\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=64000,CODECS=\"mp4a.40.5\"\r\n"
        "low/index.m3u8\r\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=128000,CODECS=\"mp4a.40.2\"\r\n"
        "mid/index.m3u8\r\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=320000,CODECS=\"mp4a.40.2\"\r\n"
        "hi/index.m3u8\r\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=900000,CODECS=\"avc1.42c00d,mp4a.40.2\",RESOLUTION=320x180\r\n"
        "video.m3u8\r\n";
    static const char media[] =
        "\xEF\xBB\xBF#EXTM3U\n"
        "#EXT-X-VERSION:3\n"
        "#EXT-X-TARGETDURATION:10\n"
        "#EXT-X-MEDIA-SEQUENCE:4711\n"
        "#EXT-X-KEY:METHOD=NONE\n"
        "#EXTINF:9.98,\n"
        "seg4711.ts\n"
        "#EXTINF:10.0,title=\"x, y\"\n"
        "  seg4712.ts  \n"
        "\n"
        "#EXT-X-MAP:URI=\"init.mp4\"\n"
        "#EXTINF:10,\n"
        "seg4713.m4s\n";
    static const char keyed[] = "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k\"\n#EXTINF:5,\na.ts\n";
    static const char alt[] = "#EXTM3U\n"
                              "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"en\",URI=\"en.m3u8\"\n"
                              "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"x\",DEFAULT=YES,URI=\"def.m3u8\"\n"
                              "#EXT-X-STREAM-INF:BANDWIDTH=2000000,CODECS=\"avc1.4d401f,mp4a.40.2\",AUDIO=\"a\"\n"
                              "v.m3u8\n";
    static const char nocodecs[] = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=96000\nonly.m3u8\n";
    hls_playlist pl;

    CHECK(hls_parse("not a playlist", 14, &pl) == -1);
    hls_free(&pl);

    CHECK(hls_parse(master, sizeof(master) - 1, &pl) == 0);
    CHECK(pl.is_master && pl.nvariants == 4 && pl.nsegs == 0);
    CHECK(strcmp(pl.variants[0].uri, "low/index.m3u8") == 0 && pl.variants[0].bandwidth == 64000);
    CHECK(pl.variants[0].audio_only && !pl.variants[3].audio_only);
    CHECK(hls_pick_variant(&pl, 320000) == 2);
    CHECK(hls_pick_variant(&pl, 200000) == 1);
    CHECK(hls_pick_variant(&pl, 1000) == 0); /* none fits: the lowest above */
    hls_free(&pl);

    CHECK(hls_parse(media, sizeof(media) - 1, &pl) == 0);
    CHECK(!pl.is_master && pl.nsegs == 3 && !pl.encrypted && !pl.endlist);
    CHECK(pl.target_duration == 10.0);
    CHECK(pl.segs[0].seq == 4711 && pl.segs[2].seq == 4713);
    CHECK(strcmp(pl.segs[1].uri, "seg4712.ts") == 0);
    CHECK(pl.segs[0].duration > 9.9 && pl.segs[0].duration < 10.0);
    CHECK(pl.segs[1].map == NULL && strcmp(pl.segs[2].map, "init.mp4") == 0);
    hls_free(&pl);

    CHECK(hls_parse(keyed, sizeof(keyed) - 1, &pl) == 0 && pl.encrypted);
    hls_free(&pl);

    CHECK(hls_parse(alt, sizeof(alt) - 1, &pl) == 0);
    CHECK(pl.is_master && strcmp(pl.audio_uri, "def.m3u8") == 0);
    CHECK(pl.nvariants == 1 && !pl.variants[0].audio_only);
    CHECK(hls_pick_variant(&pl, 320000) == 0); /* only a video one: the lowest */
    hls_free(&pl);

    CHECK(hls_parse(nocodecs, sizeof(nocodecs) - 1, &pl) == 0);
    CHECK(hls_pick_variant(&pl, 320000) == 0);
    hls_free(&pl);
}

static void test_url_resolve(void)
{
    char o[256];
    const char *b = "https://a.example/live/radio/index.m3u8?token=1";

    CHECK(url_resolve(b, "seg1.ts", o, sizeof(o)) == 0);
    CHECK(strcmp(o, "https://a.example/live/radio/seg1.ts") == 0);
    CHECK(url_resolve(b, "seg1.ts?x=2", o, sizeof(o)) == 0);
    CHECK(strcmp(o, "https://a.example/live/radio/seg1.ts?x=2") == 0);
    CHECK(url_resolve(b, "/other/a.ts", o, sizeof(o)) == 0);
    CHECK(strcmp(o, "https://a.example/other/a.ts") == 0);
    CHECK(url_resolve(b, "../hi/index.m3u8", o, sizeof(o)) == 0);
    CHECK(strcmp(o, "https://a.example/live/hi/index.m3u8") == 0);
    CHECK(url_resolve(b, "./x/../y.ts", o, sizeof(o)) == 0);
    CHECK(strcmp(o, "https://a.example/live/radio/y.ts") == 0);
    CHECK(url_resolve(b, "//cdn.example/s.ts", o, sizeof(o)) == 0);
    CHECK(strcmp(o, "https://cdn.example/s.ts") == 0);
    CHECK(url_resolve(b, "http://c.example/s.ts", o, sizeof(o)) == 0);
    CHECK(strcmp(o, "http://c.example/s.ts") == 0);
    CHECK(url_resolve("http://h.example", "s.ts", o, sizeof(o)) == 0);
    CHECK(strcmp(o, "http://h.example/s.ts") == 0);
    CHECK(url_resolve("http://h.example:8000/a/b.m3u8", "../../../s.ts", o, sizeof(o)) == 0);
    CHECK(strcmp(o, "http://h.example:8000/s.ts") == 0);
    CHECK(url_resolve(b, "?t=2", o, sizeof(o)) == 0);
    CHECK(strcmp(o, "https://a.example/live/radio/index.m3u8?t=2") == 0);
    CHECK(url_resolve(b, "seg.ts", o, 10) == -1);
    CHECK(url_resolve("not a url", "seg.ts", o, sizeof(o)) == -1);
}

/* Collects demuxer output. */
typedef struct {
    unsigned char buf[8192];
    size_t n;
} sink;

static void collect(void *ud, const unsigned char *p, size_t n)
{
    sink *s = ud;
    if (s->n + n <= sizeof(s->buf)) {
        memcpy(s->buf + s->n, p, n);
        s->n += n;
    }
}

/* One TS packet: header, optional adaptation field of `af` bytes, payload. */
static void ts_packet(unsigned char *k, int pid, int pusi, const unsigned char *pl, size_t pn)
{
    size_t room = 184, af = pn < room ? room - pn : 0;
    memset(k, 0xFF, 188);
    k[0] = 0x47;
    k[1] = (unsigned char)((pusi ? 0x40 : 0) | ((pid >> 8) & 0x1F));
    k[2] = (unsigned char)(pid & 0xFF);
    if (af) {
        k[3] = 0x30;
        k[4] = (unsigned char)(af - 1);
        if (af > 1)
            k[5] = 0;
        memcpy(k + 4 + af, pl, pn);
    } else {
        k[3] = 0x10;
        memcpy(k + 4, pl, 184);
    }
}

static void test_ts(void)
{
    /* PAT: program 1 on PID 0x100. */
    static const unsigned char pat[] = { 0x00, 0x00, 0xB0, 0x0D, 0x00, 0x01, 0xC1, 0x00, 0x00,
                                         0x00, 0x01, 0xE1, 0x00, 0, 0, 0, 0 };
    /* PMT: AC-3 on 0x102, ADTS AAC on 0x101. */
    static const unsigned char pmt[] = { 0x00, 0x02, 0xB0, 0x17, 0x00, 0x01, 0xC1, 0x00, 0x00,
                                         0xE1, 0x01, 0xF0, 0x00, 0x81, 0xE1, 0x02, 0xF0, 0x00,
                                         0x0F, 0xE1, 0x01, 0xF0, 0x00, 0,    0,    0,    0 };
    static unsigned char seg[188 * 5];
    unsigned char pes[200], payload[150];
    sink s;
    ts_demux t;
    size_t i;

    for (i = 0; i < sizeof(payload); i++)
        payload[i] = (unsigned char)i;
    /* PES: start code, audio stream id, length, flags, 5-byte PTS header. */
    memcpy(pes, "\x00\x00\x01\xC0\x00\x00\x80\x80\x05\x21\x00\x01\x00\x01", 14);
    memcpy(pes + 14, payload, 100);
    ts_packet(seg, 0, 1, pat, sizeof(pat));
    ts_packet(seg + 188, 0x100, 1, pmt, sizeof(pmt));
    ts_packet(seg + 376, 0x101, 1, pes, 114);
    ts_packet(seg + 564, 0x101, 0, payload + 100, 50);
    ts_packet(seg + 752, 0x102, 1, pes, 114); /* the AC-3 stream: ignored */

    CHECK(segment_sniff(seg, sizeof(seg)) == SEG_TS);
    ts_init(&t);
    s.n = 0;
    CHECK(ts_feed(&t, seg, sizeof(seg), collect, &s) == 150);
    CHECK(t.pmt_pid == 0x100 && t.audio_pid == 0x101 && t.audio_type == 0x0F);
    CHECK(t.unsupported == 0x81);
    CHECK(s.n == 150 && memcmp(s.buf, payload, 150) == 0);
    /* Garbage before a packet: sync is found again. */
    {
        static unsigned char junk[7 + 188 * 2];
        memset(junk, 0x11, 7);
        memcpy(junk + 7, seg + 564, 188);
        memcpy(junk + 7 + 188, seg + 564, 188);
        s.n = 0;
        CHECK(ts_feed(&t, junk, sizeof(junk), collect, &s) == 100);
    }
}

static void test_id3_and_packed(void)
{
    static unsigned char seg[64];
    memset(seg, 0, sizeof(seg));
    /* Two ID3 tags (5 and 3 bytes of body), then ADTS. */
    memcpy(seg, "ID3\x04\x00\x00\x00\x00\x00\x05", 10);
    memcpy(seg + 15, "ID3\x04\x00\x00\x00\x00\x00\x03", 10);
    seg[28] = 0xFF;
    seg[29] = 0xF1;
    CHECK(id3_skip(seg, sizeof(seg)) == 28);
    CHECK(id3_skip(seg + 28, 10) == 0);
    CHECK(id3_skip(seg, 12) == 12); /* a cut-off tag: all of it */
    CHECK(segment_sniff(seg, sizeof(seg)) == SEG_PACKED);
}

/* Big-endian helpers for building MP4 boxes. */
static unsigned char *put32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
    return p + 4;
}

/* Starts a box; returns where its size goes. */
static unsigned char *open_box(unsigned char **p, const char *type)
{
    unsigned char *start = *p;
    *p = put32(*p, 0);
    memcpy(*p, type, 4);
    *p += 4;
    return start;
}

static void close_box(unsigned char *start, unsigned char *end)
{
    put32(start, (unsigned long)(end - start));
}

static void test_mp4(void)
{
    static unsigned char init[512], frag[512];
    unsigned char *p = init, *moov, *trak, *tkhd, *mdia, *hdlr, *minf, *stbl, *stsd, *mp4a,
                  *esds, *mvex, *trex, *moof, *traf, *tfhd, *trun, *mdat, *ft;
    mp4_audio m;
    sink s;
    size_t frames;

    ft = open_box(&p, "ftyp");
    memcpy(p, "iso6\0\0\0\0", 8);
    p += 8;
    close_box(ft, p);
    moov = open_box(&p, "moov");
    trak = open_box(&p, "trak");
    tkhd = open_box(&p, "tkhd");
    memset(p, 0, 20);
    put32(p + 12, 7); /* version 0: track ID at 12 */
    p += 20;
    close_box(tkhd, p);
    mdia = open_box(&p, "mdia");
    hdlr = open_box(&p, "hdlr");
    memset(p, 0, 20);
    memcpy(p + 8, "soun", 4);
    p += 20;
    close_box(hdlr, p);
    minf = open_box(&p, "minf");
    stbl = open_box(&p, "stbl");
    stsd = open_box(&p, "stsd");
    p = put32(p, 0);
    p = put32(p, 1);
    mp4a = open_box(&p, "mp4a");
    memset(p, 0, 28);
    p[16 + 1 - 8] = 0; /* (reserved) */
    p[17] = 2;         /* channel count, at entry offset 24 */
    p += 28;
    esds = open_box(&p, "esds");
    p = put32(p, 0);
    /* ES_Descriptor > DecoderConfigDescriptor (AAC) > the ASC for
     * HE-AAC (AOT 5) at 48 kHz SBR over a 24 kHz AAC-LC core, stereo:
     * 00101 0011 0010 0110 00010 -> 0x2A 0x32 0x62 ... */
    {
        static const unsigned char d[] = { 0x03, 0x19, 0x00, 0x01, 0x00, 0x04, 0x11, 0x40, 0x15,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x05, 0x03, 0x29, 0x93, 0x08 };
        memcpy(p, d, sizeof(d));
        p += sizeof(d);
    }
    close_box(esds, p);
    close_box(mp4a, p);
    close_box(stsd, p);
    close_box(stbl, p);
    close_box(minf, p);
    close_box(mdia, p);
    close_box(trak, p);
    mvex = open_box(&p, "mvex");
    trex = open_box(&p, "trex");
    p = put32(p, 0);
    p = put32(p, 7);
    p = put32(p, 1);
    p = put32(p, 1024);
    p = put32(p, 6); /* default sample size */
    p = put32(p, 0);
    close_box(trex, p);
    close_box(mvex, p);
    close_box(moov, p);

    CHECK(segment_sniff(init, (size_t)(p - init)) == SEG_FMP4);
    CHECK(mp4_init(&m, init, (size_t)(p - init)) == 0);
    CHECK(m.ok && m.track_id == 7 && m.oti == 0x40 && m.default_size == 6);
    CHECK(m.aot == 2 && m.freq_index == 6 && m.channels == 2); /* LC core, 24 kHz */

    /* A fragment: two samples (6 bytes by default, then 4 given). */
    p = frag;
    moof = open_box(&p, "moof");
    traf = open_box(&p, "traf");
    tfhd = open_box(&p, "tfhd");
    p = put32(p, 0x020000); /* default-base-is-moof */
    p = put32(p, 7);
    close_box(tfhd, p);
    trun = open_box(&p, "trun");
    p = put32(p, 0x000201); /* data offset, sample sizes */
    p = put32(p, 2);
    {
        unsigned char *off = p;
        p = put32(p, 0);
        p = put32(p, 6);
        p = put32(p, 4);
        close_box(trun, p);
        close_box(traf, p);
        close_box(moof, p);
        mdat = open_box(&p, "mdat");
        put32(off, (unsigned long)(p - moof));
    }
    memcpy(p, "AAAAAABBBB", 10);
    p += 10;
    close_box(mdat, p);

    s.n = 0;
    frames = mp4_fragment(&m, frag, (size_t)(p - frag), collect, &s);
    CHECK(frames == 2);
    CHECK(s.n == 7 + 6 + 7 + 4);
    CHECK(memcmp(s.buf + 7, "AAAAAA", 6) == 0 && memcmp(s.buf + 20, "BBBB", 4) == 0);
    /* The ADTS headers say what the sniffer and faad2 need. */
    CHECK(s.buf[0] == 0xFF && (s.buf[1] & 0xF6) == 0xF0);
    CHECK(((s.buf[3] & 3) << 11 | s.buf[4] << 3 | s.buf[5] >> 5) == 13);
    CHECK((s.buf[2] >> 6) == 1 && ((s.buf[2] >> 2) & 0xF) == 6);
    /* Another track's fragments are skipped. */
    m.track_id = 8;
    s.n = 0;
    CHECK(mp4_fragment(&m, frag, (size_t)(p - frag), collect, &s) == 0);
}

static void test_adts_header(void)
{
    static unsigned char buf[3 * 300];
    int i;
    for (i = 0; i < 3; i++) {
        adts_header(buf + i * 300, 2, 4, 2, 293);
        memset(buf + i * 300 + 7, 0x21, 293);
    }
    CHECK(audio_sniff(buf, sizeof(buf)) == AF_ADTS);
}

static void test_track_info(void)
{
    char a[64], t[64], small[8], u[128], d[128];
    /* An ID3v2.4 tag: TIT2 in UTF-8, TPE1 in UTF-16 with a BOM ("Bj\xF6rk"). */
    static const unsigned char tag24[] = {
        'I', 'D', '3', 4, 0, 0, 0, 0, 0, 40,
        'T', 'I', 'T', '2', 0, 0, 0, 7, 0, 0, 3, 'H', 'y', 'p', 'e', 'r', 0,
        'T', 'P', 'E', '1', 0, 0, 0, 13, 0, 0, 1, 0xFF, 0xFE, 'B', 0, 'j', 0, 0xF6, 0, 'r', 0, 'k', 0
    };
    /* ID3v2.3 with Latin-1 text, and a PRIV frame first. */
    static const unsigned char tag23[] = {
        'I', 'D', '3', 3, 0, 0, 0, 0, 0, 34,
        'P', 'R', 'I', 'V', 0, 0, 0, 4, 0, 0, 'x', 0, 1, 2,
        'T', 'I', 'T', '2', 0, 0, 0, 6, 0, 0, 0, 'C', 'a', 'f', 0xE9, 's'
    };
    static const unsigned char timestamp_only[] = {
        'I', 'D', '3', 4, 0, 0, 0, 0, 0, 14,
        'P', 'R', 'I', 'V', 0, 0, 0, 4, 0, 0, 'x', 0, 1, 2
    };

    CHECK(title_split("Massive Attack - Teardrop", a, sizeof(a), t, sizeof(t)) == 1);
    CHECK(strcmp(a, "Massive Attack") == 0 && strcmp(t, "Teardrop") == 0);
    CHECK(title_split("A-ha - Take On Me - 2017 Mix", a, sizeof(a), t, sizeof(t)) == 1);
    CHECK(strcmp(a, "A-ha") == 0 && strcmp(t, "Take On Me - 2017 Mix") == 0);
    CHECK(title_split("Sigur R\xC3\xB3s \xE2\x80\x93 Hopp\xC3\xADpolla", a, sizeof(a), t,
                      sizeof(t)) == 1);
    CHECK(strcmp(a, "Sigur R\xC3\xB3s") == 0);
    CHECK(title_split("The Morning Show", a, sizeof(a), t, sizeof(t)) == 0);
    CHECK(a[0] == '\0' && strcmp(t, "The Morning Show") == 0);
    CHECK(title_split(" - Only title", a, sizeof(a), t, sizeof(t)) == 0);
    CHECK(title_split("Jean-Michel Jarre", a, sizeof(a), t, sizeof(t)) == 0);
    CHECK(title_split("Abcdefghij - x", small, sizeof(small), t, sizeof(t)) == 1);
    CHECK(strcmp(small, "Abcdefg") == 0);

    CHECK(id3_track(tag24, sizeof(tag24), a, sizeof(a), t, sizeof(t)) == 1);
    CHECK(strcmp(t, "Hyper") == 0 && strcmp(a, "Bj\xC3\xB6rk") == 0);
    CHECK(id3_track(tag23, sizeof(tag23), a, sizeof(a), t, sizeof(t)) == 1);
    CHECK(strcmp(t, "Caf\xC3\xA9s") == 0 && a[0] == '\0');
    CHECK(id3_track(timestamp_only, sizeof(timestamp_only), a, sizeof(a), t, sizeof(t)) == 0);
    CHECK(id3_track(tag24, 12, a, sizeof(a), t, sizeof(t)) == 0); /* cut short */

    CHECK(notes_web("MP3 128k rb:abc web:https://example.org/radio", u, sizeof(u)) == 1);
    CHECK(strcmp(u, "https://example.org/radio") == 0);
    CHECK(notes_web("web:http://a.example x", u, sizeof(u)) == 1 && strcmp(u, "http://a.example") == 0);
    CHECK(notes_web("cobweb:http://a.example", u, sizeof(u)) == 0);
    CHECK(notes_web("web:ftp://a.example", u, sizeof(u)) == 0);
    CHECK(notes_web("", u, sizeof(u)) == 0 && u[0] == '\0');
    notes_display("MP3 128k rb:abc-def web:https://e.org (country: England)", d, sizeof(d));
    CHECK(strcmp(d, "MP3 128k (country: England)") == 0);
    notes_display("rb:x", d, sizeof(d));
    CHECK(d[0] == '\0');
}

static void test_ts_metadata(void)
{
    static const unsigned char pat[] = { 0x00, 0x00, 0xB0, 0x0D, 0x00, 0x01, 0xC1, 0x00, 0x00,
                                         0x00, 0x01, 0xE1, 0x00, 0, 0, 0, 0 };
    /* PMT: ADTS on 0x101, timed ID3 on 0x103. */
    static const unsigned char pmt[] = { 0x00, 0x02, 0xB0, 0x17, 0x00, 0x01, 0xC1, 0x00, 0x00,
                                         0xE1, 0x01, 0xF0, 0x00, 0x0F, 0xE1, 0x01, 0xF0, 0x00,
                                         0x15, 0xE1, 0x03, 0xF0, 0x00, 0,    0,    0,    0 };
    static const unsigned char tag[] = { 'I', 'D', '3', 4, 0, 0, 0, 0, 0, 14,
                                         'T', 'I', 'T', '2', 0, 0, 0, 4, 0, 0, 3, 'N', 'e', 'w' };
    static unsigned char seg[188 * 3];
    unsigned char pes[64];
    sink meta;
    sink audio;
    ts_demux t;
    char a[32], ti[32];

    memcpy(pes, "\x00\x00\x01\xBD\x00\x00\x84\x80\x05\x21\x00\x01\x00\x01", 14);
    memcpy(pes + 14, tag, sizeof(tag));
    ts_packet(seg, 0, 1, pat, sizeof(pat));
    ts_packet(seg + 188, 0x100, 1, pmt, sizeof(pmt));
    ts_packet(seg + 376, 0x103, 1, pes, 14 + sizeof(tag));
    ts_init(&t);
    t.meta_out = collect;
    t.meta_ud = &meta;
    meta.n = audio.n = 0;
    ts_feed(&t, seg, sizeof(seg), collect, &audio);
    CHECK(t.id3_pid == 0x103 && audio.n == 0);
    CHECK(meta.n == sizeof(tag) && memcmp(meta.buf, tag, sizeof(tag)) == 0);
    CHECK(id3_track(meta.buf, meta.n, a, sizeof(a), ti, sizeof(ti)) == 1 && strcmp(ti, "New") == 0);
}

void run_hls_tests(void)
{
    test_track_info();
    test_ts_metadata();
    test_playlists();
    test_url_resolve();
    test_ts();
    test_id3_and_packed();
    test_mp4();
    test_adts_header();
}
