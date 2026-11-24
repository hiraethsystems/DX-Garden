#include "track_info.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"

/* Copies at most n-1 bytes, never splitting a UTF-8 character, trimming
 * spaces at both ends. */
static void copy_trim(char *out, size_t n, const char *s, size_t len)
{
    size_t k;
    while (len && (*s == ' ' || *s == '\t'))
        s++, len--;
    while (len && (s[len - 1] == ' ' || s[len - 1] == '\t'))
        len--;
    k = len < n - 1 ? len : n - 1;
    while (k > 0 && k < len && ((unsigned char)s[k] & 0xC0) == 0x80)
        k--;
    memcpy(out, s, k);
    out[k] = '\0';
}

int title_split(const char *in, char *artist, size_t an, char *title, size_t tn)
{
    static const char *const SEP[] = { " - ", " \xE2\x80\x93 ", " \xE2\x80\x94 ", NULL };
    const char *best = NULL;
    size_t best_len = 0;
    int i;

    artist[0] = '\0';
    for (i = 0; SEP[i]; i++) {
        const char *p = strstr(in, SEP[i]);
        if (p && (!best || p < best)) {
            best = p;
            best_len = strlen(SEP[i]);
        }
    }
    if (best) {
        const char *rest = best + best_len;
        size_t al = (size_t)(best - in);
        /* Text on both sides (not just spaces). */
        int left = 0, right = 0;
        size_t j;
        for (j = 0; j < al; j++)
            if (in[j] != ' ')
                left = 1;
        for (j = 0; rest[j]; j++)
            if (rest[j] != ' ')
                right = 1;
        if (left && right) {
            copy_trim(artist, an, in, al);
            copy_trim(title, tn, rest, strlen(rest));
            return 1;
        }
    }
    copy_trim(title, tn, in, strlen(in));
    return 0;
}

/* ---- ID3 ----------------------------------------------------------------- */

static size_t syncsafe(const unsigned char *p)
{
    return ((size_t)(p[0] & 0x7F) << 21) | ((size_t)(p[1] & 0x7F) << 14) |
           ((size_t)(p[2] & 0x7F) << 7) | (p[3] & 0x7F);
}

/* A text frame's body (encoding byte, then text) to UTF-8. */
static void id3_text(const unsigned char *p, size_t n, char *out, size_t on)
{
    unsigned enc;
    out[0] = '\0';
    if (n < 1)
        return;
    enc = p[0];
    p++;
    n--;
    if (enc == 0 || enc == 3) {
        size_t len = 0;
        while (len < n && p[len])
            len++;
        if (enc == 3 || utf8_valid((const char *)p, len)) {
            copy_trim(out, on, (const char *)p, len);
        } else {
            size_t ol;
            char *u = latin1_to_utf8((const char *)p, len, &ol);
            if (u) {
                copy_trim(out, on, u, ol);
                free(u);
            }
        }
    } else if (enc == 1 || enc == 2) {
        /* UTF-16, with a byte order mark (1) or big-endian (2). */
        uint16_t units[256];
        size_t k = 0, i = 0;
        int be = enc == 2;
        char tmp[768];
        if (enc == 1 && n >= 2) {
            be = !(p[0] == 0xFF && p[1] == 0xFE);
            if ((p[0] == 0xFF && p[1] == 0xFE) || (p[0] == 0xFE && p[1] == 0xFF))
                i = 2;
        }
        for (; i + 1 < n && k < 255; i += 2) {
            uint16_t u = be ? (uint16_t)(p[i] << 8 | p[i + 1]) : (uint16_t)(p[i + 1] << 8 | p[i]);
            if (!u)
                break;
            units[k++] = u;
        }
        units[k] = 0;
        utf16_to_utf8(units, tmp, sizeof(tmp));
        copy_trim(out, on, tmp, strlen(tmp));
    }
}

int id3_track(const unsigned char *p, size_t n, char *artist, size_t an, char *title, size_t tn)
{
    size_t size, off, end;
    unsigned ver, flags;
    int found = 0;

    artist[0] = title[0] = '\0';
    if (n < 10 || memcmp(p, "ID3", 3) != 0)
        return 0;
    ver = p[3];
    flags = p[5];
    size = syncsafe(p + 6);
    end = 10 + size < n ? 10 + size : n;
    off = 10;
    if ((flags & 0x40) && ver >= 3) { /* extended header */
        if (off + 4 > end)
            return 0;
        off += ver == 4 ? syncsafe(p + off) : 4 + ((size_t)p[off] << 24 | (size_t)p[off + 1] << 16 |
                                                   (size_t)p[off + 2] << 8 | p[off + 3]);
    }
    while (off < end) {
        char id[5];
        size_t fs, hdr = ver == 2 ? 6 : 10;
        if (off + hdr > end || p[off] == 0)
            break;
        if (ver == 2) {
            memcpy(id, p + off, 3);
            id[3] = '\0';
            fs = (size_t)p[off + 3] << 16 | (size_t)p[off + 4] << 8 | p[off + 5];
        } else {
            memcpy(id, p + off, 4);
            id[4] = '\0';
            fs = ver == 4 ? syncsafe(p + off + 4)
                          : ((size_t)p[off + 4] << 24 | (size_t)p[off + 5] << 16 |
                             (size_t)p[off + 6] << 8 | p[off + 7]);
        }
        if (fs > end - off - hdr)
            break;
        if (strcmp(id, "TIT2") == 0 || strcmp(id, "TT2") == 0) {
            id3_text(p + off + hdr, fs, title, tn);
            found = 1;
        } else if (strcmp(id, "TPE1") == 0 || strcmp(id, "TP1") == 0) {
            id3_text(p + off + hdr, fs, artist, an);
            found = 1;
        }
        off += hdr + fs;
    }
    return found && (artist[0] || title[0]);
}

/* ---- notes ----------------------------------------------------------------- */

int notes_web(const char *notes, char *url, size_t n)
{
    const char *p = notes;
    if (n)
        url[0] = '\0';
    while (p && (p = strstr(p, "web:")) != NULL) {
        if (p == notes || p[-1] == ' ') {
            const char *u = p + 4;
            size_t len = strcspn(u, " \t");
            if (len && len < n && (strncmp(u, "http://", 7) == 0 || strncmp(u, "https://", 8) == 0)) {
                memcpy(url, u, len);
                url[len] = '\0';
                return 1;
            }
        }
        p += 4;
    }
    return 0;
}

void notes_display(const char *notes, char *out, size_t n)
{
    const char *p = notes;
    size_t o = 0;
    if (!n)
        return;
    out[0] = '\0';
    while (*p) {
        size_t len;
        while (*p == ' ')
            p++;
        len = strcspn(p, " ");
        if (!len)
            break;
        if (!(len > 3 && strncmp(p, "rb:", 3) == 0) && !(len > 4 && strncmp(p, "web:", 4) == 0)) {
            if (o && o + 1 < n)
                out[o++] = ' ';
            if (o + len < n) {
                memcpy(out + o, p, len);
                o += len;
            }
            out[o] = '\0';
        }
        p += len;
    }
}
