/*
 * nlibc - libc/src/math.c: a matemática de math.h, em C puro.
 *
 * O truque para muitas delas é mexer nos bits do double: 1 bit de sinal, 11
 * de expoente (com 1023 somado) e 52 da mantissa. Assim se separa x em
 * m × 2^e, e cada função só precisa de uma série curta para m.
 */
#include <math.h>

typedef union { double d; unsigned long long u; } bits_t;

static double nan_(void) { volatile double z = 0.0; return z / z; }

double fabs(double x) { bits_t b; b.d = x; b.u &= 0x7FFFFFFFFFFFFFFFull; return b.d; }

/* |x| >= 2^52: já é inteiro (ou infinito/NaN) */
#define GRANDE 4503599627370496.0

double trunc(double x)
{
    if (isnan(x) || fabs(x) >= GRANDE) return x;
    double t = (double)(long long)x;
    return t == 0 && x < 0 ? -0.0 : t;
}

double floor(double x)
{
    double t = trunc(x);
    return t > x ? t - 1 : t;
}

double ceil(double x)
{
    double t = trunc(x);
    return t < x ? t + 1 : t;
}

double round(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }

double fmod(double x, double y)
{
    if (y == 0 || isnan(x) || isnan(y) || isinf(x)) return nan_();
    if (isinf(y)) return x;
    double r = x - trunc(x / y) * y;
    if (x >= 0 ? r < 0 : r > 0) r += x >= 0 ? fabs(y) : -fabs(y);    /* arredondamento de x/y */
    return r;
}

double frexp(double x, int *e)
{
    bits_t b; b.d = x;
    int ex = (int)(b.u >> 52 & 0x7FF);
    if (x == 0 || ex == 0x7FF) { *e = 0; return x; }
    if (ex == 0) {                                   /* subnormal: multiplica por 2^54 antes */
        double m = frexp(x * 18014398509481984.0, e);
        *e -= 54;
        return m;
    }
    *e = ex - 1022;
    b.u = (b.u & 0x800FFFFFFFFFFFFFull) | 0x3FE0000000000000ull;    /* 0.5 <= |m| < 1 */
    return b.d;
}

static double pot2(int e)                    /* 2^e, para -1022 <= e <= 1023 */
{
    bits_t b; b.u = (unsigned long long)(e + 1023) << 52;
    return b.d;
}

double ldexp(double x, int e)
{
    while (e > 1023) { x *= pot2(1023); e -= 1023; if (isinf(x)) return x; }
    while (e < -1022) { x *= pot2(-1022); e += 1022; if (x == 0) return x; }
    return x * pot2(e);
}

double sqrt(double x)
{
    if (isnan(x) || x < 0) return x == 0 ? x : nan_();
    if (x == 0 || isinf(x)) return x;
    int e;
    double m = frexp(x, &e);                 /* x = m * 2^e, 0.5 <= m < 1 */
    if (e & 1) { m *= 2; e--; }              /* expoente par: sqrt(2^e) = 2^(e/2) */
    double y = 0.41731 + 0.59016 * m;        /* um bom começo para sqrt(m) */
    for (int i = 0; i < 5; i++) y = 0.5 * (y + m / y);   /* Newton: dobra os dígitos certos */
    return ldexp(y, e / 2);
}

double exp(double x)
{
    if (isnan(x)) return x;
    if (x > 709.78) return HUGE_VAL;
    if (x < -745.2) return 0;
    double k = round(x / M_LN2);             /* x = k*ln2 + r, |r| <= ln2/2 */
    double r = x - k * 0.693147180369123816490 - k * 1.90821492927058770002e-10;
    double s = 1, t = 1;
    for (int i = 1; i < 22; i++) { t *= r / i; s += t; }
    return ldexp(s, (int)k);
}

