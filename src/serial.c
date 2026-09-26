#include <osbind.h>
#include <stdlib.h>
#include <string.h>
#include "serial.h"

#define AUX 1            /* BIOS device number for Bconin() etc. */
#define IOREC_RS232 0    /* ...but Iorec() numbers devices differently */
#define BAUD_19200 0
#define FLOW_NONE 0

#define HZ_200 ((volatile unsigned long *)0x4ba)

/*
 * The BIOS receive buffer is only 256 bytes, which overflows while the
 * program is busy (e.g. doing TLS maths). Swap in a bigger one, and put the
 * original back on exit; the BIOS keeps using it after we're gone.
 */
#define RX_BUF_SIZE 16384

static char rx_buf[RX_BUF_SIZE];
static _IOREC saved_iorec;
static _IOREC *aux_iorec;
static int buffer_swapped;

static unsigned long hz200_value;

static long read_hz200(void)
{
    hz200_value = *HZ_200;
    return 0;
}

long millis(void)
{
    Supexec(read_hz200);    /* _hz_200 is only readable in supervisor mode */
    return (long)(hz200_value * 5);
}

static void set_rx_buffer(char *buf, short size, short low, short high)
{
    unsigned short sr;

    /* Mask interrupts so the receive interrupt can't see a half-changed record. */
    __asm__ volatile ("move.w %%sr,%0\n\tor.w #0x0700,%%sr" : "=d"(sr));
    aux_iorec->ibuf = buf;
    aux_iorec->ibufsiz = size;
    aux_iorec->ibufhd = 0;
    aux_iorec->ibuftl = 0;
    aux_iorec->ibuflow = low;
    aux_iorec->ibufhi = high;
    __asm__ volatile ("move.w %0,%%sr" : : "d"(sr));
}

static long install_buffer(void)
{
    set_rx_buffer(rx_buf, RX_BUF_SIZE, RX_BUF_SIZE / 4, RX_BUF_SIZE * 3 / 4);
    return 0;
}

static long restore_buffer(void)
{
    set_rx_buffer(saved_iorec.ibuf, saved_iorec.ibufsiz,
                  saved_iorec.ibuflow, saved_iorec.ibufhi);
    return 0;
}

void serial_shutdown(void)
{
    if (buffer_swapped) {
        Supexec(restore_buffer);
        buffer_swapped = 0;
    }
}

void serial_init(void)
{
    Rsconf(BAUD_19200, FLOW_NONE, -1, -1, -1, -1);

    aux_iorec = (_IOREC *)Iorec(IOREC_RS232);
    saved_iorec = *aux_iorec;
    Supexec(install_buffer);
    buffer_swapped = 1;
    atexit(serial_shutdown);
}

void serial_putc(char c)
{
    Bconout(AUX, (unsigned char)c);
}

void serial_write(const char *buf, long len)
{
    while (len-- > 0)
        serial_putc(*buf++);
}

void serial_puts(const char *s)
{
    serial_write(s, strlen(s));
}

int serial_avail(void)
{
    return Bconstat(AUX) != 0;
}

int serial_getc(void)
{
    return (int)(Bconin(AUX) & 0xff);
}

int serial_getc_timeout(long ms)
{
    long deadline = millis() + ms;

    while (!serial_avail()) {
        if (millis() >= deadline)
            return -1;
    }
    return serial_getc();
}

void serial_flush_input(void)
{
    while (serial_avail())
        serial_getc();
}
