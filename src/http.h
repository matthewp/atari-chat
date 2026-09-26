#ifndef HTTP_H
#define HTTP_H

/*
 * Minimal HTTP/1.1 client over the TLS connection. Several requests can
 * share one connection (keep_alive), which matters here: Cloudflare wants
 * the first request within ~15 s of connecting, but our handshake takes 12 s
 * and verifying the server takes 7 s more. So a harmless first request goes
 * out right after the handshake, and the real one after verification.
 * The response body is handed over in pieces as it arrives, de-chunked.
 */
#include <stddef.h>

typedef void (*http_body_fn)(void *ctx, const char *buf, size_t len);

/* Call once per new connection. */
void http_begin(void);

/*
 * Send a request to https://host/path and read the response. method is
 * "GET" or "POST"; body may be NULL for none. extra_headers is zero or more
 * complete "Name: value\r\n" lines. on_body may be NULL to discard the body.
 * Returns the HTTP status code, or -1 on a connection or protocol error.
 */
int http_request(const char *method, const char *host, const char *path,
                 const char *extra_headers, const char *content_type,
                 const char *body, size_t body_len, int keep_alive,
                 http_body_fn on_body, void *ctx);

#endif
