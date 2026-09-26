/* Host-side check of the X25519 ladder using the portable field multiply. */
#include <stdio.h>
#include <string.h>
#include "../src/x25519/fe16.h"

void fe16_mul(fe16 r, const fe16 a, const fe16 b) { fe16_mul_ref(r, a, b); }
void fe16_sq(fe16 r, const fe16 a) { fe16_sq_ref(r, a); }

static void unhex(unsigned char *out, const char *hex)
{
    for (int i = 0; i < 32; i++)
        sscanf(hex + 2 * i, "%2hhx", &out[i]);
}

/* RFC 7748 scalars are little-endian; BearSSL's interface wants big-endian. */
static int check(const char *scalar, const char *u, const char *expect)
{
    unsigned char k[32], kb[32], p[32], want[32];
    unhex(k, scalar);
    unhex(p, u);
    unhex(want, expect);
    for (int i = 0; i < 32; i++)
        kb[i] = k[31 - i];
    x25519_ladder(p, kb, 32);
    int ok = memcmp(p, want, 32) == 0;
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok;
}

int main(void)
{
    int ok = 1;
    ok &= check("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4",
                "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c",
                "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552");
    ok &= check("4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d",
                "e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493",
                "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957");
    return ok ? 0 : 1;
}
