#ifndef DEFERRED_H
#define DEFERRED_H

/*
 * Deferred authentication for TLS on a slow CPU.
 *
 * Checking the server's signatures takes minutes on a 68000, but the server
 * only allows ~15 s for the handshake. So during the handshake:
 *   - the X.509 "validator" just stores the certificates and pulls out the
 *     server's public key, and
 *   - the RSA "verifier" records the key-exchange signature and reports the
 *     hash BearSSL expects.
 * Afterwards, deferred_verify() does the real checks. Nothing secret may be
 * sent over the connection until it returns 0.
 */
#include <bearssl.h>

typedef struct {
    const br_x509_class *vtable;
    unsigned char certs[8192];
    size_t cert_start[8], cert_len[8];
    int ncerts;
    size_t used;
    int overflow;
    br_x509_decoder_context leaf;
    char server_name[64];
} deferred_x509_context;

extern const br_x509_class deferred_x509_vtable;

void deferred_init(deferred_x509_context *ctx, br_ssl_client_context *cc);

/* Stand-in for BearSSL's RSA PKCS#1 verifier, to install with br_ssl_engine_set_rsavrfy(). */
uint32_t deferred_rsa_vrfy(const unsigned char *x, size_t xlen,
                           const unsigned char *hash_oid, size_t hash_len,
                           const br_rsa_public_key *pk, unsigned char *hash_out);

/* Progress messages go through this, since checks take minutes. */
typedef void (*deferred_log_fn)(const char *msg);

/*
 * The real checks. Returns 0 if the server is authentic, else a BR_ERR_* code.
 * BR_ERR_X509_TIME_UNKNOWN means the system date isn't set (year < 2025).
 */
int deferred_verify(deferred_x509_context *ctx, deferred_log_fn log);

#endif
