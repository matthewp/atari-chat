#include <string.h>
#include "rsa16.h"

#define MAXL (RSA16_MAX_BITS / 16)

/* From BearSSL (inner.h): checks PKCS#1 v1.5 padding, extracts the hash. */
uint32_t br_rsa_pkcs1_sig_unpad(const unsigned char *sig, size_t sig_len,
                                const unsigned char *hash_oid, size_t hash_len,
                                unsigned char *hash_out);

uint16_t rsa16_addmul_1_ref(uint16_t *t, const uint16_t *a, unsigned len, uint16_t w)
{
    uint32_t c = 0;
    unsigned i;

    for (i = 0; i < len; i++) {
        c += (uint32_t)a[i] * w + t[i];
        t[i] = (uint16_t)c;
        c >>= 16;
    }
    return (uint16_t)c;
}

/* A modulus and what Montgomery arithmetic needs to know about it. */
typedef struct {
    uint16_t n[MAXL];
    unsigned len;           /* limbs */
    unsigned bits;
    uint16_t n0i;           /* -n^-1 mod 2^16 */
} modulus;

static void decode_be(uint16_t *r, unsigned len, const unsigned char *b, size_t blen)
{
    unsigned i;

    memset(r, 0, len * 2);
    for (i = 0; i < blen; i++) {
        unsigned bit = (unsigned)(blen - 1 - i) * 8;
        r[bit / 16] |= (uint16_t)(b[i] << (bit % 16));
    }
}

static void encode_be(unsigned char *b, size_t blen, const uint16_t *r)
{
    unsigned i;

    for (i = 0; i < blen; i++) {
        unsigned bit = (unsigned)(blen - 1 - i) * 8;
        b[i] = (unsigned char)(r[bit / 16] >> (bit % 16));
    }
}

/* a >= b ? (both len limbs) */
static int geq(const uint16_t *a, const uint16_t *b, unsigned len)
{
    while (len-- > 0) {
        if (a[len] != b[len])
            return a[len] > b[len];
    }
    return 1;
}

/* a -= b; returns borrow. */
static unsigned sub(uint16_t *a, const uint16_t *b, unsigned len)
{
    int32_t c = 0;
    unsigned i;

    for (i = 0; i < len; i++) {
        c += (int32_t)a[i] - b[i];
        a[i] = (uint16_t)c;
        c >>= 16;
    }
    return (unsigned)(c & 1);
}

/* r = a * b / R mod n, with R = 2^(16*len). a, b < n. r may alias a or b. */
static void montmul(uint16_t *r, const uint16_t *a, const uint16_t *b, const modulus *m)
{
    uint16_t t[2 * MAXL + 2];
    unsigned L = m->len, i;

    memset(t, 0, (2 * L + 2) * 2);
    for (i = 0; i < L; i++) {
        uint16_t *tp = t + i;
        uint32_t x;
        uint16_t q;

        x = (uint32_t)tp[L] + rsa16_addmul_1(tp, a, L, b[i]);
        tp[L] = (uint16_t)x;
        tp[L + 1] += (uint16_t)(x >> 16);

        q = (uint16_t)(tp[0] * m->n0i);          /* makes tp[0] zero below */
        x = (uint32_t)tp[L] + rsa16_addmul_1(tp, m->n, L, q);
        tp[L] = (uint16_t)x;
        tp[L + 1] += (uint16_t)(x >> 16);
    }
    /* Result is t[L..2L], below 2n: subtract n once if needed. */
    if (t[2 * L] || geq(t + L, m->n, L))
        sub(t + L, m->n, L);
    memcpy(r, t + L, L * 2);
}

/*
 * r = a^2 / R mod n. Squaring computes each cross product a[i]*a[j] once
 * and doubles the sum, so it needs about half the multiplies of montmul();
 * then a separate Montgomery reduction (another len^2).
 */
