#include "media_demux.h"

#include <string.h>

#define TS 188

static unsigned be16(const unsigned char *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static unsigned long be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) |
           p[3];
}

size_t id3_skip(const unsigned char *p, size_t n)
{
    size_t off = 0;
    while (n - off >= 10 && p[off] == 'I' && p[off + 1] == 'D' && p[off + 2] == '3' &&
           !((p[off + 6] | p[off + 7] | p[off + 8] | p[off + 9]) & 0x80)) {
        size_t sz = ((size_t)p[off + 6] << 21) | ((size_t)p[off + 7] << 14) |
                    ((size_t)p[off + 8] << 7) | p[off + 9];
        sz += 10 + ((p[off + 5] & 0x10) ? 10 : 0); /* header, and a footer if flagged */
        if (sz > n - off)
            return n;
        off += sz;
    }
    return off;
}

segment_kind segment_sniff(const unsigned char *p, size_t n)
{
    size_t off;
    if (n >= TS * 2 + 1 && p[0] == 0x47 && p[TS] == 0x47 && p[TS * 2] == 0x47)
        return SEG_TS;
    if (n >= 1 && n < TS * 2 + 1 && p[0] == 0x47)
        return SEG_TS;
    if (n >= 8 && (memcmp(p + 4, "ftyp", 4) == 0 || memcmp(p + 4, "styp", 4) == 0 ||
                   memcmp(p + 4, "moof", 4) == 0 || memcmp(p + 4, "sidx", 4) == 0 ||
                   memcmp(p + 4, "prft", 4) == 0 || memcmp(p + 4, "emsg", 4) == 0 ||
                   memcmp(p + 4, "moov", 4) == 0))
        return SEG_FMP4;
    off = id3_skip(p, n);
    if (off < n && (p[off] == 0xFF || p[off] == 0x56 || p[off] == 'I'))
        return SEG_PACKED;
    return SEG_UNKNOWN;
}

/* ---- MPEG-TS ---------------------------------------------------------- */

void ts_init(ts_demux *t)
{
    memset(t, 0, sizeof(*t));
    t->pmt_pid = -1;
    t->audio_pid = -1;
    t->id3_pid = -1;
}

static void meta_flush(ts_demux *t)
{
    if (t->meta_n && t->meta_out)
        t->meta_out(t->meta_ud, t->meta, t->meta_n);
    t->meta_n = 0;
}

static int supported_audio(int type)
{
    return type == 0x0F || type == 0x11 || type == 0x03 || type == 0x04;
}

static int other_audio(int type)
{
    return type == 0x81 || type == 0x87; /* AC-3, E-AC-3 */
}

/* A PSI section (PAT or PMT) in one packet's payload. */
static void ts_psi(ts_demux *t, int pid, const unsigned char *p, size_t n)
{
    size_t ptr, len, i, end;
    if (n < 1)
        return;
    ptr = p[0];
    if (1 + ptr + 3 > n)
        return;
    p += 1 + ptr;
    n -= 1 + ptr;
    len = ((size_t)(p[1] & 0x0F) << 8) | p[2];
    end = 3 + len < n ? 3 + len : n;
    if (end < 4)
        return;
    end -= 4; /* CRC */
    if (pid == 0 && p[0] == 0x00) {
        for (i = 8; i + 4 <= end; i += 4) {
            unsigned prog = be16(p + i);
            if (prog != 0) {
                t->pmt_pid = (int)(be16(p + i + 2) & 0x1FFF);
                return;
            }
        }
    } else if (pid == t->pmt_pid && p[0] == 0x02 && end >= 12) {
        size_t pil = be16(p + 10) & 0x0FFF;
        for (i = 12 + pil; i + 5 <= end;) {
            int type = p[i], spid = (int)(be16(p + i + 1) & 0x1FFF);
            size_t esl = be16(p + i + 3) & 0x0FFF;
            if (t->audio_pid < 0 && supported_audio(type)) {
                t->audio_pid = spid;
                t->audio_type = type;
            } else if (type == 0x15 && t->id3_pid < 0) {
                t->id3_pid = spid;
            } else if (other_audio(type) && !t->unsupported) {
                t->unsupported = type;
            }
            i += 5 + esl;
        }
    }
}

