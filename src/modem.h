#ifndef MODEM_H
#define MODEM_H

/* Hayes-style modem on the serial port (a WiFi modem, or tools/modem.py). */

typedef void (*modem_log_fn)(const char *line);

/* Reset the modem and turn off command echo. Returns 1 if it answered. */
int modem_reset(modem_log_fn log);

/* Dial "host:port". Returns 1 once connected. */
int modem_dial(const char *host_port, modem_log_fn log);

/* Drop the call: "+++" (with a second of silence either side), then ATH. */
void modem_hangup(void);

#endif