static void montsq(uint16_t *r, const uint16_t *a, const modulus *m)
{
    uint16_t t[2 * MAXL + 2];
    unsigned L = m->len, i;
    uint32_t c;

    memset(t, 0, (2 * L + 2) * 2);

    /* Cross products: t += a[i] * a[i+1..L-1] at position 2i+1. */
    for (i = 0; i + 1 < L; i++)
        t[i + L] = rsa16_addmul_1(t + 2 * i + 1, a + i + 1, L - 1 - i, a[i]);

    /* Double them, then add the squares a[i]^2 at position 2i. */
    for (i = 2 * L; i > 0; i--)
        t[i] = (uint16_t)((t[i] << 1) | (t[i - 1] >> 15));
    t[0] <<= 1;
    c = 0;
    for (i = 0; i < L; i++) {
        uint32_t sq = (uint32_t)a[i] * a[i];
        c += (uint32_t)t[2 * i] + (uint16_t)sq;
        t[2 * i] = (uint16_t)c;
        c >>= 16;
        c += (uint32_t)t[2 * i + 1] + (sq >> 16);
        t[2 * i + 1] = (uint16_t)c;
        c >>= 16;
    }

    /* Montgomery reduction: clear the low L limbs one at a time. */
    for (i = 0; i < L; i++) {
        uint16_t q = (uint16_t)(t[i] * m->n0i);
        unsigned k = i + L;
        c = rsa16_addmul_1(t + i, m->n, L, q);
        while (c && k <= 2 * L) {
            c += t[k];
            t[k++] = (uint16_t)c;
            c >>= 16;
        }
    }
    if (t[2 * L] || geq(t + L, m->n, L))
        sub(t + L, m->n, L);
    memcpy(r, t + L, L * 2);
}

static int setup(modulus *m, const unsigned char *n, size_t nlen)
{
    uint16_t inv;
    int i;

    while (nlen > 0 && *n == 0) {
        n++;
        nlen--;
    }
    if (nlen == 0 || nlen * 8 > RSA16_MAX_BITS || (n[nlen - 1] & 1) == 0)
        return 0;
    m->len = (unsigned)(nlen + 1) / 2;
    decode_be(m->n, m->len, n, nlen);
    m->bits = (unsigned)nlen * 8;
    while (!((m->n[(m->bits - 1) / 16] >> ((m->bits - 1) % 16)) & 1))
        m->bits--;

    /* Newton's method: each step doubles the number of correct bits. */
    inv = m->n[0];
    for (i = 0; i < 4; i++)
        inv = (uint16_t)(inv * (2 - m->n[0] * inv));
    m->n0i = (uint16_t)-inv;
    return 1;
}

/* t[0..len-1] -= a[0..len-1] * w; returns the borrow out (one limb). */
static uint16_t submul_1(uint16_t *t, const uint16_t *a, unsigned len, uint16_t w)
{
    uint32_t borrow = 0;
    unsigned i;

    for (i = 0; i < len; i++) {
        uint32_t p = (uint32_t)a[i] * w + borrow;
        uint16_t lo = (uint16_t)p;
        borrow = p >> 16;
        if (t[i] < lo)
            borrow++;
        t[i] = (uint16_t)(t[i] - lo);
    }
    return (uint16_t)borrow;
}

/*
 * r = R^2 mod n (R = 2^(16*len)), which converts numbers into Montgomery
 * form. Long division, one limb at a time (Knuth's algorithm D): start from
 * R mod n and multiply by 2^16 mod n, len times. About len^2 multiplies,
 * the cost of half a Montgomery multiply.
 */
