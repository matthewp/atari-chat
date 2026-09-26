#ifndef EC_X25519_H
#define EC_X25519_H

#include <bearssl.h>

extern const br_ec_impl ec_x25519_atari;

/* Generate the next handshake's key pair now (~8 s). scalar: 32 random bytes. */
void ec_x25519_precompute(const unsigned char *scalar);

/* Wipe the pair after its one handshake. */
void ec_x25519_forget(void);

#endif
