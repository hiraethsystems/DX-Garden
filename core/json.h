/*
 * A small JSON reader: the whole text into a tree, for Radio Browser's
 * station pages and Natural Earth's GeoJSON borders. Reading only.
 * Strings come out as UTF-8 (escapes decoded; a lone surrogate becomes
 * U+FFFD). Object members keep their order; with duplicate keys,
 * json_get() finds the last, as Python's json does.
 */
#ifndef DXG_JSON_H
#define DXG_JSON_H

#include <stddef.h>

typedef enum { JSON_NULL, JSON_FALSE, JSON_TRUE, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT }
    json_type;

typedef struct json_value json_value;
struct json_value {
    json_type type;
    /* JSON_NUMBER: the value, and whether it was written as an integer
     * (no fraction or exponent), with that integer if it fits. */
    double num;
    int is_int;
    long long inum;
    /* JSON_STRING: UTF-8, NUL-terminated (may contain NULs from \u0000). */
    char *str;
    size_t len;
    /* JSON_ARRAY and JSON_OBJECT: n items; objects also have keys. */
    size_t n;
    json_value *items;
    char **keys;
};

/* Parses n bytes. Returns NULL on a syntax error, with a message in err
 * (e.g. "line 3: expected ':'"). Free with json_free(). */
json_value *json_parse(const char *text, size_t n, char *err, size_t errn);
void json_free(json_value *v);

/* A member of an object, or NULL (also when v isn't an object). */
const json_value *json_get(const json_value *v, const char *key);
/* The string, or NULL if v is missing or not a string. */
const char *json_str(const json_value *v);

#endif
