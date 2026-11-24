#include "zip_read.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static unsigned u16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned long u32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

static int ends_ci(const unsigned char *name, size_t len, const char *suffix)
{
    size_t k = strlen(suffix), i;
    if (len < k)
        return 0;
    for (i = 0; i < k; i++)
        if (tolower(name[len - k + i]) != tolower((unsigned char)suffix[i]))
            return 0;
    return 1;
}

char *zip_extract(const unsigned char *zip, size_t n, const char *suffix, size_t *out_len,
                  char *err, size_t errn)
{
    size_t eocd, cd, i;
    unsigned entries, e;
    if (n < 22) {
        snprintf(err, errn, "not a zip file");
        return NULL;
    }
    /* The end-of-central-directory record, searching back over a comment. */
    for (eocd = n - 22;; eocd--) {
        if (u32(zip + eocd) == 0x06054b50UL)
            break;
        if (eocd == 0 || n - eocd > 22 + 65535) {
            snprintf(err, errn, "not a zip file");
            return NULL;
        }
    }
    entries = u16(zip + eocd + 10);
    cd = u32(zip + eocd + 16);
    for (e = 0, i = cd; e < entries; e++) {
        unsigned method, nlen, xlen, clen;
        unsigned long csize, usize, off;
        const unsigned char *name;
        if (i + 46 > n || u32(zip + i) != 0x02014b50UL)
            break;
        method = u16(zip + i + 10);
        csize = u32(zip + i + 20);
        usize = u32(zip + i + 24);
        nlen = u16(zip + i + 28);
        xlen = u16(zip + i + 30);
        clen = u16(zip + i + 32);
        off = u32(zip + i + 42);
        name = zip + i + 46;
        if (i + 46 + nlen > n)
            break;
        if (ends_ci(name, nlen, suffix)) {
            size_t data;
            char *out;
            if (off + 30 > n || u32(zip + off) != 0x04034b50UL) {
                snprintf(err, errn, "damaged zip file");
                return NULL;
            }
            data = off + 30 + u16(zip + off + 26) + u16(zip + off + 28);
            if (data + csize > n) {
                snprintf(err, errn, "the zip file is cut short");
                return NULL;
            }
            out = (char *)malloc(usize + 1);
            if (!out) {
                snprintf(err, errn, "out of memory");
                return NULL;
            }
            if (method == 0) {
                memcpy(out, zip + data, csize < usize ? csize : usize);
            } else if (method == 8) {
                z_stream z;
                int rc;
                memset(&z, 0, sizeof(z));
                if (inflateInit2(&z, -MAX_WBITS) != Z_OK) {
                    free(out);
                    snprintf(err, errn, "zlib failed");
                    return NULL;
                }
                z.next_in = (Bytef *)(zip + data);
                z.avail_in = (uInt)csize;
                z.next_out = (Bytef *)out;
                z.avail_out = (uInt)usize;
                rc = inflate(&z, Z_FINISH);
                inflateEnd(&z);
                if (rc != Z_STREAM_END || z.total_out != usize) {
                    free(out);
                    snprintf(err, errn, "the zip file is damaged");
                    return NULL;
                }
            } else {
                free(out);
                snprintf(err, errn, "unsupported zip compression (%u)", method);
                return NULL;
            }
            out[usize] = '\0';
            *out_len = usize;
            return out;
        }
        i += 46 + nlen + xlen + clen;
    }
    snprintf(err, errn, "no %s file in the zip", suffix);
    return NULL;
}
