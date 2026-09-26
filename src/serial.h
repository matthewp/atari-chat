#ifndef SERIAL_H
#define SERIAL_H

/* The ST's serial port (BIOS device 1, "AUX:"). */

void serial_init(void);                     /* restores itself at exit() */
void serial_shutdown(void);
void serial_putc(char c);
void serial_write(const char *buf, long len);
void serial_puts(const char *s);
int  serial_avail(void);
int  serial_getc(void);                      /* blocks */
int  serial_getc_timeout(long ms);           /* -1 on timeout */
void serial_flush_input(void);

/* Milliseconds since boot, from the 200 Hz system timer. */
long millis(void);

#endif
