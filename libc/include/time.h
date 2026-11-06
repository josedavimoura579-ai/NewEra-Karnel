/* nlibc - time.h: o tempo desde o boot. O NovaEra ainda não lê o relógio
 * do PC: time() conta os segundos desde que o sistema ligou. */
#ifndef _TIME_H
#define _TIME_H
#include <stddef.h>
typedef long clock_t;
typedef long time_t;
#define CLOCKS_PER_SEC 100L      /* o timer do núcleo: 100 vezes por segundo */
clock_t clock(void);
time_t  time(time_t *t);
#endif
