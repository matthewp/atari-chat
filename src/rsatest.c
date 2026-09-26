/* RSA on the Atari: check the assembly inner loop, time 2048/4096-bit ops. */
#include <stdio.h>
#include <string.h>
#include <osbind.h>
#include "serial.h"
#include "rsa/rsa16.h"

long _stksize = 65536;

static void out(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            Cconout('\r');
        Cconout(*s);
    }
}

static unsigned long rng = 777;

static uint16_t rand16(void)
{
    rng = rng * 1103515245u + 12345u;
    return (uint16_t)(rng >> 12);
}

static int check_addmul(void)
{
    static uint16_t a[256], t1[256], t2[256];
    int n, i, bad = 0;

    for (n = 0; n < 200; n++) {
        unsigned len = 1 + n % 256;
        uint16_t w = (n % 9 == 0) ? 0xffff : rand16();
        for (i = 0; i < (int)len; i++) {
            a[i] = (n % 7 == 0) ? 0xffff : rand16();
            t1[i] = t2[i] = (n % 5 == 0) ? 0xffff : rand16();
        }
        if (rsa16_addmul_1(t1, a, len, w) != rsa16_addmul_1_ref(t2, a, len, w)
            || memcmp(t1, t2, len * 2) != 0)
            bad++;
    }
    return bad;
}

static void time_public(int bits)
{
    static unsigned char n[512], x[512];
    static const unsigned char e[] = { 0x01, 0x00, 0x01 };
    br_rsa_public_key pk;
    char line[80];
    int i, len = bits / 8;
    long t;

    for (i = 0; i < len; i++) {
        n[i] = (unsigned char)rand16();
        x[i] = (unsigned char)rand16();
    }
    n[0] |= 0x80;
    n[len - 1] |= 1;
    x[0] &= 0x7f;
    pk.n = n;
    pk.nlen = len;
    pk.e = (unsigned char *)e;
    pk.elen = sizeof(e);

    t = millis();
    i = rsa16_public(x, len, &pk);
    sprintf(line, "RSA-%d verify (e=65537): %ld ms%s\n", bits, millis() - t, i ? "" : " FAILED");
    out(line);
}

int main(void)
{
    char line[80];
    int bad = check_addmul();

    out("RSA on the 68000\n\n");
    sprintf(line, "addmul_1 assembly vs C, 200 cases: %s (%d mismatches)\n\n",
            bad ? "FAIL" : "ok", bad);
    out(line);
    time_public(2048);
    time_public(4096);
    out("\nDone. Press any key...");
    Cconin();
    return 0;
}
