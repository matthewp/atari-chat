#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "claude.h"
#include "transcript.h"
#include "serial.h"
#include "modem.h"
#include "tls/tls.h"
#include "http.h"
#include "json.h"
#include "charset.h"

#define HOST "gateway.ai.cloudflare.com"
#define PATH "/v1/" AIG_ACCOUNT_ID "/" AIG_GATEWAY "/compat/chat/completions"
#define MAX_TOKENS "1000"
#define IDLE_LIMIT_MS 45000L    /* reconnect rather than trust an older connection */

static const char system_prompt[] =
    "You are chatting with someone using an Atari ST computer from 1985, "
    "through a program running on the Atari itself. Its screen shows plain "
    "text in 80 columns, in the Atari's own character set. Reply in plain "
    "text only: no Markdown (no #, *, backticks or tables) and no emoji. "
    "Use simple ASCII punctuation. Keep answers reasonably short.";

static claude_status_fn status;
static int connected, key_ready;
static long last_used;

/* ---- connection ------------------------------------------------------ */

static void log_to_status(const char *line)
{
    (void)line;     /* modem chatter isn't interesting in the GUI */
}

void claude_init(claude_status_fn status_fn)
{
    status = status_fn;
    serial_init();
    status("Preparing encryption keys...");
    tls_prepare();
    key_ready = 1;
}

void claude_disconnect(void)
{
    if (connected) {
        tls_close();
        modem_hangup();
        connected = 0;
    }
}

static const char *connect_server(void)
{
    tls_stats st;
    int err;

    if (!key_ready) {
        status("Preparing encryption keys...");
        tls_prepare();
    }
    status("Dialing " HOST "...");
    if (!modem_reset(log_to_status) || !modem_dial(HOST ":443", log_to_status))
        return "Could not connect: no answer from the modem or the server.";

    status("Securing the connection (about 15 seconds)...");
    err = tls_handshake(HOST, &st);
    key_ready = 0;              /* each key pair is good for one handshake */
    if (err) {
        modem_hangup();
        return "Secure connection failed (TLS handshake).";
    }

    /* Cloudflare wants a request within ~15 s of connecting; send a
       harmless one now, then verify before anything secret goes out. */
    http_begin();
    if (http_request("GET", HOST, "/", NULL, NULL, NULL, 0, 1, NULL, NULL) < 0) {
        modem_hangup();
        return "The server closed the connection.";
    }

    status("Verifying the server's identity...");
    err = tls_verify(log_to_status);
    if (err) {
        static char msg[100];
        tls_close();
        modem_hangup();
        if (err == 53)          /* BR_ERR_X509_TIME_UNKNOWN */
            return "Can't verify the server: set the date in the Control Panel.";
        sprintf(msg, "Server could NOT be verified (error %d). Nothing was sent.", err);
        return msg;
    }
    connected = 1;
    last_used = millis();
    return NULL;
}

/* ---- request ----------------------------------------------------------- */

typedef struct {
    char *buf;
    size_t len, cap;
} growbuf;

static int grow_add(growbuf *g, const char *s, size_t n)
{
    if (g->len + n + 1 > g->cap) {
        size_t cap = g->cap ? g->cap * 2 : 1024;
        char *nb;
        while (cap < g->len + n + 1)
            cap *= 2;
        nb = realloc(g->buf, cap);
        if (!nb)
            return 0;
        g->buf = nb;
        g->cap = cap;
    }
    memcpy(g->buf + g->len, s, n);
    g->len += n;
    g->buf[g->len] = '\0';
    return 1;
}

/* Append an Atari-charset string as a JSON string literal (UTF-8 inside). */
static int grow_json_string(growbuf *g, const char *s, size_t n)
{
    size_t i;
    char esc[8];

    if (!grow_add(g, "\"", 1))
        return 0;
    for (i = 0; i < n; i++) {
        uint32_t cp = charset_to_unicode((unsigned char)s[i]);
        if (cp == '"' || cp == '\\') {
            esc[0] = '\\';
            esc[1] = (char)cp;
            if (!grow_add(g, esc, 2))
                return 0;
        } else if (cp == '\n') {
            if (!grow_add(g, "\\n", 2))
                return 0;
        } else if (cp < 0x20 || cp >= 0x80) {
            sprintf(esc, "\\u%04lx", (unsigned long)(cp < 0x10000 ? cp : '?'));
            if (!grow_add(g, esc, 6))
                return 0;
        } else {
            esc[0] = (char)cp;
            if (!grow_add(g, esc, 1))
                return 0;
        }
    }
    return grow_add(g, "\"", 1);
}