double log(double x)
{
    if (isnan(x) || x < 0) return nan_();
    if (x == 0) return -HUGE_VAL;
    if (isinf(x)) return x;
    int e;
    double m = frexp(x, &e);
    if (m < 0.70710678118654752440) { m *= 2; e--; }     /* sqrt(1/2) <= m < sqrt(2) */
    double s = (m - 1) / (m + 1), s2 = s * s, t = s, soma = 0;
    for (int i = 1; i < 40; i += 2) { soma += t / i; t *= s2; }   /* log m = 2 atanh s */
    return 2 * soma + e * M_LN2;
}

double log2(double x) { return log(x) / M_LN2; }
double log10(double x) { return log(x) / 2.30258509299404568402; }

double pow(double x, double y)
{
    if (y == 0) return 1;
    if (isnan(x) || isnan(y)) return nan_();
    if (y == trunc(y) && fabs(y) < 1e9) {    /* expoente inteiro: multiplica (vale para x < 0) */
        long long n = (long long)(y < 0 ? -y : y);
        double r = 1, b = x;
        while (n) { if (n & 1) r *= b; b *= b; n >>= 1; }
        return y < 0 ? 1 / r : r;
    }
    if (x < 0) return nan_();
    if (x == 0) return y > 0 ? 0 : HUGE_VAL;
    return exp(y * log(x));
}

/* sin e cos de |r| <= pi/4 */
static double sen_(double r)
{
    double r2 = r * r, t = r, s = r;
    for (int i = 2; i < 24; i += 2) { t *= -r2 / (i * (i + 1)); s += t; }
    return s;
}
static double cos_(double r)
{
    double r2 = r * r, t = 1, s = 1;
    for (int i = 1; i < 24; i += 2) { t *= -r2 / (i * (i + 1)); s += t; }
    return s;
}

/* x = q*(pi/2) + r; devolve q mod 4 */
static int reduzir(double x, double *r)
{
    double q = round(x / M_PI_2);
    *r = x - q * 1.57079632673412561417 - q * 6.07710050650619224932e-11;
    long long qi = (long long)fmod(q, 4.0);
    return (int)((qi + 4) % 4);
}

double sin(double x)
{
    if (isnan(x) || isinf(x)) return nan_();
    double r;
    switch (reduzir(x, &r)) {
    case 0: return sen_(r);
    case 1: return cos_(r);
    case 2: return -sen_(r);
    default: return -cos_(r);
    }
}

double cos(double x)
{
    if (isnan(x) || isinf(x)) return nan_();
    double r;
    switch (reduzir(x, &r)) {
    case 0: return cos_(r);
    case 1: return -sen_(r);
    case 2: return -cos_(r);
    default: return sen_(r);
    }
}

double tan(double x) { return sin(x) / cos(x); }

double atan(double x)
{
    if (isnan(x)) return x;
    int neg = x < 0;
    if (neg) x = -x;
    int inv = x > 1;
    if (inv) x = 1 / x;                              /* atan x = pi/2 - atan(1/x) */
    int meio = x > 0.41421356237309504880;           /* tan(pi/8) */
    if (meio) x = (x - 1) / (x + 1);                 /* atan x = pi/4 + atan((x-1)/(x+1)) */
    double x2 = x * x, t = x, s = 0;
    for (int i = 1; i < 60; i += 2) { s += t / i; t *= -x2; }
    if (meio) s += M_PI / 4;
    if (inv) s = M_PI_2 - s;
    return neg ? -s : s;
}

double atan2(double y, double x)
{
    if (isnan(x) || isnan(y)) return nan_();
    if (x > 0) return atan(y / x);
    if (x < 0) return y >= 0 ? atan(y / x) + M_PI : atan(y / x) - M_PI;
    return y > 0 ? M_PI_2 : y < 0 ? -M_PI_2 : 0;
}

double asin(double x)
{
    if (x > 1 || x < -1) return nan_();
    return atan2(x, sqrt(1 - x * x));
}

double acos(double x)
{
    if (x > 1 || x < -1) return nan_();
    return atan2(sqrt(1 - x * x), x);
}

float fabsf(float x) { return (float)fabs(x); }
float floorf(float x) { return (float)floor(x); }
float ceilf(float x) { return (float)ceil(x); }
float sqrtf(float x) { return (float)sqrt(x); }
float roundf(float x) { return (float)round(x); }
