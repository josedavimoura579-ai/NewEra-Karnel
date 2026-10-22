/* nlibc - stdlib.h */
#ifndef _STDLIB_H
#define _STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX     2147483647
typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;

void  *malloc(size_t n);
void  *calloc(size_t n, size_t tam);
void  *realloc(void *p, size_t n);
void   free(void *p);

_Noreturn void exit(int codigo);
_Noreturn void _Exit(int codigo);
_Noreturn void abort(void);
int    atexit(void (*funcao)(void));
char  *getenv(const char *nome);
int    system(const char *comando);
int    mkstemp(char *modelo);          /* POSIX */

int    atoi(const char *s);
long   atol(const char *s);
long long atoll(const char *s);
long   strtol(const char *s, char **fim, int base);
unsigned long strtoul(const char *s, char **fim, int base);
long long strtoll(const char *s, char **fim, int base);
unsigned long long strtoull(const char *s, char **fim, int base);
double strtod(const char *s, char **fim);
float  strtof(const char *s, char **fim);
double atof(const char *s);

int    abs(int x);
long   labs(long x);
long long llabs(long long x);
div_t  div(int a, int b);
ldiv_t ldiv(long a, long b);
int    rand(void);
void   srand(unsigned semente);

void   qsort(void *base, size_t n, size_t tam, int (*cmp)(const void *, const void *));
void  *bsearch(const void *chave, const void *base, size_t n, size_t tam,
               int (*cmp)(const void *, const void *));
#endif
