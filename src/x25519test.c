/*
 * X25519 on the Atari: check the assembly field multiply against the C
 * reference, run the RFC 7748 test vectors, and time it all.
 */
#include <stdio.h>
#include <string.h>
#include <osbind.h>
#include "serial.h"
#include "x25519/fe16.h"

static void out(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            Cconout('\r');
        Cconout(*s);
    }
}

static unsigned long rng = 12345;

static uint16_t rand16(void)
{
    rng = rng * 1103515245u + 12345u;
    return (uint16_t)(rng >> 12);
}

/* Random limbs, with some inputs forced to all-ones / zero to hit carries. */
static void random_fe(fe16 x, int n)
{
    int i;
    for (i = 0; i < 16; i++) {
        if (n % 5 == 0)
            x[i] = 0xffff;
        else if (n % 7 == 0)
            x[i] = (i == 0) ? rand16() : 0;
        else
            x[i] = rand16();
    }
}

static int check_field(void)
{
    fe16 a, b, r1, r2;
    int n, bad = 0;

    for (n = 0; n < 300; n++) {
        random_fe(a, n);
        random_fe(b, n / 3);
        fe16_mul(r1, a, b);
        fe16_mul_ref(r2, a, b);
        if (memcmp(r1, r2, sizeof(fe16)) != 0)
            bad++;
        fe16_sq(r1, a);
        fe16_sq_ref(r2, a);
        if (memcmp(r1, r2, sizeof(fe16)) != 0)
            bad++;
        memcpy(r1, a, sizeof(fe16));
        fe16_mul(r1, r1, b);            /* output aliasing an input */
        fe16_mul_ref(r2, a, b);
        if (memcmp(r1, r2, sizeof(fe16)) != 0)
            bad++;
    }
    return bad;
}

static void unhex(unsigned char *o, const char *h)
{
    int i;
    for (i = 0; i < 32; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        o[i] = (unsigned char)v;
    }
}

static int rfc_vector(const char *scalar, const char *u, const char *expect, long *ms)
{
    unsigned char k[32], kb[32], p[32], want[32];
    long t;
    int i;

    unhex(k, scalar);
    unhex(p, u);
    unhex(want, expect);
    for (i = 0; i < 32; i++)
        kb[i] = k[31 - i];
    t = millis();
    x25519_ladder(p, kb, 32);
    *ms = millis() - t;
    return memcmp(p, want, 32) == 0;
}

int main(void)
{
    char line[100];
    fe16 a, b, r;
    long t, ms;
    int i, bad, ok;

    out("X25519 on the 68000\n\n");

    bad = check_field();
    sprintf(line, "Assembly vs C reference, 900 cases: %s (%d mismatches)\n",
            bad ? "FAIL" : "ok", bad);
    out(line);

    random_fe(a, 1);
    random_fe(b, 2);
    t = millis();
    for (i = 0; i < 1000; i++)
        fe16_mul(r, a, b);
    ms = millis() - t;
    sprintf(line, "fe16_mul: %ld us each (~%ld cycles)\n", ms, ms * 8);
    out(line);
    t = millis();
    for (i = 0; i < 1000; i++)
        fe16_sq(r, a);
    ms = millis() - t;
    sprintf(line, "fe16_sq:  %ld us each (~%ld cycles)\n", ms, ms * 8);
    out(line);
    t = millis();
    for (i = 0; i < 1000; i++)
        fe16_add(r, a, b);
    ms = millis() - t;
    sprintf(line, "fe16_add: %ld us each\n", ms);
    out(line);
    t = millis();
    for (i = 0; i < 1000; i++)
        fe16_sub(r, a, b);
    ms = millis() - t;
    sprintf(line, "fe16_sub: %ld us each\n\n", ms);
    out(line);

    out("RFC 7748 vector 1... ");
    ok = rfc_vector("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4",
                    "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c",
                    "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552", &ms);
    sprintf(line, "%s in %ld ms\n", ok ? "PASS" : "FAIL", ms);
    out(line);

    out("RFC 7748 vector 2... ");
    ok = rfc_vector("4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d",
                    "e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493",
                    "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957", &ms);
    sprintf(line, "%s in %ld ms\n", ok ? "PASS" : "FAIL", ms);
    out(line);

    out("\nDone. Press any key...");
    Cconin();
    return 0;
}
