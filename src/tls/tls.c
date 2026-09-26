#include <string.h>
#include <bearssl.h>
#include "tls.h"
#include "entropy.h"
#include "serial.h"
#include "x25519/ec_x25519.h"

#define READ_TIMEOUT_MS 30000L

static br_ssl_client_context cc;
static deferred_x509_context xc;
static br_sslio_context ioc;
static unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
static unsigned char seed[32];

static const uint16_t suites[] = {
    BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256
};

/* ---- serial I/O for BearSSL -------------------------------------------- */

static long io_in, io_out;

static int serial_read_cb(void *ctx, unsigned char *buf, size_t len)
{
    size_t n = 0;
    int c;

    (void)ctx;
    c = serial_getc_timeout(READ_TIMEOUT_MS);
    if (c < 0)
        return -1;
    buf[n++] = (unsigned char)c;
    while (n < len && serial_avail())
        buf[n++] = (unsigned char)serial_getc();
    io_in += n;
    return (int)n;
}

static int serial_write_cb(void *ctx, const unsigned char *buf, size_t len)
{
    (void)ctx;
    serial_write((const char *)buf, (long)len);
    io_out += len;
    return (int)len;
}

/* ---- setup ------------------------------------------------------------- */

void tls_prepare(void)
{
    br_hmac_drbg_context rng;
    unsigned char scalar[32];

    entropy_gather(seed);
    br_hmac_drbg_init(&rng, &br_sha256_vtable, seed, sizeof(seed));
    br_hmac_drbg_generate(&rng, scalar, sizeof(scalar));
    br_hmac_drbg_generate(&rng, seed, sizeof(seed));   /* separate seed for BearSSL */

    ec_x25519_precompute(scalar);
    memset(scalar, 0, sizeof(scalar));
}

static void configure(void)
{
    br_ssl_engine_context *eng = &cc.eng;

    br_ssl_client_zero(&cc);
    br_ssl_engine_set_versions(eng, BR_TLS12, BR_TLS12);
    br_ssl_engine_set_suites(eng, suites, sizeof(suites) / sizeof(suites[0]));

    br_ssl_engine_set_hash(eng, br_sha256_ID, &br_sha256_vtable);
    br_ssl_engine_set_prf_sha256(eng, &br_tls12_sha256_prf);

    br_ssl_engine_set_chapol(eng, &br_sslrec_in_chapol_vtable, &br_sslrec_out_chapol_vtable);
    br_ssl_engine_set_chacha20(eng, &br_chacha20_ct_run);
    br_ssl_engine_set_poly1305(eng, &br_poly1305_ctmul_run);

    br_ssl_engine_set_ec(eng, &ec_x25519_atari);
    br_ssl_engine_set_rsavrfy(eng, &deferred_rsa_vrfy);

    deferred_init(&xc, &cc);
    br_ssl_engine_set_x509(eng, &xc.vtable);

    br_ssl_engine_set_buffer(eng, iobuf, sizeof(iobuf), 1);
    br_ssl_engine_inject_entropy(eng, seed, sizeof(seed));
}

/* ---- handshake --------------------------------------------------------- */

int tls_handshake(const char *host, tls_stats *st)
{
    br_ssl_engine_context *eng = &cc.eng;
    long start, last_rx = 0;
    int sent_after_rx = 0;

    memset(st, 0, sizeof(*st));
    io_in = io_out = 0;
    configure();
    if (!br_ssl_client_reset(&cc, host, 0))
        return -1;

    /* Drive the engine by hand (instead of br_sslio) to time each stage. */
    start = millis();
    for (;;) {
        unsigned state = br_ssl_engine_current_state(eng);
        unsigned char *buf;
        size_t len;

        if (state & BR_SSL_CLOSED)
            return -1;

        if (state & BR_SSL_SENDREC) {
            if (io_in > 0 && !sent_after_rx) {
                /* First thing we send after the server's flight: the time in
                   between was our key exchange computation. */
                st->server_done_ms = last_rx - start;
                st->compute_ms = millis() - last_rx;
                sent_after_rx = 1;
            }
            buf = br_ssl_engine_sendrec_buf(eng, &len);
            serial_write_cb(NULL, buf, len);
            br_ssl_engine_sendrec_ack(eng, len);
            continue;
        }

        if (state & BR_SSL_SENDAPP)
            break;              /* handshake complete */

        if (state & BR_SSL_RECVREC) {
            int n;
            buf = br_ssl_engine_recvrec_buf(eng, &len);
            n = serial_read_cb(NULL, buf, len);
            if (n < 0)
                return -1;
            if (st->first_byte_ms == 0)
                st->first_byte_ms = millis() - start;
            last_rx = millis();
            br_ssl_engine_recvrec_ack(eng, (size_t)n);
            continue;
        }
    }

    st->total_ms = millis() - start;
    st->bytes_in = io_in;
    st->bytes_out = io_out;
    ec_x25519_forget();

    br_sslio_init(&ioc, eng, serial_read_cb, NULL, serial_write_cb, NULL);
    return 0;
}

int tls_last_error(void)
{
    return br_ssl_engine_last_error(&cc.eng);
}

int tls_verify(deferred_log_fn log)
{
    return deferred_verify(&xc, log);
}

/* ---- application data -------------------------------------------------- */

int tls_write(const void *buf, size_t len)
{
    if (br_sslio_write_all(&ioc, buf, len) < 0)
        return -1;
    return br_sslio_flush(&ioc);
}

int tls_read(void *buf, size_t len)
{
    int n = br_sslio_read(&ioc, buf, len);

    if (n < 0) {
        int err = br_ssl_engine_last_error(&cc.eng);
        /* A plain close without close_notify still counts as end of data. */
        return (err == BR_ERR_OK || err == BR_ERR_IO) ? 0 : -1;
    }
    return n;
}

void tls_close(void)
{
    br_sslio_close(&ioc);
}