size_t ts_feed(ts_demux *t, const unsigned char *p, size_t n, es_out out, void *ud)
{
    size_t i = 0, written = 0;
    while (i + TS <= n) {
        const unsigned char *k = p + i;
        int pusi, pid, afc;
        size_t off = 4;
        if (k[0] != 0x47) {
            /* Lost sync: find the next packet start. */
            i++;
            while (i + TS <= n && !(p[i] == 0x47 && (i + TS >= n || p[i + TS] == 0x47)))
                i++;
            continue;
        }
        pusi = (k[1] & 0x40) != 0;
        pid = (int)(be16(k + 1) & 0x1FFF);
        afc = (k[3] >> 4) & 3;
        i += TS;
        if (afc & 2)
            off += 1 + (size_t)k[4];
        if (!(afc & 1) || off >= TS)
            continue;
        if (pid == 0 || pid == t->pmt_pid) {
            if (pusi)
                ts_psi(t, pid, k + off, TS - off);
            continue;
        }
        if (pid == t->id3_pid && pid >= 0) {
            const unsigned char *h = k + off;
            size_t avail = TS - off;
            if (pusi) {
                meta_flush(t);
                t->meta_skip = avail >= 9 && h[0] == 0 && h[1] == 0 && h[2] == 1 ? 9 + h[8] : 0;
            }
            if (t->meta_skip) {
                size_t s = (size_t)t->meta_skip < avail ? (size_t)t->meta_skip : avail;
                h += s;
                avail -= s;
                t->meta_skip -= (int)s;
            }
            if (avail && t->meta_n + avail <= sizeof(t->meta)) {
                memcpy(t->meta + t->meta_n, h, avail);
                t->meta_n += avail;
            }
            continue;
        }
        if (pid != t->audio_pid)
            continue;
        if (pusi) {
            /* A PES header: start code, stream id, length, then for audio
             * streams two flag bytes and the header's own length. */
            const unsigned char *h = k + off;
            size_t avail = TS - off;
            if (avail >= 9 && h[0] == 0 && h[1] == 0 && h[2] == 1)
                t->pes_skip = 9 + h[8];
            else
                t->pes_skip = 0;
        }
        if (t->pes_skip) {
            size_t s = (size_t)t->pes_skip < TS - off ? (size_t)t->pes_skip : TS - off;
            off += s;
            t->pes_skip -= (int)s;
        }
        if (off < TS) {
            out(ud, k + off, TS - off);
            written += TS - off;
        }
    }
    meta_flush(t);
    return written;
}

/* ---- fragmented MP4 --------------------------------------------------- */

/* The box at p (n bytes left): its type and the payload's bounds. Returns
 * the whole box size, or 0 if it doesn't fit. */
static size_t box(const unsigned char *p, size_t n, char type[5], size_t *hdr)
{
    unsigned long long sz;
    if (n < 8)
        return 0;
    sz = be32(p);
    memcpy(type, p + 4, 4);
    type[4] = '\0';
    *hdr = 8;
    if (sz == 1) {
        if (n < 16)
            return 0;
        sz = ((unsigned long long)be32(p + 8) << 32) | be32(p + 12);
        *hdr = 16;
    } else if (sz == 0) {
        sz = n;
    }
    if (sz < *hdr || sz > n)
        return 0;
    return (size_t)sz;
}

/* The first box of `type` among the children in p..p+n; its payload in
 * out and on. */
static int find(const unsigned char *p, size_t n, const char *type, const unsigned char **out,
                size_t *on)
{
    size_t i = 0;
    while (i < n) {
        char t[5];
        size_t hdr, sz = box(p + i, n - i, t, &hdr);
        if (!sz)
            return 0;
        if (strcmp(t, type) == 0) {
            *out = p + i + hdr;
            *on = sz - hdr;
            return 1;
        }
        i += sz;
    }
    return 0;
}

/* An MPEG-4 descriptor's length (up to four 7-bit bytes). */
static size_t desc_len(const unsigned char **p, const unsigned char *end)
{
    size_t len = 0;
    int i;
    for (i = 0; i < 4 && *p < end; i++) {
        unsigned char b = *(*p)++;
        len = (len << 7) | (b & 0x7F);
        if (!(b & 0x80))
            break;
    }
    return len;
}

/* Bit reader for the AudioSpecificConfig. */
typedef struct {
    const unsigned char *p;
    size_t n, bit;
} bits;

static unsigned getbits(bits *b, unsigned k)
{
    unsigned v = 0;
    while (k--) {
        size_t byte = b->bit >> 3;
        unsigned bitv = byte < b->n ? (b->p[byte] >> (7 - (b->bit & 7))) & 1 : 0;
        v = (v << 1) | bitv;
        b->bit++;
    }
    return v;
}

static unsigned asc_aot(bits *b)
{
    unsigned aot = getbits(b, 5);
    return aot == 31 ? 32 + getbits(b, 6) : aot;
}

