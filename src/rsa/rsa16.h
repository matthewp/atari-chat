#ifndef RSA16_H
#define RSA16_H

/*
 * RSA public-key operations (signature verification) tuned for the 68000.
 * Montgomery multiplication on 16-bit limbs, with the inner loop in
 * assembly (addmul.S). Only public data is involved, so nothing here needs
 * to be constant-time.
 *
 * rsa16_public() and rsa16_pkcs1_vrfy() follow BearSSL's br_rsa_public and
 * br_rsa_pkcs1_vrfy interfaces, so they can be plugged into BearSSL.
 */
#include <stddef.h>
#include <stdint.h>
#include <bearssl.h>

#define RSA16_MAX_BITS 4096

uint32_t rsa16_public(unsigned char *x, size_t xlen, const br_rsa_public_key *pk);

uint32_t rsa16_pkcs1_vrfy(const unsigned char *x, size_t xlen,
                          const unsigned char *hash_oid, size_t hash_len,
                          const br_rsa_public_key *pk, unsigned char *hash_out);

/*
 * t[0..len-1] += a[0..len-1] * w; returns the carry out (one limb).
 * Limbs are least significant first. Assembly on the Atari, C elsewhere.
 */
uint16_t rsa16_addmul_1(uint16_t *t, const uint16_t *a, unsigned len, uint16_t w);
uint16_t rsa16_addmul_1_ref(uint16_t *t, const uint16_t *a, unsigned len, uint16_t w);

#endif
