#include <string.h>
#include "charset.h"

/* Unicode for Atari ST characters 0x80-0xBF (mostly the same as code page 437). */
static const uint16_t st_high[64] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,   /* 80 */
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,   /* 88 */
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,   /* 90 */
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x00DF, 0x0192,   /* 98 */
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,   /* A0 */
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,   /* A8 */
    0x00E3, 0x00F5, 0x00D8, 0x00F8, 0x0153, 0x0152, 0x00C0, 0x00C3,   /* B0 */
    0x00D5, 0x00A8, 0x00B4, 0x2020, 0x00B6, 0x00A9, 0x00AE, 0x2122,   /* B8 */
};

/* A few more single characters elsewhere in the ST set. */
static const struct { uint16_t cp; unsigned char st; } st_other[] = {
    { 0x00A7, 0xDD },   /* section sign */
    { 0x00B1, 0xF1 },   /* plus-minus */
    { 0x00B0, 0xF8 },   /* degree */
    { 0x00B5, 0xE6 },   /* micro */
    { 0x00F7, 0xF6 },   /* division */
    { 0x00B2, 0xFD },   /* superscript two */
    { 0x00B3, 0xFE },   /* superscript three */
};

/* Typography the ST lacks, spelled with ASCII. */
static const struct { uint16_t cp; const char *text; } ascii_like[] = {
    { 0x00A0, " " },    { 0x2002, " " },    { 0x2003, " " },    { 0x2009, " " },
    { 0x2010, "-" },    { 0x2011, "-" },    { 0x2012, "-" },    { 0x2013, "-" },
    { 0x2014, "--" },   { 0x2015, "--" },   { 0x2212, "-" },
    { 0x2018, "'" },    { 0x2019, "'" },    { 0x201A, "," },    { 0x2032, "'" },
    { 0x201C, "\"" },   { 0x201D, "\"" },   { 0x201E, "\"" },   { 0x2033, "\"" },
    { 0x2026, "..." },  { 0x2022, "*" },    { 0x00B7, "." },    { 0x00D7, "x" },
    { 0x2190, "<-" },   { 0x2192, "->" },   { 0x21D2, "=>" },   { 0x2264, "<=" },
    { 0x2265, ">=" },   { 0x2260, "!=" },   { 0x2248, "~" },    { 0x2713, "v" },
    { 0x00C1, "A" },    { 0x00CD, "I" },    { 0x00D3, "O" },    { 0x00DA, "U" },
    { 0x00C8, "E" },    { 0x00CA, "E" },    { 0x00CB, "E" },    { 0x00D4, "O" },
    { 0x200B, "" },     { 0xFE0F, "" },     /* zero-width space, emoji variant selector */
};

size_t charset_from_unicode(uint32_t cp, char out[3])
{
    size_t i;

    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    for (i = 0; i < 64; i++) {
        if (st_high[i] == cp) {
            out[0] = (char)(0x80 + i);
            return 1;
        }
    }
    for (i = 0; i < sizeof(st_other) / sizeof(st_other[0]); i++) {
        if (st_other[i].cp == cp) {
            out[0] = (char)st_other[i].st;
            return 1;
        }
    }
    for (i = 0; i < sizeof(ascii_like) / sizeof(ascii_like[0]); i++) {
        if (ascii_like[i].cp == cp) {
            size_t n = strlen(ascii_like[i].text);
            memcpy(out, ascii_like[i].text, n);
            return n;
        }
    }
    out[0] = '?';
    return 1;
}

uint32_t charset_to_unicode(unsigned char c)
{
    size_t i;

    if (c < 0x80)
        return c;
    if (c < 0xC0)
        return st_high[c - 0x80];
    for (i = 0; i < sizeof(st_other) / sizeof(st_other[0]); i++)
        if (st_other[i].st == c)
            return st_other[i].cp;
    return '?';
}
