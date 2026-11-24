#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 64

typedef struct {
    const char *p, *end, *start;
    char *err;
    size_t errn;
    int failed;
} parser;

static void fail(parser *ps, const char *what)
{
    if (!ps->failed) {
        int line = 1;
        const char *q;
        for (q = ps->start; q < ps->p && q < ps->end; q++)
            if (*q == '\n')
                line++;
        if (ps->errn)
            snprintf(ps->err, ps->errn, "line %d: %s", line, what);
        ps->failed = 1;
    }
}

static void skip_ws(parser *ps)
{
    while (ps->p < ps->end &&
           (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r'))
        ps->p++;
}

static void free_inner(json_value *v)
{
    size_t i;
    if (v->type == JSON_STRING) {
        free(v->str);
    } else if (v->type == JSON_ARRAY || v->type == JSON_OBJECT) {
        for (i = 0; i < v->n; i++) {
            free_inner(&v->items[i]);
            if (v->keys)
                free(v->keys[i]);
        }
        free(v->items);
        free(v->keys);
    }
}

static int hex4(parser *ps, unsigned *out)
{
    unsigned v = 0;
    int i;
    if (ps->end - ps->p < 4)
        return 0;
    for (i = 0; i < 4; i++) {
        char c = ps->p[i];
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
            v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v |= (unsigned)(c - 'A' + 10);
        else
            return 0;
    }
    ps->p += 4;
    *out = v;
    return 1;
}

static size_t put_utf8(char *o, unsigned c)
{
    if (c < 0x80) {
        o[0] = (char)c;
        return 1;
    }
    if (c < 0x800) {
        o[0] = (char)(0xC0 | (c >> 6));
        o[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    if (c < 0x10000) {
        o[0] = (char)(0xE0 | (c >> 12));
        o[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        o[2] = (char)(0x80 | (c & 0x3F));
        return 3;
    }
    o[0] = (char)(0xF0 | (c >> 18));
    o[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((c >> 6) & 0x3F));
    o[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

/* At the opening quote. The result is malloc'd; escapes never make a
 * string longer than its source, so the source length is enough. */
static char *parse_string(parser *ps, size_t *len)
{
    const char *q = ps->p + 1;
    char *out, *o;
    /* Find the end first, for the size. */
    while (q < ps->end && *q != '"') {
        if (*q == '\\')
            q++;
        q++;
    }
    if (q >= ps->end) {
        fail(ps, "unterminated string");
        return NULL;
    }
    out = (char *)malloc((size_t)(q - ps->p) + 4);
    if (!out) {
        fail(ps, "out of memory");
        return NULL;
    }
    o = out;
    ps->p++;
    while (*ps->p != '"') {
        char c = *ps->p++;
        if (c != '\\') {
            *o++ = c;
            continue;
        }
        c = *ps->p++;
        switch (c) {
        case '"': *o++ = '"'; break;
        case '\\': *o++ = '\\'; break;
        case '/': *o++ = '/'; break;
        case 'b': *o++ = '\b'; break;
        case 'f': *o++ = '\f'; break;
        case 'n': *o++ = '\n'; break;
        case 'r': *o++ = '\r'; break;
        case 't': *o++ = '\t'; break;
        case 'u': {
            unsigned u, lo;
            if (!hex4(ps, &u)) {
                free(out);
                fail(ps, "bad \\u escape");
                return NULL;
            }
            if (u >= 0xD800 && u <= 0xDBFF && ps->end - ps->p >= 6 && ps->p[0] == '\\' &&
                ps->p[1] == 'u') {
                const char *save = ps->p;
                ps->p += 2;
                if (hex4(ps, &lo) && lo >= 0xDC00 && lo <= 0xDFFF)
                    u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                else
                    ps->p = save;
            }
            if (u >= 0xD800 && u <= 0xDFFF)
                u = 0xFFFD;
            o += put_utf8(o, u);
            break;
        }
        default:
            free(out);
            fail(ps, "bad escape");
            return NULL;
        }
    }
    ps->p++;
    *o = '\0';
    *len = (size_t)(o - out);
    return out;
}

static int parse_value(parser *ps, json_value *v, int depth);

static int parse_container(parser *ps, json_value *v, int depth, int is_obj)
{
    size_t cap = 0;
    char close = is_obj ? '}' : ']';
    v->type = is_obj ? JSON_OBJECT : JSON_ARRAY;
    ps->p++;
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == close) {
        ps->p++;
        return 1;
    }
    for (;;) {
        char *key = NULL;
        size_t klen;
        skip_ws(ps);
        if (is_obj) {
            if (ps->p >= ps->end || *ps->p != '"') {
                fail(ps, "expected a key");
                return 0;
            }
            key = parse_string(ps, &klen);
            if (!key)
                return 0;
            skip_ws(ps);
            if (ps->p >= ps->end || *ps->p != ':') {
                free(key);
                fail(ps, "expected ':'");
                return 0;
            }
            ps->p++;
        }
        if (v->n == cap) {
            size_t nc = cap ? cap * 2 : 8;
            json_value *ni = (json_value *)realloc(v->items, nc * sizeof(json_value));
            if (!ni) {
                free(key);
                fail(ps, "out of memory");
                return 0;
            }
            v->items = ni;
            if (is_obj) {
                char **nk = (char **)realloc(v->keys, nc * sizeof(char *));
                if (!nk) {
                    free(key);
                    fail(ps, "out of memory");
                    return 0;
                }
                v->keys = nk;
            }
            cap = nc;
        }
        memset(&v->items[v->n], 0, sizeof(json_value));
        if (is_obj)
            v->keys[v->n] = key;
        v->n++;
        if (!parse_value(ps, &v->items[v->n - 1], depth + 1))
            return 0;
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ',') {
            ps->p++;
            continue;
        }
        if (ps->p < ps->end && *ps->p == close) {
            ps->p++;
            return 1;
        }
        fail(ps, is_obj ? "expected ',' or '}'" : "expected ',' or ']'");
        return 0;
    }
}

static int parse_number(parser *ps, json_value *v)
{
    const char *q = ps->p;
    char buf[64], *endp;
    size_t n;
    int is_int = 1;
    if (q < ps->end && *q == '-')
        q++;
    while (q < ps->end && ((*q >= '0' && *q <= '9') || *q == '.' || *q == 'e' || *q == 'E' ||
                           *q == '+' || *q == '-')) {
        if (*q == '.' || *q == 'e' || *q == 'E')
            is_int = 0;
        q++;
    }
    n = (size_t)(q - ps->p);
    if (n == 0 || n >= sizeof(buf)) {
        fail(ps, "bad number");
        return 0;
    }
    memcpy(buf, ps->p, n);
    buf[n] = '\0';
    v->type = JSON_NUMBER;
    v->num = strtod(buf, &endp);
    if (*endp != '\0') {
        fail(ps, "bad number");
        return 0;
    }
    v->is_int = is_int;
    if (is_int) {
        v->inum = strtoll(buf, &endp, 10);
    }
    ps->p = q;
    return 1;
}

static int literal(parser *ps, const char *word, json_value *v, json_type t)
{
    size_t n = strlen(word);
    if ((size_t)(ps->end - ps->p) < n || memcmp(ps->p, word, n) != 0) {
        fail(ps, "unexpected character");
        return 0;
    }
    ps->p += n;
    v->type = t;
    return 1;
}

static int parse_value(parser *ps, json_value *v, int depth)
{
    if (depth > MAX_DEPTH) {
        fail(ps, "nested too deeply");
        return 0;
    }
    skip_ws(ps);
    if (ps->p >= ps->end) {
        fail(ps, "unexpected end");
        return 0;
    }
    switch (*ps->p) {
    case '{': return parse_container(ps, v, depth, 1);
    case '[': return parse_container(ps, v, depth, 0);
    case '"':
        v->type = JSON_STRING;
        v->str = parse_string(ps, &v->len);
        if (!v->str) {
            v->type = JSON_NULL;
            return 0;
        }
        return 1;
    case 't': return literal(ps, "true", v, JSON_TRUE);
    case 'f': return literal(ps, "false", v, JSON_FALSE);
    case 'n': return literal(ps, "null", v, JSON_NULL);
    default: return parse_number(ps, v);
    }
}

json_value *json_parse(const char *text, size_t n, char *err, size_t errn)
{
    parser ps;
    json_value *v = (json_value *)calloc(1, sizeof(json_value));
    if (errn)
        err[0] = '\0';
    if (!v) {
        if (errn)
            snprintf(err, errn, "out of memory");
        return NULL;
    }
    ps.p = ps.start = text;
    ps.end = text + n;
    ps.err = err;
    ps.errn = errn;
    ps.failed = 0;
    /* A UTF-8 byte order mark. */
    if (n >= 3 && memcmp(text, "\xEF\xBB\xBF", 3) == 0)
        ps.p += 3;
    if (parse_value(&ps, v, 0)) {
        skip_ws(&ps);
        if (ps.p != ps.end)
            fail(&ps, "extra text after the value");
    }
    if (ps.failed) {
        json_free(v);
        return NULL;
    }
    return v;
}

void json_free(json_value *v)
{
    if (v) {
        free_inner(v);
        free(v);
    }
}

const json_value *json_get(const json_value *v, const char *key)
{
    size_t i;
    if (!v || v->type != JSON_OBJECT)
        return NULL;
    for (i = v->n; i-- > 0;)
        if (strcmp(v->keys[i], key) == 0)
            return &v->items[i];
    return NULL;
}

const char *json_str(const json_value *v)
{
    return v && v->type == JSON_STRING ? v->str : NULL;
}