static unsigned asc_freq(bits *b)
{
    static const unsigned long RATES[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                             22050, 16000, 12000, 11025, 8000,  7350 };
    unsigned idx = getbits(b, 4), i, best = 4;
    unsigned long f, d = ~0UL;
    if (idx != 15)
        return idx;
    f = ((unsigned long)getbits(b, 12) << 12) | getbits(b, 12);
    /* An explicit rate: the nearest index (ADTS has no other way). */
    for (i = 0; i < 13; i++) {
        unsigned long e = RATES[i] > f ? RATES[i] - f : f - RATES[i];
        if (e < d)
            d = e, best = i;
    }
    return best;
}

static void parse_esds(mp4_audio *m, const unsigned char *p, size_t n)
{
    const unsigned char *end = p + n;
    if (n < 4)
        return;
    p += 4; /* version and flags */
    while (p < end) {
        unsigned tag = *p++;
        size_t len = desc_len(&p, end);
        const unsigned char *body = p;
        if (len > (size_t)(end - p))
            len = (size_t)(end - p);
        if (tag == 0x03) { /* ES_Descriptor: its children follow a short header */
            unsigned flags;
            if (len < 3)
                return;
            flags = body[2];
            p = body + 3;
            if (flags & 0x80)
                p += 2;
            if ((flags & 0x40) && p < end)
                p += 1 + *p;
            if (flags & 0x20)
                p += 2;
            continue;
        }
        if (tag == 0x04) { /* DecoderConfigDescriptor */
            if (len < 13)
                return;
            m->oti = body[0];
            p = body + 13;
            continue;
        }
        if (tag == 0x05) { /* DecoderSpecificInfo: the AudioSpecificConfig */
            bits b = { body, len, 0 };
            unsigned aot = asc_aot(&b), fi = asc_freq(&b), ch = getbits(&b, 4);
            if (aot == 5 || aot == 29) {
                /* HE-AAC: the core is described after the SBR rate; the
                 * decoder finds SBR (and PS) in the stream itself. */
                fi = asc_freq(&b);
                aot = asc_aot(&b);
            }
            m->aot = aot;
            m->freq_index = fi;
            m->channels = ch ? ch : 2;
            return;
        }
        p = body + len;
    }
}

int mp4_init(mp4_audio *m, const unsigned char *p, size_t n)
{
    const unsigned char *moov, *trak, *x;
    size_t moov_n, trak_n, xn, i;

    memset(m, 0, sizeof(*m));
    if (!find(p, n, "moov", &moov, &moov_n))
        return -1;
    /* The first sound track. */
    for (i = 0; i < moov_n;) {
        char t[5];
        size_t hdr, sz = box(moov + i, moov_n - i, t, &hdr);
        const unsigned char *mdia, *minf, *stbl, *stsd;
        size_t mdia_n, minf_n, stbl_n, stsd_n;
        if (!sz)
            break;
        trak = moov + i + hdr;
        trak_n = sz - hdr;
        i += sz;
        if (strcmp(t, "trak") != 0)
            continue;
        if (!find(trak, trak_n, "mdia", &mdia, &mdia_n))
            continue;
        if (!find(mdia, mdia_n, "hdlr", &x, &xn) || xn < 12 || memcmp(x + 8, "soun", 4) != 0)
            continue;
        if (find(trak, trak_n, "tkhd", &x, &xn) && xn >= 16)
            m->track_id = (unsigned)be32(x + (x[0] == 1 ? 20 : 12));
        if (!find(mdia, mdia_n, "minf", &minf, &minf_n) ||
            !find(minf, minf_n, "stbl", &stbl, &stbl_n) ||
            !find(stbl, stbl_n, "stsd", &stsd, &stsd_n) || stsd_n < 8 + 8 + 28)
            continue;
        {
            /* stsd: version/flags, count, then the first sample entry:
             * size, type, 6 reserved + data reference index, then the audio
             * fields (28 bytes) and child boxes. */
            const unsigned char *e = stsd + 8, *esds;
            size_t en = be32(e), esds_n;
            if (en > stsd_n - 8 || en < 8 + 28)
                continue;
            memcpy(m->codec, e + 4, 4);
            m->codec[4] = '\0';
            if (strcmp(m->codec, "mp4a") != 0)
                return -1; /* encrypted (enca), Opus, FLAC, AC-3...: not here */
            m->channels = be16(e + 8 + 16);
            if (find(e + 8 + 28, en - 8 - 28, "esds", &esds, &esds_n))
                parse_esds(m, esds, esds_n);
        }
        break;
    }
    if (!m->oti)
        return -1;
    /* Defaults for the fragments (mvex/trex). */
    if (find(moov, moov_n, "mvex", &x, &xn)) {
        const unsigned char *trex;
        size_t trex_n;
        if (find(x, xn, "trex", &trex, &trex_n) && trex_n >= 24)
            m->default_size = (unsigned)be32(trex + 16);
    }
    m->ok = m->oti == 0x40 || m->oti == 0x66 || m->oti == 0x67 || m->oti == 0x68 ||
            m->oti == 0x69 || m->oti == 0x6B;
    if (m->ok && (m->oti == 0x40 || (m->oti >= 0x66 && m->oti <= 0x68)) && !m->aot)
        m->aot = 2, m->freq_index = 4; /* no ASC: guess AAC-LC 44.1 kHz */
    return m->ok ? 0 : -1;
}

