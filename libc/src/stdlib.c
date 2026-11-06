/* nlibc - stdlib.h (menos malloc e strtod, que têm arquivos próprios) */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include "interno.h"

/* ------------------------------- Fim do processo ------------------------- */

static void (*ao_sair[32])(void);
static int n_ao_sair;

int atexit(void (*f)(void))
{
    if (n_ao_sair == 32) return -1;
    ao_sair[n_ao_sair++] = f;
    return 0;
}

_Noreturn void exit(int codigo)
{
    while (n_ao_sair) ao_sair[--n_ao_sair]();
    __stdio_esvaziar_todos();
    __fds_fechar_todos();                        /* pipes: quem lê recebe o fim */
    sys_sair(codigo);
    for (;;) ;
}

_Noreturn void _Exit(int codigo)
{
    sys_sair(codigo);
    for (;;) ;
}

_Noreturn void abort(void)
{
    __stdio_esvaziar_todos();
    sys_sair(134);                               /* como o SIGABRT do Unix: 128 + 6 */
    for (;;) ;
}

char *getenv(const char *nome) { (void)nome; return NULL; }   /* ainda sem ambiente */
int system(const char *comando) { (void)comando; return -1; }

/* ---------------------------------- Inteiros ------------------------------ */

int abs(int x)              { return x < 0 ? -x : x; }
long labs(long x)           { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }
div_t div(int a, int b)     { div_t r; r.quot = a / b; r.rem = a % b; return r; }
ldiv_t ldiv(long a, long b) { ldiv_t r; r.quot = a / b; r.rem = a % b; return r; }

static unsigned long semente = 1;
int rand(void)
{
    semente = semente * 1103515245 + 12345;
    return (int)((semente >> 16) & RAND_MAX);
}
void srand(unsigned s) { semente = s; }

/* Lê um inteiro sem sinal na base dada; 'neg' recebe o sinal. Devolve o
 * valor (saturado em 'limite' com ERANGE) e aponta *fim para depois dele. */
static unsigned long long ler_inteiro(const char *s, char **fim, int base, int *neg,
                                      unsigned long long limite)
{
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;
    *neg = 0;
    if (*p == '+' || *p == '-') *neg = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        isxdigit((unsigned char)p[2])) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = *p == '0' ? 8 : 10;
    }
    unsigned long long v = 0;
    int algum = 0, estourou = 0;
    for (;; p++) {
        int d;
        if (isdigit((unsigned char)*p)) d = *p - '0';
        else if (isalpha((unsigned char)*p)) d = tolower((unsigned char)*p) - 'a' + 10;
        else break;
        if (d >= base) break;
        algum = 1;
        if (v > (limite - (unsigned long long)d) / (unsigned long long)base) estourou = 1;
        else v = v * (unsigned long long)base + (unsigned long long)d;
    }
    if (fim) *fim = (char *)(algum ? p : s);
    if (estourou) { errno = ERANGE; return limite; }
    return v;
}

long long strtoll(const char *s, char **fim, int base)
{
    int neg;
    unsigned long long v = ler_inteiro(s, fim, base, &neg, (unsigned long long)LLONG_MAX + 1);
    if (!neg && v > (unsigned long long)LLONG_MAX) { errno = ERANGE; return LLONG_MAX; }
    return neg ? (long long)(0 - v) : (long long)v;
}

unsigned long long strtoull(const char *s, char **fim, int base)
{
    int neg;
    unsigned long long v = ler_inteiro(s, fim, base, &neg, ULLONG_MAX);
    return neg ? 0 - v : v;
}

long strtol(const char *s, char **fim, int base)            { return strtoll(s, fim, base); }
unsigned long strtoul(const char *s, char **fim, int base)  { return strtoull(s, fim, base); }
int atoi(const char *s)        { return (int)strtol(s, NULL, 10); }
long atol(const char *s)       { return strtol(s, NULL, 10); }
long long atoll(const char *s) { return strtoll(s, NULL, 10); }
double atof(const char *s)     { return strtod(s, NULL); }

/* ------------------------------ Ordenação e busca ------------------------- */

/* Merge sort estável (a mesma ordem para elementos iguais que a glibc usa
 * quando há memória). Sem memória, cai num insertion sort. */
static void misturar(char *base, char *tmp, size_t n, size_t tam,
                     int (*cmp)(const void *, const void *))
{
    if (n < 2) return;
    size_t meio = n / 2;
    misturar(base, tmp, meio, tam, cmp);
    misturar(base + meio * tam, tmp, n - meio, tam, cmp);
    size_t i = 0, j = meio, k = 0;
    while (i < meio && j < n) {
        if (cmp(base + j * tam, base + i * tam) < 0) memcpy(tmp + k++ * tam, base + j++ * tam, tam);
        else                                         memcpy(tmp + k++ * tam, base + i++ * tam, tam);
    }
    while (i < meio) memcpy(tmp + k++ * tam, base + i++ * tam, tam);
    while (j < n)    memcpy(tmp + k++ * tam, base + j++ * tam, tam);
    memcpy(base, tmp, n * tam);
}

void qsort(void *base, size_t n, size_t tam, int (*cmp)(const void *, const void *))
{
    if (n < 2 || !tam) return;
    char *tmp = malloc(n * tam);
    if (tmp) {
        misturar(base, tmp, n, tam, cmp);
        free(tmp);
        return;
    }
    char *b = base, troca[256];
    for (size_t i = 1; i < n; i++)
        for (size_t j = i; j > 0 && cmp(b + (j - 1) * tam, b + j * tam) > 0; j--)
            for (size_t k = 0; k < tam; k += sizeof(troca)) {
                size_t m = tam - k < sizeof(troca) ? tam - k : sizeof(troca);
                memcpy(troca, b + (j - 1) * tam + k, m);
                memcpy(b + (j - 1) * tam + k, b + j * tam + k, m);
                memcpy(b + j * tam + k, troca, m);
            }
}

void *bsearch(const void *chave, const void *base, size_t n, size_t tam,
              int (*cmp)(const void *, const void *))
{
    const char *b = base;
    while (n) {
        const char *meio = b + (n / 2) * tam;
        int r = cmp(chave, meio);
        if (r == 0) return (void *)meio;
        if (r > 0) { b = meio + tam; n -= n / 2 + 1; }
        else       { n /= 2; }
    }
    return NULL;
}
