#include "ustr.h"

#include <stdlib.h>
#include <string.h>

unsigned u_flags(uint32_t cp)
{
    size_t lo = 0, hi = UNITAB_FLAGS_N;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (cp < UNITAB_FLAGS[mid].first)
            hi = mid;
        else if (cp > UNITAB_FLAGS[mid].last)
            lo = mid + 1;
        else
            return UNITAB_FLAGS[mid].flags;
    }
    return 0;
}

static const unitab_map *map_find(const unitab_map *m, size_t n, uint32_t cp)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (cp < m[mid].cp)
            hi = mid;
        else if (cp > m[mid].cp)
            lo = mid + 1;
        else
            return &m[mid];
    }
    return NULL;
}

void ustr_init(ustr *s)
{
    s->c = NULL;
    s->n = s->cap = 0;
}

void ustr_free(ustr *s)
{
    free(s->c);
    ustr_init(s);
}

void ustr_clear(ustr *s)
{
    s->n = 0;
}

static void reserve(ustr *s, size_t n)
{
    if (n > s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 32;
        uint32_t *c;
        while (cap < n)
            cap *= 2;
        c = (uint32_t *)realloc(s->c, cap * sizeof(uint32_t));
        if (!c)
            abort(); /* small strings only; out of memory is fatal here */
        s->c = c;
        s->cap = cap;
    }
}

void ustr_push(ustr *s, uint32_t cp)
{
    reserve(s, s->n + 1);
    s->c[s->n++] = cp;
}

void ustr_append(ustr *s, const ustr *t)
{
    reserve(s, s->n + t->n);
    if (t->n)
        memcpy(s->c + s->n, t->c, t->n * sizeof(uint32_t));
    s->n += t->n;
}

void ustr_append_ascii(ustr *s, const char *a)
{
    while (*a)
        ustr_push(s, (unsigned char)*a++);
}

void ustr_copy(ustr *dst, const ustr *src)
{
    dst->n = 0;
    ustr_append(dst, src);
}

void ustr_set_utf8(ustr *s, const char *p, size_t n)
{
    const unsigned char *u = (const unsigned char *)p;
    size_t i = 0;
    s->n = 0;
    reserve(s, n);
    while (i < n) {
        uint32_t c = u[i];
        size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        size_t k;
        int ok = len && i + len <= n;
        if (ok && len > 1) {
            c &= 0x7F >> len;
            for (k = 1; k < len; k++) {
                if ((u[i + k] & 0xC0) != 0x80) {
                    ok = 0;
                    break;
                }
                c = (c << 6) | (u[i + k] & 0x3F);
            }
            /* Overlong forms, surrogates and values past U+10FFFF. */
            if (ok && ((len == 2 && c < 0x80) || (len == 3 && c < 0x800) ||
                       (len == 4 && (c < 0x10000 || c > 0x10FFFF)) ||
                       (c >= 0xD800 && c <= 0xDFFF)))
                ok = 0;
        }
        if (!ok) {
            s->c[s->n++] = 0xFFFD;
            i++;
        } else {
            s->c[s->n++] = c;
            i += len;
        }
    }
}