void adts_header(unsigned char h[7], unsigned aot, unsigned freq_index, unsigned channels,
                 size_t payload)
{
    size_t len = payload + 7;
    unsigned profile = (aot >= 1 && aot <= 4 ? aot : 2) - 1;
    h[0] = 0xFF;
    h[1] = 0xF1; /* MPEG-4, layer 0, no CRC */
    h[2] = (unsigned char)((profile << 6) | ((freq_index & 0xF) << 2) | ((channels >> 2) & 1));
    h[3] = (unsigned char)(((channels & 3) << 6) | ((len >> 11) & 3));
    h[4] = (unsigned char)((len >> 3) & 0xFF);
    h[5] = (unsigned char)(((len & 7) << 5) | 0x1F);
    h[6] = 0xFC;
}

size_t mp4_fragment(const mp4_audio *m, const unsigned char *p, size_t n, es_out out, void *ud)
{
    size_t i = 0, count = 0;
    int aac = m->oti == 0x40 || (m->oti >= 0x66 && m->oti <= 0x68);

    if (!m->ok)
        return 0;
    while (i < n) {
        char t[5];
        size_t hdr, sz = box(p + i, n - i, t, &hdr), moof = i;
        const unsigned char *traf, *x;
        size_t traf_n, xn, j;
        if (!sz)
            break;
        i += sz;
        if (strcmp(t, "moof") != 0)
            continue;
        /* Each track fragment of ours: tfhd defaults, then trun samples. */
        for (j = 0; j < sz - hdr;) {
            char tt[5];
            size_t h2, s2 = box(p + moof + hdr + j, sz - hdr - j, tt, &h2);
            unsigned long flags, def_size = m->default_size, track;
            size_t base = moof, data_off = 0, k, nsamp;
            int have_off = 0;
            if (!s2)
                break;
            traf = p + moof + hdr + j + h2;
            traf_n = s2 - h2;
            j += s2;
            if (strcmp(tt, "traf") != 0 || !find(traf, traf_n, "tfhd", &x, &xn) || xn < 8)
                continue;
            flags = be32(x) & 0xFFFFFF;
            track = be32(x + 4);
            if (m->track_id && track != m->track_id)
                continue;
            {
                size_t o = 8;
                if (flags & 0x1) { /* base data offset: from the start of the segment */
                    if (xn >= o + 8)
                        base = (size_t)be32(x + o + 4);
                    o += 8;
                }
                if (flags & 0x2)
                    o += 4;
                if (flags & 0x8)
                    o += 4;
                if ((flags & 0x10) && xn >= o + 4)
                    def_size = be32(x + o);
            }
            if (!find(traf, traf_n, "trun", &x, &xn) || xn < 8)
                continue;
            flags = be32(x) & 0xFFFFFF;
            nsamp = be32(x + 4);
            k = 8;
            if (flags & 0x1) {
                if (xn < k + 4)
                    continue;
                data_off = (size_t)(long)(int)be32(x + k); /* signed */
                have_off = 1;
                k += 4;
            }
            if (flags & 0x4)
                k += 4;
            if (!have_off) {
                /* No offset: the samples start in the mdat that follows. */
                const unsigned char *md;
                size_t mdn;
                if (!find(p + i, n - i, "mdat", &md, &mdn))
                    continue;
                base = (size_t)(md - p);
            }
            {
                size_t pos = base + data_off, s;
                size_t per = ((flags & 0x100) ? 4 : 0) + ((flags & 0x200) ? 4 : 0) +
                             ((flags & 0x400) ? 4 : 0) + ((flags & 0x800) ? 4 : 0);
                for (s = 0; s < nsamp && k + per <= xn; s++) {
                    size_t q = k, size = def_size;
                    if (flags & 0x100)
                        q += 4;
                    if (flags & 0x200)
                        size = be32(x + q);
                    k += per;
                    /* An ADTS frame's length field has 13 bits. */
                    if (!size || pos + size > n || (aac && size + 7 > 0x1FFF))
                        break;
                    if (aac) {
                        unsigned char h[7];
                        adts_header(h, m->aot, m->freq_index, m->channels, size);
                        out(ud, h, 7);
                    }
                    out(ud, p + pos, size);
                    pos += size;
                    count++;
                }
            }
        }
    }
    return count;
}
