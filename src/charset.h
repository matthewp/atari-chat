#ifndef CHARSET_H
#define CHARSET_H

#include <stddef.h>
#include <stdint.h>

/*
 * Unicode code point -> Atari ST character set. Writes 1-3 bytes to out
 * (smart quotes, dashes, ellipses... become ASCII look-alikes) and returns
 * how many. Characters the ST can't show become '?'.
 */
size_t charset_from_unicode(uint32_t cp, char out[3]);

/* Atari ST character -> Unicode code point (for sending what the user typed). */
uint32_t charset_to_unicode(unsigned char c);

#endif
