/*
 * Field arithmetic mod 2^255-19 and the X25519 Montgomery ladder.
 * The ladder mirrors BearSSL's ec_c25519_m15.c; only the field layer differs.
 * Everything here runs in constant time except MULU.W itself, whose timing
 * depends on operand bits. X25519 keys here are ephemeral (one per TLS
 * connection), so an attacker cannot collect enough timings to exploit it.
 */
#include <stddef.h>
#include <string.h>
#include "fe16.h"

/* Add c*38 into r (2^256 = 38 mod p), propagating carries. Returns carry out. */
static uint32_t fold(fe16 r, uint32_t c)
{
    int i;

    c *= 38;
    for (i = 0; i < 16; i++) {
        c += r[i];
        r[i] = (uint16_t)c;
        c >>= 16;
    }
    return c;
}

void fe16_add(fe16 r, const fe16 a, const fe16 b)
{
    uint32_t c = 0;
    int i;

    for (i = 0; i < 16; i++) {
        c += (uint32_t)a[i] + b[i];
        r[i] = (uint16_t)c;
        c >>= 16;
    }
    /* The second fold can only carry if the first one wrapped. */
    fold(r, fold(r, c));
}

void fe16_sub(fe16 r, const fe16 a, const fe16 b)
{
    int32_t c = 0;
    int i;

    for (i = 0; i < 16; i++) {
        c += (int32_t)a[i] - b[i];
        r[i] = (uint16_t)c;
        c >>= 16;                   /* arithmetic shift: 0 or -1 */
    }
    /* A borrow means we computed a - b + 2^256; subtract 38 to fix. Twice,
       for the (tiny) chance the first correction borrows again. */
    for (int pass = 0; pass < 2; pass++) {
        int32_t d = c * 38;
        for (i = 0; i < 16; i++) {
            d += r[i];
            r[i] = (uint16_t)d;
            d >>= 16;
        }
        c = d;
    }
}

void fe16_mul_a24(fe16 r, const fe16 a)
{
    /* 121665 = 0x1DB41 = 0xDB41 + 2^16: a*0xDB41 plus a shifted up one limb. */
    uint32_t c = 0;
    uint16_t prev = 0, cur;
    int i;

    for (i = 0; i < 16; i++) {
        cur = a[i];                 /* read before write: r may alias a */
        c += (uint32_t)cur * 0xDB41u + prev;
        r[i] = (uint16_t)c;
        c >>= 16;
        prev = cur;
    }
    fold(r, fold(r, c + prev));
}

void fe16_cswap(fe16 a, fe16 b, uint16_t swap)
{
    uint16_t mask = (uint16_t)-swap;
    int i;

    for (i = 0; i < 16; i++) {
        uint16_t t = mask & (a[i] ^ b[i]);
        a[i] ^= t;
        b[i] ^= t;
    }
}

void fe16_invert(fe16 r, const fe16 z)
{
    /* z^(p-2), same addition chain as BearSSL. */
    fe16 a, b;
    int i, j;

    memcpy(a, z, sizeof(fe16));
    for (i = 0; i < 15; i++) {
        fe16_sq(a, a);
        fe16_mul(a, a, z);
    }
    memcpy(b, a, sizeof(fe16));
    for (i = 0; i < 14; i++) {
        for (j = 0; j < 16; j++)
            fe16_sq(b, b);
        fe16_mul(b, b, a);
    }
    for (i = 14; i >= 0; i--) {
        fe16_sq(b, b);
        if ((0xFFEB >> i) & 1)      /* public exponent: branching is fine */
            fe16_mul(b, z, b);
    }
    memcpy(r, b, sizeof(fe16));
}

void fe16_from_bytes(fe16 r, const unsigned char *s)
{
    int i;

    for (i = 0; i < 16; i++)
        r[i] = (uint16_t)(s[2 * i] | (s[2 * i + 1] << 8));
}

void fe16_to_bytes(unsigned char *s, const fe16 a)
{
    fe16 t, u;
    uint32_t c;
    uint16_t mask;
    int i;

    memcpy(t, a, sizeof(fe16));

    /* Fold bit 255 down (2^255 = 19 mod p), twice to be sure t < 2^255. */
    for (int pass = 0; pass < 2; pass++) {
        c = (uint32_t)(t[15] >> 15) * 19;
        t[15] &= 0x7fff;
        for (i = 0; i < 16; i++) {
            c += t[i];
            t[i] = (uint16_t)c;
            c >>= 16;
        }
    }

    /* Now t < 2^255 < 2p. If t + 19 >= 2^255 then t >= p: use t - p. */
    c = 19;
    for (i = 0; i < 16; i++) {
        c += t[i];
        u[i] = (uint16_t)c;
        c >>= 16;
    }
    mask = (uint16_t)-(u[15] >> 15);
    u[15] &= 0x7fff;
    for (i = 0; i < 16; i++)
        t[i] ^= mask & (t[i] ^ u[i]);

    for (i = 0; i < 16; i++) {
        s[2 * i] = (unsigned char)t[i];
        s[2 * i + 1] = (unsigned char)(t[i] >> 8);
    }
}

void x25519_ladder(unsigned char *point, const unsigned char *kb, size_t kblen)
{
    fe16 x1, x2, x3, z2, z3;
    fe16 a, aa, b, bb, c, d, e, da, cb;
    unsigned char k[32];
    uint16_t swap = 0;
    int i;

    point[31] &= 0x7f;
    fe16_from_bytes(x1, point);
    memcpy(x3, x1, sizeof(fe16));
    memset(x2, 0, sizeof(fe16));
    x2[0] = 1;
    memset(z2, 0, sizeof(fe16));
    memset(z3, 0, sizeof(fe16));
    z3[0] = 1;

    memset(k, 0, sizeof(k) - kblen);
    memcpy(k + sizeof(k) - kblen, kb, kblen);
    k[31] &= 0xF8;
    k[0] &= 0x7F;
    k[0] |= 0x40;

    for (i = 254; i >= 0; i--) {
        uint16_t kt = (k[31 - (i >> 3)] >> (i & 7)) & 1;

        swap ^= kt;
        fe16_cswap(x2, x3, swap);
        fe16_cswap(z2, z3, swap);
        swap = kt;

        fe16_add(a, x2, z2);
        fe16_sq(aa, a);
        fe16_sub(b, x2, z2);
        fe16_sq(bb, b);
        fe16_sub(e, aa, bb);
        fe16_add(c, x3, z3);
        fe16_sub(d, x3, z3);
        fe16_mul(da, d, a);
        fe16_mul(cb, c, b);

        fe16_add(x3, da, cb);
        fe16_sq(x3, x3);
        fe16_sub(z3, da, cb);
        fe16_sq(z3, z3);
        fe16_mul(z3, z3, x1);
        fe16_mul(x2, aa, bb);
        fe16_mul_a24(z2, e);
        fe16_add(z2, z2, aa);
        fe16_mul(z2, e, z2);
    }
    fe16_cswap(x2, x3, swap);
    fe16_cswap(z2, z3, swap);

    fe16_invert(z2, z2);
    fe16_mul(x2, x2, z2);
    fe16_to_bytes(point, x2);
}
