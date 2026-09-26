/* Host test: argv[1] = target path; stdin = JSON. Prints the extracted
   string as UTF-8, then "OK" or "ERR". Feeds input in random-sized chunks. */
#include <stdio.h>
#include <stdlib.h>
#include "../src/json.h"

static void put_utf8(void *ctx, int which, uint32_t cp)
{
    (void)ctx; (void)which;
    if (cp < 0x80) putchar((int)cp);
    else if (cp < 0x800) { putchar(0xc0 | (cp >> 6)); putchar(0x80 | (cp & 0x3f)); }
    else if (cp < 0x10000) { putchar(0xe0 | (cp >> 12)); putchar(0x80 | ((cp >> 6) & 0x3f)); putchar(0x80 | (cp & 0x3f)); }
    else { putchar(0xf0 | (cp >> 18)); putchar(0x80 | ((cp >> 12) & 0x3f)); putchar(0x80 | ((cp >> 6) & 0x3f)); putchar(0x80 | (cp & 0x3f)); }
}

int main(int argc, char **argv)
{
    (void)argc;
    static char buf[1 << 20];
    size_t n = fread(buf, 1, sizeof(buf), stdin), off = 0;
    json_parser p;
    int err = 0;
    srand((unsigned)n);
    json_init(&p, put_utf8, NULL);
    json_add_target(&p, argv[1]);
    while (off < n && !err) {
        size_t chunk = 1 + (size_t)rand() % 7;
        if (chunk > n - off) chunk = n - off;
        err = json_feed(&p, buf + off, chunk);
        off += chunk;
    }
    printf("\n%s\n", err ? "ERR" : "OK");
    return 0;
}
