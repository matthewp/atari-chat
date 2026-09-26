#ifndef JSON_H
#define JSON_H

/*
 * Streaming JSON parser that pulls string values out of a document as the
 * bytes arrive, without ever holding the whole document. You name the paths
 * you care about, like "choices.0.message.content" (object keys and array
 * indices, dot-separated), and get each decoded character of matching
 * strings through a callback, as Unicode code points.
 */
#include <stddef.h>
#include <stdint.h>

#define JSON_MAX_DEPTH 16
#define JSON_MAX_KEY 32
#define JSON_MAX_TARGETS 4

/* which: index of the matching target path. */
typedef void (*json_char_fn)(void *ctx, int which, uint32_t codepoint);

typedef struct {
    unsigned char is_object;
    unsigned char expect_key;
    int index;
    char key[JSON_MAX_KEY];
} json_level;

typedef struct {
    const char *targets[JSON_MAX_TARGETS];
    int ntargets;
    json_char_fn out;
    void *ctx;

    json_level stack[JSON_MAX_DEPTH];
    int depth;
    int state;
    int emitting;           /* target index of the string being read, or -1 */
    int is_key;
    int keylen;
    uint32_t uval;          /* \uXXXX being read */
    int uleft;
    uint32_t high_surrogate; /* first half of a \uD83D\uDE00-style pair */
    uint32_t utf8;          /* UTF-8 sequence being read */
    int utf8_left;
    int error;
} json_parser;

void json_init(json_parser *p, json_char_fn out, void *ctx);
void json_add_target(json_parser *p, const char *path);

/* Returns 0, or -1 once the input is malformed. */
int json_feed(json_parser *p, const char *buf, size_t len);

#endif
