/*
 * Our X25519 as a BearSSL br_ec_impl, with one twist: the ephemeral key pair
 * can be generated ahead of time (ec_x25519_precompute), because a TLS
 * server only gives us ~15 s for the handshake and one X25519 takes ~8 s.
 *
 * During the handshake BearSSL calls mul() (shared secret) and mulgen() (our
 * public key) with a scalar from its own RNG. When a precomputed pair is
 * loaded we use its scalar for both calls instead, so the two results stay
 * consistent and only mul() costs time. Each pair must be used for exactly
 * one handshake; call ec_x25519_forget() afterwards.
 */
#include <string.h>
#include <bearssl.h>
#include "fe16.h"
#include "ec_x25519.h"

static const unsigned char GEN[32] = { 9 };

static unsigned char pre_scalar[32];
static unsigned char pre_public[32];
static int pre_ready;

void ec_x25519_precompute(const unsigned char *scalar)
{
    memcpy(pre_scalar, scalar, 32);
    memcpy(pre_public, GEN, 32);
    x25519_ladder(pre_public, pre_scalar, 32);
    pre_ready = 1;
}

void ec_x25519_forget(void)
{
    memset(pre_scalar, 0, sizeof(pre_scalar));
    memset(pre_public, 0, sizeof(pre_public));
    pre_ready = 0;
}

static const unsigned char *api_generator(int curve, size_t *len)
{
    return br_ec_c25519_m15.generator(curve, len);
}

static const unsigned char *api_order(int curve, size_t *len)
{
    return br_ec_c25519_m15.order(curve, len);
}

static size_t api_xoff(int curve, size_t *len)
{
    return br_ec_c25519_m15.xoff(curve, len);
}

static uint32_t api_mul(unsigned char *G, size_t Glen,
                        const unsigned char *kb, size_t kblen, int curve)
{
    (void)curve;
    if (Glen != 32 || kblen > 32)
        return 0;
    if (pre_ready)
        x25519_ladder(G, pre_scalar, 32);
    else
        x25519_ladder(G, kb, kblen);
    return 1;
}

static size_t api_mulgen(unsigned char *R, const unsigned char *x, size_t xlen, int curve)
{
    (void)curve;
    if (pre_ready) {
        memcpy(R, pre_public, 32);
    } else {
        memcpy(R, GEN, 32);
        x25519_ladder(R, x, xlen);
    }
    return 32;
}

static uint32_t api_muladd(unsigned char *A, const unsigned char *B, size_t len,
                           const unsigned char *x, size_t xlen,
                           const unsigned char *y, size_t ylen, int curve)
{
    (void)A; (void)B; (void)len; (void)x; (void)xlen; (void)y; (void)ylen; (void)curve;
    return 0;   /* not defined for Curve25519 */
}

const br_ec_impl ec_x25519_atari = {
    (uint32_t)1 << BR_EC_curve25519,
    &api_generator,
    &api_order,
    &api_xoff,
    &api_mul,
    &api_mulgen,
    &api_muladd
};
