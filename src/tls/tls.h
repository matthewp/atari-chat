#ifndef TLS_H
#define TLS_H

/*
 * TLS 1.2 client over the serial modem link, built on BearSSL.
 * One cipher suite: ECDHE-RSA-CHACHA20-POLY1305 with X25519, the fastest
 * combination on a 68000.
 *
 * Usage:
 *   tls_prepare()        before dialing: seeds the RNG, makes a key pair (~8 s)
 *   (dial with the modem)
 *   tls_handshake(host)  must finish within the server's timeout
 *   tls_verify()         checks the server is authentic (slow). Do not send
 *                        anything secret before this returns 0.
 *   tls_write/tls_read/tls_close
 */
#include <stddef.h>
#include "deferred.h"

typedef struct {
    long first_byte_ms;     /* from handshake start to the server's first byte */
    long server_done_ms;    /* ...to having the server's whole first flight */
    long compute_ms;        /* our X25519 + key derivation */
    long total_ms;          /* handshake start to finished */
    long bytes_in, bytes_out;
} tls_stats;

void tls_prepare(void);
int  tls_handshake(const char *host, tls_stats *stats);
int  tls_verify(deferred_log_fn log);
int  tls_write(const void *buf, size_t len);
int  tls_read(void *buf, size_t len);      /* bytes read, 0 at close, <0 error */
void tls_close(void);
int  tls_last_error(void);

#endif