char *ustr_utf8(const ustr *s)
{
    char *out = (char *)malloc(s->n * 4 + 1), *o = out;
    size_t i;
    if (!out)
        return NULL;
    for (i = 0; i < s->n; i++) {
        uint32_t c = s->c[i];
        if (c < 0x80) {
            *o++ = (char)c;
        } else if (c < 0x800) {
            *o++ = (char)(0xC0 | (c >> 6));
            *o++ = (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            *o++ = (char)(0xE0 | (c >> 12));
            *o++ = (char)(0x80 | ((c >> 6) & 0x3F));
            *o++ = (char)(0x80 | (c & 0x3F));
        } else {
            *o++ = (char)(0xF0 | (c >> 18));
            *o++ = (char)(0x80 | ((c >> 12) & 0x3F));
            *o++ = (char)(0x80 | ((c >> 6) & 0x3F));
            *o++ = (char)(0x80 | (c & 0x3F));
        }
    }
    *o = '\0';
    return out;
}

void ustr_rstrip(ustr *s)
{
    while (s->n && u_isspace(s->c[s->n - 1]))
        s->n--;
}

void ustr_strip(ustr *s)
{
    size_t a = 0;
    ustr_rstrip(s);
    while (a < s->n && u_isspace(s->c[a]))
        a++;
    if (a) {
        memmove(s->c, s->c + a, (s->n - a) * sizeof(uint32_t));
        s->n -= a;
    }
}

void ustr_lstrip_char(ustr *s, uint32_t ch)
{
    size_t a = 0;
    while (a < s->n && s->c[a] == ch)
        a++;
    if (a) {
        memmove(s->c, s->c + a, (s->n - a) * sizeof(uint32_t));
        s->n -= a;
    }
}

void ustr_rstrip_chars(ustr *s, const char *chars)
{
    while (s->n && s->c[s->n - 1] < 0x80 && strchr(chars, (int)s->c[s->n - 1]) &&
           s->c[s->n - 1] != 0)
        s->n--;
}

void ustr_collapse_spaces(ustr *s)
{
    size_t i, o = 0;
    int pending = 0;
    for (i = 0; i < s->n; i++) {
        if (u_isspace(s->c[i])) {
            pending = o > 0;
        } else {
            if (pending)
                s->c[o++] = ' ';
            pending = 0;
            s->c[o++] = s->c[i];
        }
    }
    s->n = o;
}

void ustr_truncate(ustr *s, size_t n)
{
    if (s->n > n)
        s->n = n;
}

/* Python's final sigma rule: capital sigma becomes final sigma when a
 * cased letter comes before it and none after (skipping case-ignorable
 * characters both ways). */
static int final_sigma(const uint32_t *c, size_t n, size_t i)
{
    size_t j = i;
    int before = 0, after = 0;
    while (j > 0) {
        unsigned f = u_flags(c[--j]);
        if (f & U_IGNORABLE)
            continue;
        before = (f & U_CASED) != 0;
        break;
    }
    if (!before)
        return 0;
    for (j = i + 1; j < n; j++) {
        unsigned f = u_flags(c[j]);
        if (f & U_IGNORABLE)
            continue;
        after = (f & U_CASED) != 0;
        break;
    }
    return !after;
}

void ustr_lower(ustr *s)
{
    ustr out;
    size_t i;
    int changed = 0;
    for (i = 0; i < s->n && !changed; i++)
        if (s->c[i] >= 'A' && (s->c[i] <= 'Z' || s->c[i] >= 0x80))
            changed = 1;
    if (!changed)
        return;
    ustr_init(&out);
    reserve(&out, s->n);
    for (i = 0; i < s->n; i++) {
        uint32_t c = s->c[i];
        const unitab_map *m;
        if (c == 0x3A3) {
            ustr_push(&out, final_sigma(s->c, s->n, i) ? 0x3C2 : 0x3C3);
            continue;
        }
        if (c < 0x80) {
            ustr_push(&out, c >= 'A' && c <= 'Z' ? c + 32 : c);
            continue;
        }
        m = map_find(UNITAB_LOWER, UNITAB_LOWER_N, c);
        if (m) {
            unsigned k;
            for (k = 0; k < m->n; k++)
                ustr_push(&out, UNITAB_POOL[m->off + k]);
        } else {
            ustr_push(&out, c);
        }
    }
    free(s->c);
    *s = out;
}

void ustr_upper_ascii(ustr *s)
{
    size_t i;
    for (i = 0; i < s->n; i++)
        if (s->c[i] >= 'a' && s->c[i] <= 'z')
            s->c[i] -= 32;
}

void ustr_fold(ustr *s)
{
    ustr out;
    size_t i;
    ustr_init(&out);
    reserve(&out, s->n);
    for (i = 0; i < s->n; i++) {
        uint32_t c = s->c[i];
        const unitab_map *m;
        if (c < 0x80) {
            ustr_push(&out, c);
        } else if (c >= 0xAC00 && c <= 0xD7A3) {
            /* Hangul syllables: leading consonant, vowel, trailing one. */
            uint32_t k = c - 0xAC00, t = k % 28;
            ustr_push(&out, 0x1100 + k / (21 * 28));
            ustr_push(&out, 0x1161 + (k % (21 * 28)) / 28);
            if (t)
                ustr_push(&out, 0x11A7 + t);
        } else if ((m = map_find(UNITAB_FOLD, UNITAB_FOLD_N, c)) != NULL) {
            unsigned k;
            for (k = 0; k < m->n; k++)
                ustr_push(&out, UNITAB_POOL[m->off + k]);
        } else {
            ustr_push(&out, c);
        }
    }
    free(s->c);
    *s = out;
    ustr_lower(s);
    ustr_collapse_spaces(s);
}

int ustr_eq(const ustr *a, const ustr *b)
{
    return a->n == b->n && (a->n == 0 || memcmp(a->c, b->c, a->n * sizeof(uint32_t)) == 0);
}

int ustr_eq_ascii(const ustr *a, const char *b)
{
    size_t i;
    for (i = 0; i < a->n; i++)
        if (!b[i] || a->c[i] != (unsigned char)b[i])
            return 0;
    return b[i] == '\0';
}

long ustr_find(const ustr *s, const ustr *t, size_t from)
{
    size_t i;
    if (t->n == 0)
        return from <= s->n ? (long)from : -1;
    for (i = from; i + t->n <= s->n; i++)
        if (memcmp(s->c + i, t->c, t->n * sizeof(uint32_t)) == 0)
            return (long)i;
    return -1;
}

long ustr_find_ascii(const ustr *s, const char *t)
{
    ustr tt;
    long r;
    ustr_init(&tt);
    ustr_append_ascii(&tt, t);
    r = ustr_find(s, &tt, 0);
    ustr_free(&tt);
    return r;
}

int ustr_starts_ascii(const ustr *s, const char *prefix)
{
    size_t i;
    for (i = 0; prefix[i]; i++)
        if (i >= s->n || s->c[i] != (unsigned char)prefix[i])
            return 0;
    return 1;
}