static int build_body(growbuf *g)
{
    int i, ok;

    ok = grow_add(g, "{\"model\":", 9)
      && grow_json_string(g, AIG_MODEL, strlen(AIG_MODEL))
      && grow_add(g, ",\"max_tokens\":" MAX_TOKENS ",\"messages\":[{\"role\":\"system\",\"content\":",
                  strlen(",\"max_tokens\":" MAX_TOKENS ",\"messages\":[{\"role\":\"system\",\"content\":"))
      && grow_json_string(g, system_prompt, strlen(system_prompt))
      && grow_add(g, "}", 1);

    for (i = 0; ok && i < transcript_count(); i++) {
        const message *m = transcript_get(i);
        const char *role = m->role == ROLE_USER ? "user" : "assistant";
        if (m->role == ROLE_NOTE)
            continue;
        ok = grow_add(g, ",{\"role\":\"", 10)
          && grow_add(g, role, strlen(role))
          && grow_add(g, "\",\"content\":", 12)
          && grow_json_string(g, m->text, m->len)
          && grow_add(g, "}", 1);
    }
    return ok && grow_add(g, "]}", 2);
}

/* The reply's text and any error message, converted as they arrive. */
static growbuf reply_text, error_text;
static json_parser jp;

static void on_char(void *ctx, int which, uint32_t cp)
{
    char st[3];
    size_t n = charset_from_unicode(cp, st);

    (void)ctx;
    grow_add(which == 0 ? &reply_text : &error_text, st, n);
}

static void on_body(void *ctx, const char *buf, size_t len)
{
    (void)ctx;
    json_feed(&jp, buf, len);
}

static int post_once(growbuf *body)
{
    static const char headers[] =
        "cf-aig-authorization: Bearer " AIG_TOKEN "\r\n";
    static const char headers_with_key[] =
        "cf-aig-authorization: Bearer " AIG_TOKEN "\r\n"
        "Authorization: Bearer " PROVIDER_API_KEY "\r\n";

    reply_text.len = error_text.len = 0;
    json_init(&jp, on_char, NULL);
    json_add_target(&jp, "choices.0.message.content");
    json_add_target(&jp, "error.message");
    status("Waiting for Claude...");
    return http_request("POST", HOST, PATH,
                        PROVIDER_API_KEY[0] ? headers_with_key : headers,
                        "application/json", body->buf, body->len, 1, on_body, NULL);
}

const char *claude_ask(char **reply, size_t *reply_len)
{
    static char msg[200];
    growbuf body = { 0 };
    const char *err;
    int http_status, attempt;

    *reply = NULL;
    if (!build_body(&body)) {
        free(body.buf);
        return "Out of memory.";
    }

    for (attempt = 0; attempt < 2; attempt++) {
        /* Anything arriving while idle means the server hung up (a TLS
           alert, or the modem's NO CARRIER). Old connections get replaced. */
        if (connected && (serial_avail() || millis() - last_used > IDLE_LIMIT_MS))
            claude_disconnect();
        if (!connected) {
            err = connect_server();
            if (err) {
                free(body.buf);
                return err;
            }
        }
        http_status = post_once(&body);
        last_used = millis();
        if (http_status >= 0)
            break;
        claude_disconnect();    /* dropped mid-request: reconnect, try once more */
    }
    free(body.buf);

    if (http_status < 0)
        return "Lost the connection while waiting for the reply.";
    if (http_status != 200) {
        snprintf(msg, sizeof(msg), "Error %d from the server%s%.150s", http_status,
                 error_text.len ? ": " : ".", error_text.len ? error_text.buf : "");
        return msg;
    }
    if (reply_text.len == 0)
        return "The reply was empty.";

    *reply = malloc(reply_text.len + 1);
    if (!*reply)
        return "Out of memory.";
    memcpy(*reply, reply_text.buf, reply_text.len + 1);
    *reply_len = reply_text.len;
    return NULL;
}
