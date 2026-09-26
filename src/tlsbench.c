/*
 * Time the crypto that a TLS connection needs, on the real (or emulated)
 * 8 MHz 68000. Each BearSSL primitive has several implementations tuned for
 * different CPUs; this tells us which ones to use and how slow a handshake
 * will be.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <osbind.h>
#include <bearssl.h>
#include "serial.h"

static unsigned char buf[4096];

static void out(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            Cconout('\r');
        Cconout(*s);
    }
}

static void report(const char *name, long ms, long bytes)
{
    char line[100];

    if (bytes > 0) {
        long bps = ms > 0 ? bytes * 1000L / ms : 0;
        sprintf(line, "  %-28s %7ld ms  %6ld bytes/s\n", name, ms, bps);
    } else {
        sprintf(line, "  %-28s %7ld ms\n", name, ms);
    }
    out(line);
}

static void fill(unsigned char *p, size_t len, unsigned seed)
{
    size_t i;
    for (i = 0; i < len; i++)
        p[i] = (unsigned char)(seed = seed * 1103515245u + 12345u) >> 16;
}

/* ---- symmetric --------------------------------------------------------- */

static void bench_sha256(void)
{
    br_sha256_context ctx;
    unsigned char out_hash[32];
    long t = millis();
    br_sha256_init(&ctx);
    br_sha256_update(&ctx, buf, sizeof(buf));
    br_sha256_out(&ctx, out_hash);
    report("SHA-256", millis() - t, sizeof(buf));
}

static void bench_chacha(void)
{
    unsigned char key[32], iv[12];
    long t;
    fill(key, 32, 1);
    fill(iv, 12, 2);
    t = millis();
    br_chacha20_ct_run(key, iv, 0, buf, sizeof(buf));
    report("ChaCha20 (ct)", millis() - t, sizeof(buf));
}

static void bench_poly(const char *name, br_poly1305_run run)
{
    unsigned char key[32], iv[12], tag[16];
    long t;
    fill(key, 32, 3);
    fill(iv, 12, 4);
    t = millis();
    run(key, iv, buf, sizeof(buf), NULL, 0, tag, br_chacha20_ct_run, 0);
    report(name, millis() - t, sizeof(buf));
}

static void bench_aes_ctr(const char *name, const br_block_ctr_class *cls)
{
    br_aes_gen_ctr_keys ctx;
    unsigned char key[16], iv[12];
    long t;
    fill(key, 16, 5);
    fill(iv, 12, 6);
    cls->init(&ctx.vtable, key, 16);
    t = millis();
    cls->run(&ctx.vtable, iv, 1, buf, sizeof(buf));
    report(name, millis() - t, sizeof(buf));
}

static void bench_ghash(const char *name, br_ghash gh)
{
    unsigned char y[16], h[16];
    long t;
    fill(y, 16, 7);
    fill(h, 16, 8);
    t = millis();
    gh(y, h, buf, sizeof(buf));
    report(name, millis() - t, sizeof(buf));
}

/* ---- public key -------------------------------------------------------- */

static void bench_x25519(const char *name, const br_ec_impl *impl)
{
    unsigned char g[32], k[32];
    long t;
    memset(g, 0, 32);
    g[0] = 9;                      /* the curve25519 base point */
    fill(k, 32, 9);
    t = millis();
    impl->mul(g, 32, k, 32, BR_EC_curve25519);
    report(name, millis() - t, 0);
}

static void bench_p256_mulgen(const char *name, const br_ec_impl *impl)
{
    unsigned char r[65], k[32];
    long t;
    fill(k, 32, 10);
    k[0] &= 0x7f;
    t = millis();
    impl->mulgen(r, k, 32, BR_EC_secp256r1);
    report(name, millis() - t, 0);
}

static void bench_p256_muladd(const char *name, const br_ec_impl *impl)
{
    unsigned char a[65], x[32], y[32];
    size_t glen;
    long t;
    const unsigned char *gen = impl->generator(BR_EC_secp256r1, &glen);

    memcpy(a, gen, glen);
    fill(x, 32, 11);
    fill(y, 32, 12);
    x[0] &= 0x7f;
    y[0] &= 0x7f;
    t = millis();
    impl->muladd(a, NULL, glen, x, 32, y, 32, BR_EC_secp256r1);
    report(name, millis() - t, 0);
}

static void bench_rsa(const char *name, br_rsa_public pub)
{
    static unsigned char n[256], x[256];
    static const unsigned char e[] = { 0x01, 0x00, 0x01 };
    br_rsa_public_key pk;
    long t;
    fill(n, sizeof(n), 13);
    n[0] |= 0x80;
    n[255] |= 0x01;                /* modulus must be odd */
    fill(x, sizeof(x), 14);
    x[0] &= 0x3f;                  /* x < n */
    pk.n = n;
    pk.nlen = sizeof(n);
    pk.e = (unsigned char *)e;
    pk.elen = sizeof(e);
    t = millis();
    pub(x, sizeof(x), &pk);
    report(name, millis() - t, 0);
}

int main(void)
{
    fill(buf, sizeof(buf), 42);

    out("atari-chat TLS crypto benchmark (4 KB blocks)\n\n");

    out("Hashing / bulk encryption:\n");
    bench_sha256();
    bench_chacha();
    bench_poly("Poly1305 (ctmul)", br_poly1305_ctmul_run);
    bench_poly("Poly1305 (ctmul32)", br_poly1305_ctmul32_run);
    bench_poly("Poly1305 (i15)", br_poly1305_i15_run);
    bench_aes_ctr("AES-128-CTR (small)", &br_aes_small_ctr_vtable);
    bench_aes_ctr("AES-128-CTR (ct)", &br_aes_ct_ctr_vtable);
    bench_ghash("GHASH (ctmul)", br_ghash_ctmul);
    bench_ghash("GHASH (ctmul32)", br_ghash_ctmul32);

    out("\nKey exchange (one ECDH operation):\n");
    bench_x25519("X25519 (m15)", &br_ec_c25519_m15);
    bench_x25519("X25519 (i15)", &br_ec_c25519_i15);
    bench_x25519("X25519 (m31)", &br_ec_c25519_m31);
    bench_p256_mulgen("P-256 keygen (m15)", &br_ec_p256_m15);

    out("\nSignature verification:\n");
    bench_p256_muladd("ECDSA P-256 verify (m15)", &br_ec_p256_m15);
    bench_p256_muladd("ECDSA P-256 verify (m31)", &br_ec_p256_m31);
    bench_rsa("RSA-2048 verify (i15)", br_rsa_i15_public);
    bench_rsa("RSA-2048 verify (i31)", br_rsa_i31_public);
    bench_rsa("RSA-2048 verify (i32)", br_rsa_i32_public);

    out("\nDone. Press any key...");
    Cconin();
    return 0;
}
