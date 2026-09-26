/*
 * Network smoke test: dial out through the modem and fetch a web page.
 * Plain HTTP on purpose. This only proves the byte pipe works; TLS comes next.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <osbind.h>
#include "serial.h"

#define HOST "example.com"
#define PORT "80"

static void con_putc(char c)
{
    if (c == '\n')
        Cconout('\r');
    Cconout(c);
}

static void con_puts(const char *s)
{
    while (*s)
        con_putc(*s++);
}

/* Read modem result lines until one matches ok or fail. Returns 1 if ok. */
static int wait_result(const char *ok, const char *fail, long timeout_ms)
{
    char line[80];
    int len = 0, c;
    long deadline = millis() + timeout_ms;

    while (millis() < deadline) {
        c = serial_getc_timeout(100);
        if (c < 0)
            continue;
        if (c == '\r' || c == '\n') {
            line[len] = '\0';
            if (len > 0) {
                con_puts("  modem: ");
                con_puts(line);
                con_puts("\n");
                if (strncmp(line, ok, strlen(ok)) == 0)
                    return 1;
                if (strncmp(line, fail, strlen(fail)) == 0 ||
                    strcmp(line, "ERROR") == 0 || strcmp(line, "NO CARRIER") == 0)
                    return 0;
            }
            len = 0;
        } else if (len < (int)sizeof(line) - 1) {
            line[len++] = (char)c;
        }
    }
    con_puts("  (timeout)\n");
    return 0;
}

static int modem_cmd(const char *cmd, const char *ok, long timeout_ms)
{
    con_puts("> ");
    con_puts(cmd);
    con_puts("\n");
    serial_puts(cmd);
    serial_putc('\r');
    return wait_result(ok, "NO CARRIER", timeout_ms);
}

static void finish(int code)
{
    con_puts("\nPress any key to exit...");
    Cconin();
    exit(code);
}

int main(void)
{
    const char *request =
        "GET / HTTP/1.0\r\n"
        "Host: " HOST "\r\n"
        "User-Agent: atari-chat/0.1 (Atari ST)\r\n"
        "\r\n";
    const char *tail = "\r\nNO CARRIER\r\n";
    size_t tail_len = strlen(tail);
    static char response[8192];
    long total = 0, start, elapsed;
    int c;

    con_puts("atari-chat network test\n\n");
    serial_init();

    if (!modem_cmd("ATZ", "OK", 3000) || !modem_cmd("ATE0", "OK", 3000)) {
        con_puts("No modem answered.\n");
        finish(1);
    }
    if (!modem_cmd("ATDT" HOST ":" PORT, "CONNECT", 20000)) {
        con_puts("Could not connect.\n");
        finish(1);
    }

    con_puts("\n--- sending request ---\n");
    serial_puts(request);
    con_puts("--- response ---\n");

    /* Store first, print after: printing is slow and we want clean timing. */
    start = millis();
    for (;;) {
        c = serial_getc_timeout(5000);
        if (c < 0)
            break;
        if (total < (long)sizeof(response))
            response[total] = (char)c;
        total++;
        if (total >= (long)tail_len &&
            memcmp(response + total - tail_len, tail, tail_len) == 0)
            break;
    }
    elapsed = millis() - start;

    {
        long i, col = 0;
        for (i = 0; i < total && i < (long)sizeof(response); i++) {
            char ch = response[i];
            if (ch == '\r')
                continue;
            if (ch == '\n' || col == 79) {
                con_putc('\n');
                col = 0;
                if (ch == '\n')
                    continue;
            }
            con_putc(ch);
            col++;
        }
    }
    {
        char buf[80];
        sprintf(buf, "\n--- %ld bytes in %ld ms ---\n", total, elapsed);
        con_puts(buf);
    }
    finish(0);
    return 0;
}
