#ifndef ENTROPY_H
#define ENTROPY_H

/*
 * Gather 32 bytes of seed material. The ST has no hardware RNG, so this
 * hashes timer jitter, clocks and counters. Good enough to start; it should
 * later mix in keyboard/mouse timings from the user.
 */
void entropy_gather(unsigned char out[32]);

#endif
