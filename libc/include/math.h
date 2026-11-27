/*
 * nlibc - math.h: as funções de matemática mais usadas.
 *
 * Em double (64 bits). As exatas (floor, ceil, fabs, trunc, round, fmod
 * para números pequenos) dão o mesmo resultado que no Linux; as outras
 * (sqrt, exp, log, pow, sin, cos, atan...) são séries de Taylor, com erro
 * de poucos ULPs: boas para desenhar, não para ciência.
 */
#ifndef _MATH_H
#define _MATH_H

#define M_PI     3.14159265358979323846
#define M_PI_2   1.57079632679489661923
#define M_E      2.7182818284590452354
#define M_LN2    0.69314718055994530942
#define M_SQRT2  1.41421356237309504880
#define HUGE_VAL (1e300 * 1e300)
#define INFINITY ((float)HUGE_VAL)
#define NAN      (0.0f / 0.0f)

#define isnan(x) ((x) != (x))
#define isinf(x) (!isnan(x) && isnan((x) - (x)))
#define isfinite(x) (!isnan((x) - (x)))

double fabs(double x);
double floor(double x);
double ceil(double x);
double trunc(double x);
double round(double x);
double fmod(double x, double y);
double sqrt(double x);
double exp(double x);
double log(double x);
double log2(double x);
double log10(double x);
double pow(double x, double y);
double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);
double asin(double x);
double acos(double x);
double ldexp(double x, int e);
double frexp(double x, int *e);

float fabsf(float x);
float floorf(float x);
float ceilf(float x);
float sqrtf(float x);
float roundf(float x);
#endif
