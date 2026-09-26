#include <stdlib.h>
#include <string.h>
#include "transcript.h"

#define MAX_MESSAGES 200
#define MAX_LINES 4000

static message messages[MAX_MESSAGES];
static int nmessages;
static tline lines[MAX_LINES];
static int nlines;
static int layout_cols = 80;

void transcript_init(void)
{
    nmessages = 0;
    nlines = 0;
}

int transcript_add(role_t role, const char *text, size_t len)
{
    char *copy;

    if (nmessages == MAX_MESSAGES) {
        /* Forget the oldest message to make room. */
        free(messages[0].text);
        memmove(messages, messages + 1, sizeof(message) * (MAX_MESSAGES - 1));
        nmessages--;
    }
    copy = malloc(len + 1);
    if (!copy)
        return -1;
    memcpy(copy, text, len);
    copy[len] = '\0';
    messages[nmessages].role = role;
    messages[nmessages].text = copy;
    messages[nmessages].len = len;
    nmessages++;
    transcript_layout(layout_cols);
    return nmessages - 1;
}

void transcript_remove_last(void)
{
    if (nmessages > 0) {
        free(messages[--nmessages].text);
        transcript_layout(layout_cols);
    }
}

int transcript_count(void)
{
    return nmessages;
}

const message *transcript_get(int i)
{
    return (i >= 0 && i < nmessages) ? &messages[i] : NULL;
}

static void add_line(int msg, size_t start, size_t len, int first)
{
    if (nlines == MAX_LINES) {
        memmove(lines, lines + 1, sizeof(tline) * (MAX_LINES - 1));
        nlines--;
    }
    lines[nlines].msg = msg;
    lines[nlines].start = start;
    lines[nlines].len = (unsigned short)len;
    lines[nlines].first = (unsigned char)first;
    nlines++;
}

/* Word-wrap one message into lines of at most `width` characters. */
static void wrap(int m, int width)
{
    const char *t = messages[m].text;
    size_t len = messages[m].len, pos = 0;
    int first = 1;

    if (width < 10)
        width = 10;
    while (pos < len || first) {
        size_t end = pos, brk = 0, next;

        /* Take characters up to the width or a newline, remembering the
           last space as a break point. */
        while (end < len && t[end] != '\n' && end - pos < (size_t)width) {
            if (t[end] == ' ')
                brk = end;
            end++;
        }
        if (end < len && (t[end] == '\n' || t[end] == ' ')) {
            next = end + 1;             /* newline, or a space right at the edge */
        } else if (end < len && brk > pos) {
            next = brk + 1;             /* break at the space, drop it */
            end = brk;
        } else {
            next = end;                 /* one long word: hard break */
            while (next < len && t[next] == ' ' && end - pos == (size_t)width)
                next++;
        }
        add_line(m, pos, end - pos, first);
        first = 0;
        pos = next;
        if (pos >= len)
            break;
    }
}

void transcript_layout(int cols)
{
    int m;

    layout_cols = cols;
    nlines = 0;
    for (m = 0; m < nmessages; m++) {
        if (m > 0)
            add_line(-1, 0, 0, 0);
        wrap(m, cols - LABEL_WIDTH);
    }
}

int transcript_lines(void)
{
    return nlines;
}

const tline *transcript_line(int i)
{
    return (i >= 0 && i < nlines) ? &lines[i] : NULL;
}
