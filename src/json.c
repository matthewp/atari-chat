#include <string.h>
#include "json.h"

enum { S_VALUE, S_AFTER, S_STRING, S_ESCAPE, S_UNICODE, S_LITERAL, S_DONE };

void json_init(json_parser *p, json_char_fn out, void *ctx)
{
    memset(p, 0, sizeof(*p));
    p->out = out;
    p->ctx = ctx;
    p->state = S_VALUE;
    p->emitting = -1;
}

void json_add_target(json_parser *p, const char *path)
{
    if (p->ntargets < JSON_MAX_TARGETS)
        p->targets[p->ntargets++] = path;
}

/* Does the current position match a target path? Returns its index or -1. */
static int match_target(json_parser *p)
{
    char path[160], num[8];
    int i, t;

    path[0] = '\0';
    for (i = 0; i < p->depth; i++) {
        json_level *l = &p->stack[i];
        const char *part = l->key;
        if (!l->is_object) {
            int v = l->index, n = 0;
            char rev[8];
            do {
                rev[n++] = (char)('0' + v % 10);
                v /= 10;
            } while (v && n < 7);
            for (t = 0; t < n; t++)
                num[t] = rev[n - 1 - t];
            num[n] = '\0';
            part = num;
        }
        if (strlen(path) + strlen(part) + 2 > sizeof(path))
            return -1;
        if (i > 0)
            strcat(path, ".");
        strcat(path, part);
    }
    for (t = 0; t < p->ntargets; t++)
        if (strcmp(path, p->targets[t]) == 0)
            return t;
    return -1;
}

static void emit(json_parser *p, uint32_t cp)
{
    if (p->is_key) {
        if (p->keylen < JSON_MAX_KEY - 1 && cp < 128) {
            json_level *l = &p->stack[p->depth - 1];
            l->key[p->keylen++] = (char)cp;
            l->key[p->keylen] = '\0';
        }
    } else if (p->emitting >= 0) {
        p->out(p->ctx, p->emitting, cp);
    }
}

/* A value just finished: what comes next depends on the container. */
static void value_done(json_parser *p)
{
    p->state = (p->depth == 0) ? S_DONE : S_AFTER;
}

static int fail(json_parser *p)
{
    p->error = 1;
    return -1;
}

static int push(json_parser *p, int is_object)
{
    json_level *l;

    if (p->depth >= JSON_MAX_DEPTH)
        return fail(p);
    l = &p->stack[p->depth++];
    l->is_object = (unsigned char)is_object;
    l->expect_key = (unsigned char)is_object;
    l->index = 0;
    l->key[0] = '\0';
    p->state = S_VALUE;
    return 0;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int feed_char(json_parser *p, char ch)
{
    unsigned char c = (unsigned char)ch;
    json_level *top = p->depth ? &p->stack[p->depth - 1] : NULL;

    switch (p->state) {
    case S_STRING:
        if (p->utf8_left > 0) {                 /* inside a UTF-8 sequence */
            if ((c & 0xc0) != 0x80)
                return fail(p);
            p->utf8 = (p->utf8 << 6) | (c & 0x3f);
            if (--p->utf8_left == 0)
                emit(p, p->utf8);
            return 0;
        }
        if (c == '"') {
            if (p->is_key) {
                p->is_key = 0;
                top->expect_key = 0;
                p->state = S_AFTER;             /* expecting ':' */
            } else {
                p->emitting = -1;
                value_done(p);
            }
        } else if (c == '\\') {
            p->state = S_ESCAPE;
        } else if (c < 0x20) {
            return fail(p);
        } else if (c < 0x80) {
            emit(p, c);
        } else if ((c & 0xe0) == 0xc0) {
            p->utf8 = c & 0x1f; p->utf8_left = 1;
        } else if ((c & 0xf0) == 0xe0) {
            p->utf8 = c & 0x0f; p->utf8_left = 2;
        } else if ((c & 0xf8) == 0xf0) {
            p->utf8 = c & 0x07; p->utf8_left = 3;
        } else {
            return fail(p);
        }
        return 0;

    case S_ESCAPE:
        p->state = S_STRING;
        switch (c) {
        case '"': case '\\': case '/': emit(p, c); break;
        case 'b': emit(p, 8); break;
        case 'f': emit(p, 12); break;
        case 'n': emit(p, '\n'); break;
        case 'r': emit(p, '\r'); break;
        case 't': emit(p, '\t'); break;
        case 'u': p->state = S_UNICODE; p->uval = 0; p->uleft = 4; break;
        default: return fail(p);
        }
        return 0;

    case S_UNICODE: {
        int h = hexval((char)c);
        if (h < 0)
            return fail(p);
        p->uval = (p->uval << 4) | (uint32_t)h;
        if (--p->uleft == 0) {
            /* Characters beyond U+FFFF (emoji...) arrive as two escapes,
               a high and a low surrogate: combine them. */
            if (p->uval >= 0xd800 && p->uval < 0xdc00) {
                p->high_surrogate = p->uval;
            } else if (p->uval >= 0xdc00 && p->uval < 0xe000 && p->high_surrogate) {
                emit(p, 0x10000 + ((p->high_surrogate - 0xd800) << 10) + (p->uval - 0xdc00));
                p->high_surrogate = 0;
            } else {
                emit(p, p->uval);
            }
            p->state = S_STRING;
        }
        return 0;
    }

    case S_LITERAL:
        /* true/false/null/numbers: skip until a delimiter, then reprocess it. */
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '.' || c == '-'
            || c == '+' || c == 'E')
            return 0;
        value_done(p);
        return feed_char(p, ch);

    case S_DONE:
        return (c == ' ' || c == '\t' || c == '\r' || c == '\n') ? 0 : fail(p);
    }

    if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        return 0;

    if (p->state == S_AFTER) {
        if (top == NULL)
            return fail(p);
        if (c == ':' && top->is_object && !top->expect_key) {
            p->state = S_VALUE;
            return 0;
        }
        if (c == ',') {
            if (top->is_object)
                top->expect_key = 1;
            else
                top->index++;
            p->state = S_VALUE;
            return 0;
        }
        if ((c == '}' && top->is_object) || (c == ']' && !top->is_object)) {
            p->depth--;
            value_done(p);
            return 0;
        }
        return fail(p);
    }

    /* S_VALUE: a value (or, in an object, a key) starts here. */
    if (top && top->is_object && top->expect_key) {
        if (c == '"') {
            p->is_key = 1;
            p->keylen = 0;
            top->key[0] = '\0';
            p->state = S_STRING;
            return 0;
        }
        if (c == '}') {                          /* empty object */
            p->depth--;
            value_done(p);
            return 0;
        }
        return fail(p);
    }
    if (c == ']' && top && !top->is_object && top->index == 0) {   /* empty array */
        p->depth--;
        value_done(p);
        return 0;
    }
    switch (c) {
    case '{': return push(p, 1);
    case '[': return push(p, 0);
    case '"':
        p->emitting = match_target(p);
        p->state = S_STRING;
        return 0;
    default:
        if ((c >= '0' && c <= '9') || c == '-' || c == 't' || c == 'f' || c == 'n') {
            p->state = S_LITERAL;
            return 0;
        }
        return fail(p);
    }
}

int json_feed(json_parser *p, const char *buf, size_t len)
{
    size_t i;

    for (i = 0; i < len && !p->error; i++)
        feed_char(p, buf[i]);
    return p->error ? -1 : 0;
}
