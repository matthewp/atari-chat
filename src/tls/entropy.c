#include <osbind.h>
#include <bearssl.h>
#include "entropy.h"

/* Things only readable in supervisor mode. */
static struct {
    unsigned long hz200;        /* 200 Hz tick counter */
    unsigned long frclock;      /* vertical blank counter */
    unsigned char timer_c;      /* MFP timer C data (200 Hz timer, counting down) */
    unsigned char timer_d;      /* MFP timer D data (serial baud clock) */
    unsigned char timer_b;      /* MFP timer B data */
} sample;

static long take_sample(void)
{
    sample.hz200 = *(volatile unsigned long *)0x4ba;
    sample.frclock = *(volatile unsigned long *)0x466;
    sample.timer_c = *(volatile unsigned char *)0xfffa23;
    sample.timer_d = *(volatile unsigned char *)0xfffa25;
    sample.timer_b = *(volatile unsigned char *)0xfffa21;
    return 0;
}

void entropy_gather(unsigned char out[32])
{
    br_sha256_context h;
    long misc[3];
    volatile unsigned spin;
    int i;

    br_sha256_init(&h);

    misc[0] = Tgettime();
    misc[1] = Tgetdate();
    misc[2] = Random();         /* XBIOS pseudo-random, seeded from a timer */
    br_sha256_update(&h, misc, sizeof(misc));

    /* Timer registers sampled after data-dependent delays pick up jitter
       from interrupts and bus contention. */
    for (i = 0; i < 256; i++) {
        Supexec(take_sample);
        br_sha256_update(&h, &sample, sizeof(sample));
        for (spin = (sample.timer_c ^ sample.timer_d ^ i) & 0x3f; spin > 0; spin--)
            ;
    }

    br_sha256_out(&h, out);
}
