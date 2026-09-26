/* Host-side check of rsa16_public: reads "n e x" hex lines, prints x^e mod n. */
#include <stdio.h>
#include <string.h>
#include "../src/rsa/rsa16.h"

uint16_t rsa16_addmul_1(uint16_t *t, const uint16_t *a, unsigned len, uint16_t w)
{
    return rsa16_addmul_1_ref(t, a, len, w);
}

static size_t unhex(unsigned char *out, const char *h)
{
    size_t n = strlen(h) / 2;
    for (size_t i = 0; i < n; i++)
        sscanf(h + 2 * i, "%2hhx", &out[i]);
    return n;
}

int main(void)
{
    static char hn[1100], he[20], hx[1100];
    static unsigned char n[520], e[8], x[520];
    while (scanf("%1099s %19s %1099s", hn, he, hx) == 3) {
        br_rsa_public_key pk;
        size_t xl;
        pk.n = n; pk.nlen = unhex(n, hn);
        pk.e = e; pk.elen = unhex(e, he);
        xl = unhex(x, hx);
        if (!rsa16_public(x, xl, &pk)) { puts("ERR"); continue; }
        for (size_t i = 0; i < xl; i++) printf("%02x", x[i]);
        puts("");
    }
    return 0;
}
