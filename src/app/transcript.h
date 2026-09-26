#ifndef TRANSCRIPT_H
#define TRANSCRIPT_H

/*
 * The conversation: messages in the Atari character set, plus a word-wrapped
 * layout of them for a window of a given width (in characters).
 */
#include <stddef.h>

typedef enum { ROLE_USER, ROLE_ASSISTANT, ROLE_NOTE } role_t;  /* NOTE: status/errors, not sent */

typedef struct {
    role_t role;
    char *text;
    size_t len;
} message;

/* One screen line: part of a message, possibly the first (gets the label). */
typedef struct {
    int msg;                /* -1 for a blank separator line */
    size_t start;
    unsigned short len;
    unsigned char first;
} tline;

#define LABEL_WIDTH 8       /* "Claude: " */

void transcript_init(void);
int transcript_add(role_t role, const char *text, size_t len);   /* returns index or -1 */
void transcript_remove_last(void);
int transcript_count(void);
const message *transcript_get(int i);

/* Re-wrap everything for a text area `cols` characters wide. */
void transcript_layout(int cols);
int transcript_lines(void);
const tline *transcript_line(int i);

#endif
