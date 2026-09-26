#include <string.h>
#include "modem.h"
#include "serial.h"

/* Read result lines until one starts with ok (1) or is a failure (0). */
static int wait_result(const char *ok, long timeout_ms, modem_log_fn log)
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
                if (log)
                    log(line);
                if (strncmp(line, ok, strlen(ok)) == 0) {
                    /* Swallow the "\n" of "\r\n" so it isn't mistaken for
                       data after CONNECT. (TLS servers never speak first.) */
                    if (c == '\r')
                        serial_getc_timeout(50);
                    return 1;
                }
                if (strcmp(line, "ERROR") == 0 || strcmp(line, "NO CARRIER") == 0
                    || strcmp(line, "BUSY") == 0 || strcmp(line, "NO ANSWER") == 0)
                    return 0;
            }
            len = 0;
        } else if (len < (int)sizeof(line) - 1) {
            line[len++] = (char)c;
        }
    }
    if (log)
        log("(modem timeout)");
    return 0;
}

static int command(const char *cmd, const char *ok, long timeout_ms, modem_log_fn log)
{
    serial_puts(cmd);
    serial_putc('\r');
    return wait_result(ok, timeout_ms, log);
}

int modem_reset(modem_log_fn log)
{
    serial_flush_input();
    return command("ATZ", "OK", 3000, log) && command("ATE0", "OK", 3000, log);
}

int modem_dial(const char *host_port, modem_log_fn log)
{
    char cmd[100];

    strcpy(cmd, "ATDT");
    strncat(cmd, host_port, sizeof(cmd) - 5);
    return command(cmd, "CONNECT", 30000, log);
}

static void pause_ms(long ms)
{
    long until = millis() + ms;
    while (millis() < until)
        ;
}

void modem_hangup(void)
{
    pause_ms(1100);
    serial_puts("+++");
    pause_ms(1100);
    serial_flush_input();
    /* The CR clears anything left on the command line if the modem had
       already dropped to command mode by itself. */
    serial_puts("\rATH\r");
    wait_result("OK", 2000, NULL);
    serial_flush_input();
}
