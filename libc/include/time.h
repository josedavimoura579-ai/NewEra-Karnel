/* nlibc - time.h: o tempo desde o boot. O NovaEra ainda não lê o relógio
 * do PC: time() conta os segundos desde que o sistema ligou. */
#ifndef _TIME_H
#define _TIME_H
#include <stddef.h>
typedef long clock_t;
typedef long time_t;
#define CLOCKS_PER_SEC 100L      /* o timer do núcleo: 100 vezes por segundo */
clock_t clock(void);
time_t  time(time_t *t);             /* segundos desde 1/1/1970 (UTC), pelo relógio do computador */

struct tm {
    int tm_sec, tm_min, tm_hour;     /* 0-60, 0-59, 0-23 */
    int tm_mday, tm_mon, tm_year;    /* 1-31, 0-11, anos desde 1900 */
    int tm_wday, tm_yday, tm_isdst;  /* 0-6 (domingo = 0), 0-365, horário de verão (sempre 0) */
};
struct tm *gmtime(const time_t *t);       /* em UTC */
struct tm *localtime(const time_t *t);    /* com o fuso do /etc/fuso (ex.: -3) */
time_t     mktime(struct tm *tm);         /* o contrário do localtime */
double     difftime(time_t a, time_t b);
#endif
