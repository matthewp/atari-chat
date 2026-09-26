#ifndef FE16_H
#define FE16_H

/*
 * Arithmetic modulo p = 2^255 - 19, for X25519 on the 68000.
 *
 * A field element is 16 limbs of 16 bits, least significant limb first,
 * holding any value below 2^256 (not necessarily fully reduced). 16-bit limbs
 * match the 68000's MULU.W, a 16x16->32 multiply.
 */
#include <stddef.h>
#include <stdint.h>

typedef uint16_t fe16[16];

/* In assembly on the Atari (fe16_mul.S), portable C elsewhere (fe16_ref.c). */
void fe16_mul(fe16 r, const fe16 a, const fe16 b);
void fe16_sq(fe16 r, const fe16 a);

/* Portable versions, always available, for testing the assembly. */
void fe16_mul_ref(fe16 r, const fe16 a, const fe16 b);
void fe16_sq_ref(fe16 r, const fe16 a);

void fe16_add(fe16 r, const fe16 a, const fe16 b);
void fe16_sub(fe16 r, const fe16 a, const fe16 b);
void fe16_mul_a24(fe16 r, const fe16 a);     /* r = a * 121665 */
void fe16_cswap(fe16 a, fe16 b, uint16_t swap);
void fe16_invert(fe16 r, const fe16 a);
void fe16_from_bytes(fe16 r, const unsigned char *le32);
void fe16_to_bytes(unsigned char *le32, const fe16 a);  /* fully reduces */

/*
 * X25519 scalar multiplication, with BearSSL's conventions: point is a
 * 32-byte little-endian u-coordinate (overwritten with the result), scalar
 * is big-endian, clamped as RFC 7748 requires.
 */
void x25519_ladder(unsigned char *point, const unsigned char *scalar_be, size_t len);

#endif