static void r_squared(uint16_t *r, const modulus *m)
{
    static uint16_t nn[MAXL], u[MAXL + 1];
    unsigned L = m->len, shift = 16 * L - m->bits, i, j;

    /* Normalise: nn = n << shift has its top bit set, which keeps the
       quotient estimates below within 2 of the truth. */
    for (i = L; i-- > 0;)
        nn[i] = (uint16_t)((m->n[i] << shift) | (shift && i ? m->n[i - 1] >> (16 - shift) : 0));

    /* u = R mod nn = R - nn (nn >= R/2 so this is already reduced). */
    memset(u, 0, sizeof(u));
    sub(u, nn, L);

    for (j = 0; j < L; j++) {
        uint32_t top;
        uint16_t q;

        memmove(u + 1, u, L * 2);           /* u *= 2^16 */
        u[0] = 0;
        top = ((uint32_t)u[L] << 16) | u[L - 1];
        q = (u[L] >= nn[L - 1]) ? 0xffff : (uint16_t)(top / nn[L - 1]);
        u[L] -= submul_1(u, nn, L, q);
        while (u[L] != 0) {                 /* overshot: add nn back */
            uint32_t c = 0;
            for (i = 0; i < L; i++) {
                c += (uint32_t)u[i] + nn[i];
                u[i] = (uint16_t)c;
                c >>= 16;
            }
            u[L] += (uint16_t)c;
        }
    }

    /* u = R^2 mod nn. Since n divides nn, reduce once more mod n. */
    memcpy(r, u, L * 2);
    for (i = shift + 1; i-- > 0;) {
        /* subtract n << i while possible */
        static uint16_t ns[MAXL];
        unsigned k;
        for (k = L; k-- > 0;)
            ns[k] = (uint16_t)((m->n[k] << i) | (i && k ? m->n[k - 1] >> (16 - i) : 0));
        if (geq(r, ns, L))
            sub(r, ns, L);
    }
}

uint32_t rsa16_public(unsigned char *x, size_t xlen, const br_rsa_public_key *pk)
{
    static modulus m;
    static uint16_t x_plain[MAXL], a[MAXL], acc[MAXL], r2[MAXL];
    const unsigned char *e = pk->e;
    size_t elen = pk->elen, i;
    int bit, started = 0;
    unsigned char *xb = x;

    if (!setup(&m, pk->n, pk->nlen))
        return 0;
    /* x must be exactly as long as the modulus (without leading zeros) */
    while (xlen > (m.bits + 7) / 8) {
        if (*xb != 0)
            return 0;
        xb++;
        xlen--;
    }
    if (xlen != (m.bits + 7) / 8)
        return 0;
    decode_be(x_plain, m.len, xb, xlen);
    if (geq(x_plain, m.n, m.len))
        return 0;

    r_squared(r2, &m);
    montmul(a, x_plain, r2, &m);            /* a = x*R mod n */

    /*
     * Left-to-right square-and-multiply over the (public) exponent, which
     * is odd for RSA. Everything stays in Montgomery form (times R) until
     * the final multiply, which uses plain x instead: (x^(e-1) R) * x / R
     * = x^e, leaving Montgomery form without an extra multiply.
     */
    while (elen > 0 && *e == 0) {
        e++;
        elen--;
    }
    if (elen == 0 || (e[elen - 1] & 1) == 0)
        return 0;
    for (i = 0; i < elen; i++) {
        for (bit = 7; bit >= 0; bit--) {
            int last = (i == elen - 1 && bit == 0);
            if (started)
                montsq(acc, acc, &m);
            if ((e[i] >> bit) & 1) {
                if (last) {
                    if (started)
                        montmul(acc, acc, x_plain, &m);
                    else
                        memcpy(acc, x_plain, m.len * 2);    /* e == 1 */
                } else if (started) {
                    montmul(acc, acc, a, &m);
                } else {
                    memcpy(acc, a, m.len * 2);
                    started = 1;
                }
            }
        }
    }
    encode_be(xb, xlen, acc);
    return 1;
}

uint32_t rsa16_pkcs1_vrfy(const unsigned char *x, size_t xlen,
                          const unsigned char *hash_oid, size_t hash_len,
                          const br_rsa_public_key *pk, unsigned char *hash_out)
{
    static unsigned char sig[RSA16_MAX_BITS / 8];

    if (xlen > sizeof(sig))
        return 0;
    memcpy(sig, x, xlen);
    if (!rsa16_public(sig, xlen, pk))
        return 0;
    return br_rsa_pkcs1_sig_unpad(sig, xlen, hash_oid, hash_len, hash_out);
}
