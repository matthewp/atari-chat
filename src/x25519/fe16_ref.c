/* Portable field multiply/square: the reference the assembly is checked against. */
#include "fe16.h"

void fe16_mul_ref(fe16 r, const fe16 a, const fe16 b)
{
    uint32_t t[32] = { 0 };
    uint64_t acc = 0;
    uint32_t c;
    int i, j, k;

    /* Product scanning: column k collects every a[i]*b[k-i]. */
    for (k = 0; k < 31; k++) {
        for (i = 0; i < 16; i++) {
            j = k - i;
            if (j >= 0 && j < 16)
                acc += (uint32_t)a[i] * b[j];
        }
        t[k] = (uint16_t)acc;
        acc >>= 16;
    }
    t[31] = (uint16_t)acc;

    /* 2^256 = 38 (mod p): fold the high half into the low half. */
    c = 0;
    for (i = 0; i < 16; i++) {
        c += t[i] + t[i + 16] * 38;
        r[i] = (uint16_t)c;
        c >>= 16;
    }
    c *= 38;
    for (i = 0; i < 16; i++) {
        c += r[i];
        r[i] = (uint16_t)c;
        c >>= 16;
    }
    r[0] += (uint16_t)(c * 38);     /* cannot carry: r is tiny if c != 0 */
}

void fe16_sq_ref(fe16 r, const fe16 a)
{
    fe16_mul_ref(r, a, a);
}
