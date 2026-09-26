/*
 * First message to Claude from the Atari: TLS to Cloudflare AI Gateway,
 * verify the server, then POST one chat completion and print the reply.
 */
#include <stdio.h>
#include <string.h>
#include <osbind.h>
#include "config.h"
#include "serial.h"
#include "modem.h"
#include "tls/tls.h"
#include "http.h"
#include "json.h"
#include "charset.h"

#define HOST "gateway.ai.cloudflare.com"
#define PATH "/v1/" AIG_ACCOUNT_ID "/" AIG_GATEWAY "/compat/chat/completions"
#define PROMPT "Hello from an Atari ST! Tell me something fun about my machine, in two sentences."

long _stksize = 65536;

static void out(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            Cconout('\r');
        Cconout(*s);
    }
}

static void log_line(const char *s)
{
    out("  ");
    out(s);
    out("\n");
}

static int finish(void)
{
    out("\nPress any key to exit...");
    Cconin();
    return 0;
}

/* ---- the reply: JSON in, Atari characters out, word-wrapped ------------ */

enum { T_CONTENT, T_ERROR };

static int col, got_text, got_error;

static void reply_char(void *ctx, int which, uint32_t cp)
{
    char st[3];
    size_t n, i;

    (void)ctx;
    if (which == T_CONTENT)
        got_text = 1;
    else
        got_error = 1;
    n = charset_from_unicode(cp, st);
    for (i = 0; i < n; i++) {
        if (st[i] == '\n') {
            out("\n");
            col = 0;
            continue;
        }
        if (col >= 78 || (st[i] == ' ' && col >= 70)) {   /* crude wrap */
            out("\n");
            col = 0;
            if (st[i] == ' ')
                continue;
        }
        Cconout(st[i]);
        col++;
    }
}

static json_parser jp;

static void on_body(void *ctx, const char *buf, size_t len)
{
    (void)ctx;
    json_feed(&jp, buf, len);
}

/* Append s to the JSON request as a string literal. */
static void json_string(char *dst, size_t size, const char *s)
{
    size_t n = strlen(dst);

    if (n + 1 < size)
        dst[n++] = '"';
    for (; *s && n + 7 < size; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            dst[n++] = '\\';
            dst[n++] = (char)c;
        } else if (c < 0x20 || c >= 0x80) {
            n += (size_t)sprintf(dst + n, "\\u%04x", c < 0x80 ? c : '?');
        } else {
            dst[n++] = (char)c;
        }
    }
    if (n + 1 < size)
        dst[n++] = '"';
    dst[n] = '\0';
}

int main(void)
{
    static char body[1024];
    tls_stats st;
    long t;
    int status, err;
    const char *auth_headers =
        "cf-aig-authorization: Bearer " AIG_TOKEN "\r\n";

    out("atari-chat: first message to " AIG_MODEL "\n\n");
    serial_init();

    out("Generating key pair...\n");
    tls_prepare();

    out("Dialing " HOST "...\n");
    if (!modem_reset(NULL) || !modem_dial(HOST ":443", log_line)) {
        out("Could not connect.\n");
        return finish();
    }

    out("TLS handshake...\n");
    t = millis();
    if (tls_handshake(HOST, &st) != 0) {
        char line[60];
        sprintf(line, "  failed, BearSSL error %d\n", tls_last_error());
        out(line);
        return finish();
    }

    /* Cloudflare wants a request within ~15 s of connecting, and we're at
       ~12 s. Send a harmless one now (no token), keeping the connection
       open, then verify the server before anything secret goes out. */
    http_begin();
    status = http_request("GET", HOST, "/", NULL, NULL, NULL, 0, 1, NULL, NULL);
    if (status < 0) {
        out("  first request failed\n");
        return finish();
    }

    /* Nothing secret goes out until the server has proven who it is. */
    err = tls_verify(log_line);
    if (err) {
        char line[60];
        sprintf(line, "  server NOT verified (error %d); not sending anything.\n", err);
        out(line);
        return finish();
    }
    {
        char line[80];
        sprintf(line, "  secure connection ready in %ld ms\n\n", millis() - t);
        out(line);
    }

    strcpy(body, "{\"model\":");
    json_string(body, sizeof(body), AIG_MODEL);
    strcat(body, ",\"max_tokens\":300,\"messages\":[{\"role\":\"user\",\"content\":");
    json_string(body, sizeof(body), PROMPT);
    strcat(body, "}]}");
    if (PROVIDER_API_KEY[0])
        auth_headers = "cf-aig-authorization: Bearer " AIG_TOKEN "\r\n"
                       "Authorization: Bearer " PROVIDER_API_KEY "\r\n";

    out("You: " PROMPT "\n\nClaude: ");
    col = 8;
    json_init(&jp, reply_char, NULL);
    json_add_target(&jp, "choices.0.message.content");
    json_add_target(&jp, "error.message");

    t = millis();
    status = http_request("POST", HOST, PATH, auth_headers, "application/json",
                          body, strlen(body), 0, on_body, NULL);
    {
        char line[80];
        out("\n\n");
        if (status != 200 || !got_text) {
            sprintf(line, "HTTP status %d%s\n", status,
                    got_error ? " (message above)" : "");
            out(line);
        }
        sprintf(line, "(reply took %ld ms)\n", millis() - t);
        out(line);
    }
    tls_close();
    return finish();
}
