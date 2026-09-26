/*
 * First real TLS connection: handshake with Cloudflare AI Gateway, fetch
 * "/", then run the deferred authenticity checks.
 *
 * The request carries nothing secret, so for this test it goes out before
 * the (slow) checks; that tells us whether the server waits for us. Real
 * API calls must run tls_verify() first.
 */
#include <stdio.h>
#include <string.h>
#include <osbind.h>
#include "serial.h"
#include "modem.h"
#include "tls/tls.h"

#define HOST "gateway.ai.cloudflare.com"

long _stksize = 65536;      /* BearSSL's RSA code needs more than the default stack */

static void out(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            Cconout('\r');
        Cconout(*s);
    }
}

static void log_line(const char *s)
{
    out("  ");
    out(s);
    out("\n");
}

static void printf_out(const char *fmt, long a, long b)
{
    char line[120];
    sprintf(line, fmt, a, b);
    out(line);
}

static int finish(void)
{
    out("\nPress any key to exit...");
    Cconin();
    return 0;
}

int main(void)
{
    static char response[2048];
    tls_stats st;
    long t;
    int n, total = 0, err;
    const char *request =
        "GET / HTTP/1.1\r\n"
        "Host: " HOST "\r\n"
        "User-Agent: atari-chat/0.1 (Atari ST)\r\n"
        "Connection: close\r\n"
        "\r\n";

    out("atari-chat TLS test: " HOST "\n\n");
    serial_init();

    {
        unsigned d = Tgetdate() & 0xffff, tm = Tgettime() & 0xffff;
        char line[60];
        sprintf(line, "System date: %04u-%02u-%02u %02u:%02u\n\n",
                1980 + (d >> 9), (d >> 5) & 15, d & 31, tm >> 11, (tm >> 5) & 63);
        out(line);
    }

    out("Generating key pair (before dialing)...\n");
    t = millis();
    tls_prepare();
    printf_out("  done in %ld ms\n", millis() - t, 0);

    out("Dialing...\n");
    if (!modem_reset(log_line) || !modem_dial(HOST ":443", log_line)) {
        out("Could not connect.\n");
        return finish();
    }

    out("TLS handshake (server allows ~15 s)...\n");
    if (tls_handshake(HOST, &st) != 0) {
        printf_out("  FAILED, BearSSL error %ld\n", tls_last_error(), 0);
        printf_out("  (got %ld bytes, sent %ld)\n", st.bytes_in, st.bytes_out);
        return finish();
    }
    printf_out("  server's first byte:   %6ld ms\n", st.first_byte_ms, 0);
    printf_out("  server's flight done:  %6ld ms\n", st.server_done_ms, 0);
    printf_out("  our key exchange math: %6ld ms\n", st.compute_ms, 0);
    printf_out("  handshake complete:    %6ld ms  (%ld bytes in)\n", st.total_ms, st.bytes_in);

    out("\nSending GET / (nothing secret)...\n");
    if (tls_write(request, strlen(request)) < 0) {
        printf_out("  write failed, error %ld\n", tls_last_error(), 0);
        return finish();
    }
    while (total < (int)sizeof(response) - 1
           && (n = tls_read(response + total, sizeof(response) - 1 - total)) > 0)
        total += n;
    response[total] = '\0';
    {
        /* Just the status line and a few headers. */
        char *p = response;
        int lines = 0;
        while (*p && lines < 6) {
            char *eol = strstr(p, "\r\n");
            if (!eol)
                break;
            *eol = '\0';
            log_line(p);
            p = eol + 2;
            lines++;
        }
    }
    printf_out("  (%ld bytes of response)\n", total, 0);
    tls_close();

    out("\nVerifying the server (deferred checks)...\n");
    t = millis();
    err = tls_verify(log_line);
    {
        char line[100];
        sprintf(line, "  %s after %ld ms", err ? "FAILED" : "server is authentic", millis() - t);
        out(line);
        if (err == BR_ERR_X509_TIME_UNKNOWN) {
            out(": the date isn't set. Set it in the Control Panel.");
        } else if (err) {
            sprintf(line, ", BearSSL error %d", err);
            out(line);
        }
        out("\n");
    }

    return finish();
}
