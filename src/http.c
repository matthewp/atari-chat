#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "http.h"
#include "tls/tls.h"

/* ---- buffered reading from the TLS connection -------------------------- */

static char rbuf[512];
static size_t rpos, rlen;
static int reof;

static int getbyte(void)
{
    if (rpos == rlen) {
        int n;
        if (reof)
            return -1;
        n = tls_read(rbuf, sizeof(rbuf));
        if (n <= 0) {
            reof = 1;
            return -1;
        }
        rpos = 0;
        rlen = (size_t)n;
    }
    return (unsigned char)rbuf[rpos++];
}

/* Read one line without its CRLF. Returns length, or -1 at end of data. */
static int getline_crlf(char *line, size_t size)
{
    size_t n = 0;
    int c;

    while ((c = getbyte()) >= 0) {
        if (c == '\n') {
            if (n > 0 && line[n - 1] == '\r')
                n--;
            line[n] = '\0';
            return (int)n;
        }
        if (n < size - 1)
            line[n++] = (char)c;
    }
    return -1;
}

static int starts_with_nocase(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++) {
        char a = *s, b = *prefix;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b)
            return 0;
    }
    return 1;
}

/* Pass up to len body bytes (or all remaining, if len < 0) to on_body. */
static int pass_body(long len, http_body_fn on_body, void *ctx)
{
    while (len != 0) {
        size_t avail, n;
        if (rpos == rlen && getbyte() >= 0)
            rpos--;                         /* refill, then un-read */
        avail = rlen - rpos;
        if (avail == 0)
            return len < 0 ? 0 : -1;        /* EOF: fine only if unsized */
        n = (len >= 0 && (size_t)len < avail) ? (size_t)len : avail;
        on_body(ctx, rbuf + rpos, n);
        rpos += n;
        if (len > 0)
            len -= (long)n;
    }
    return 0;
}

/* ---- the request ------------------------------------------------------- */

static void discard(void *ctx, const char *buf, size_t len)
{
    (void)ctx; (void)buf; (void)len;
}

void http_begin(void)
{
    rpos = rlen = 0;
    reof = 0;
}

int http_request(const char *method, const char *host, const char *path,
                 const char *extra_headers, const char *content_type,
                 const char *body, size_t body_len, int keep_alive,
                 http_body_fn on_body, void *ctx)
{
    static char head[1024];
    char line[256], length_hdr[80] = "";
    long content_length = -1;
    int chunked = 0, status;

    if (on_body == NULL)
        on_body = discard;
    if (body)
        snprintf(length_hdr, sizeof(length_hdr),
                 "Content-Type: %s\r\nContent-Length: %lu\r\n",
                 content_type, (unsigned long)body_len);
    snprintf(head, sizeof(head),
             "%s %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: atari-chat/0.1 (Atari ST)\r\n"
             "%s"
             "Connection: %s\r\n"
             "%s"
             "\r\n",
             method, path, host, length_hdr, keep_alive ? "keep-alive" : "close",
             extra_headers ? extra_headers : "");
    if (tls_write(head, strlen(head)) < 0 || (body && tls_write(body, body_len) < 0))
        return -1;

    /* Status line: "HTTP/1.1 200 OK" */
    if (getline_crlf(line, sizeof(line)) < 0 || strncmp(line, "HTTP/1.", 7) != 0)
        return -1;
    status = atoi(line + 9);

    /* Headers, up to the blank line. */
    for (;;) {
        if (getline_crlf(line, sizeof(line)) < 0)
            return -1;
        if (line[0] == '\0')
            break;
        if (starts_with_nocase(line, "content-length:"))
            content_length = atol(line + 15);
        else if (starts_with_nocase(line, "transfer-encoding:") && strstr(line, "chunked"))
            chunked = 1;
    }

    if (!chunked)
        return pass_body(content_length, on_body, ctx) < 0 ? -1 : status;

    /* Chunked: "<hex size>\r\n<data>\r\n" ... "0\r\n\r\n" */
    for (;;) {
        long size;
        if (getline_crlf(line, sizeof(line)) < 0)
            return -1;
        size = strtol(line, NULL, 16);
        if (size == 0)
            break;
        if (pass_body(size, on_body, ctx) < 0 || getline_crlf(line, sizeof(line)) < 0)
            return -1;
    }
    return status;
}
