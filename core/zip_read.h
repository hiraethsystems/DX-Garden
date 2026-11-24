/*
 * One file out of a zip archive (GeoNames' cities1000.zip), with zlib.
 * Only "stored" and "deflated" entries, as GeoNames uses.
 */
#ifndef DXG_ZIP_READ_H
#define DXG_ZIP_READ_H

#include <stddef.h>

/* The first entry whose name ends in `suffix` (case-insensitive, e.g.
 * ".txt"), unpacked into a malloc'd buffer (NUL-terminated). Returns NULL
 * with a message in err. */
char *zip_extract(const unsigned char *zip, size_t n, const char *suffix, size_t *out_len,
                  char *err, size_t errn);

#endif
