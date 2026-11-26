/*
 * nlibc - libc/src/formatar.c
 * O motor do printf (__formatar) e do scanf (__varrer). As funções de
 * stdio.c só dizem para onde o texto vai (arquivo, buffer) ou de onde vem.
 *
 * printf: %d %i %u %o %x %X %c %s %p %n %% e os de vírgula %f %F %e %E %g %G,
 * com as flags - + espaço # 0, largura, precisão (também com *) e os
 * tamanhos hh h l ll z j t.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "interno.h"

/* ---------------------------------- printf -------------------------------- */

typedef struct {
    int menos, mais, espaco, cerquilha, zero;
    int largura, precisao;                  /* precisao < 0: não informada */
} especif_t;

typedef struct {
    nl_saida_t saida;
    void      *ctx;
    int        total;
} destino_t;

static void sai(destino_t *d, const char *s, size_t n)
{
    if (n) d->saida(d->ctx, s, n);
    d->total += (int)n;
}

static void sai_repetido(destino_t *d, char c, int n)
{
    char buf[32];
    memset(buf, c, sizeof(buf));
    while (n > 0) {
        int k = n < 32 ? n : 32;
        sai(d, buf, (size_t)k);
        n -= k;
    }
}

/* Escreve prefixo (sinal, 0x) + corpo com a largura e as flags. 'zeros'
 * diz se o '0' da flag vale (não vale com precisão em inteiros). */
static void campo(destino_t *d, const especif_t *e, const char *prefixo,
                  const char *corpo, int n_corpo, int zeros)
{
    int n_prefixo = (int)strlen(prefixo);
    int sobra = e->largura - n_prefixo - n_corpo;
    if (sobra < 0) sobra = 0;
    if (!e->menos && !(e->zero && zeros)) sai_repetido(d, ' ', sobra);
    sai(d, prefixo, (size_t)n_prefixo);
    if (!e->menos && e->zero && zeros) sai_repetido(d, '0', sobra);
    sai(d, corpo, (size_t)n_corpo);
    if (e->menos) sai_repetido(d, ' ', sobra);
}

static void inteiro(destino_t *d, const especif_t *e, unsigned long long v, int neg,
                    int base, int maiuscula, int com_sinal)
{
    const char *alg = maiuscula ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[64];
    int n = 0;
    while (v) { tmp[63 - n++] = alg[v % (unsigned)base]; v /= (unsigned)base; }
    int prec = e->precisao < 0 ? 1 : e->precisao;
    while (n < prec && n < 63) tmp[63 - n++] = '0';
    if (base == 8 && e->cerquilha && (n == 0 || tmp[64 - n] != '0')) tmp[63 - n++] = '0';

    char prefixo[4] = "";
    if (com_sinal) {
        if (neg) strcpy(prefixo, "-");
        else if (e->mais) strcpy(prefixo, "+");
        else if (e->espaco) strcpy(prefixo, " ");
    }
    if (base == 16 && e->cerquilha && n && !(n == 1 && tmp[63] == '0'))
        strcpy(prefixo, maiuscula ? "0X" : "0x");
    campo(d, e, prefixo, tmp + 64 - n, n, e->precisao < 0);
}

/* Arredonda o número 0.dig x 10^*exp para 'manter' algarismos significativos
 * (o empate exato vai para o par). Devolve quantos algarismos restaram. */
static int arredondar(char *dig, int n, int manter, int *exp)
{
    if (manter >= n) return n;
    if (manter < 0) return 0;                   /* tudo some: vira zero */
    int resto_nao_zero = 0;
    for (int i = manter + 1; i < n; i++) if (dig[i] != '0') { resto_nao_zero = 1; break; }
    char c = dig[manter];
    int anterior_impar = manter > 0 && ((dig[manter - 1] - '0') & 1);
    int sobe = c > '5' || (c == '5' && (resto_nao_zero || anterior_impar));
    n = manter;
    if (!sobe) return n;
    int i = n - 1;
    while (i >= 0 && dig[i] == '9') dig[i--] = '0';
    if (i >= 0) {
        dig[i]++;
    } else {                                    /* 9,99 -> 10,0: um algarismo a mais */
        dig[0] = '1';
        if (n == 0) n = 1;
        (*exp)++;
    }
    return n;
}

static char algarismo(const char *dig, int n, int i)
{
    return (i >= 0 && i < n) ? dig[i] : '0';
}

/* Monta "ddd.ddd" (precisão p) a partir de 0.dig x 10^exp já arredondado. */
static int fixo(char *s, const char *dig, int n, int exp, int p, int cerquilha)
{
    int k = 0;
    if (exp <= 0) s[k++] = '0';
    for (int i = 0; i < exp; i++) s[k++] = algarismo(dig, n, i);
    if (p > 0 || cerquilha) s[k++] = '.';
    for (int i = 0; i < p; i++) s[k++] = algarismo(dig, n, exp + i);
    return k;
}

/* Monta "d.ddde+XX" (precisão p). */
static int cientifico(char *s, const char *dig, int n, int exp, int p, int cerquilha,
                      int maiuscula, int zero)
{
    int k = 0;
    s[k++] = algarismo(dig, n, 0);
    if (p > 0 || cerquilha) s[k++] = '.';
    for (int i = 1; i <= p; i++) s[k++] = algarismo(dig, n, i);
    int x = zero ? 0 : exp - 1;
    s[k++] = maiuscula ? 'E' : 'e';
    s[k++] = x < 0 ? '-' : '+';
    if (x < 0) x = -x;
    char t[8];
    int nt = 0;
    do { t[nt++] = (char)('0' + x % 10); x /= 10; } while (x);
    if (nt < 2) t[nt++] = '0';
    while (nt) s[k++] = t[--nt];
    return k;
}

static void virgula(destino_t *d, const especif_t *e, double x, char conv)
{
    union { double d; u64 u; } b;
    b.d = x;
    int neg = (int)(b.u >> 63);
    int maiuscula = conv == 'F' || conv == 'E' || conv == 'G';
    char prefixo[2] = { 0, 0 };
    if (neg) prefixo[0] = '-';
    else if (e->mais) prefixo[0] = '+';
    else if (e->espaco) prefixo[0] = ' ';

    if (((b.u >> 52) & 0x7FF) == 0x7FF) {      /* infinito ou NaN */
        int nan = (b.u & (((u64)1 << 52) - 1)) != 0;
        const char *t = nan ? (maiuscula ? "NAN" : "nan") : (maiuscula ? "INF" : "inf");
        if (nan) prefixo[0] = 0;
        campo(d, e, prefixo, t, 3, 0);
        return;
    }

    static char dig[1200], corpo[1700];
    int exp;
    int n = __dbl_digitos(x, dig, sizeof(dig), &exp);
    int zero = dig[0] == '0';
    int p = e->precisao < 0 ? 6 : e->precisao;
    int k;
    char c = (char)tolower((unsigned char)conv);

    if (c == 'f') {
        if (!zero) n = arredondar(dig, n, exp + p, &exp);
        if (n == 0) { zero = 1; exp = 1; }
        k = fixo(corpo, dig, zero ? 0 : n, zero ? 1 : exp, p, e->cerquilha);
    } else if (c == 'e') {
        if (!zero) n = arredondar(dig, n, p + 1, &exp);
        k = cientifico(corpo, dig, zero ? 0 : n, exp, p, e->cerquilha, maiuscula, zero);
    } else {                                    /* %g: o mais curto entre %e e %f */
        if (p == 0) p = 1;
        if (!zero) n = arredondar(dig, n, p, &exp);
        int x10 = zero ? 0 : exp - 1;
        if (p > x10 && x10 >= -4)
            k = fixo(corpo, dig, zero ? 0 : n, zero ? 1 : exp, p - 1 - x10, e->cerquilha);
        else
            k = cientifico(corpo, dig, zero ? 0 : n, exp, p - 1, e->cerquilha, maiuscula, zero);
        if (!e->cerquilha) {                    /* tira os zeros do fim da parte decimal */
            int ponto = -1, fim_mantissa = k;
            for (int i = 0; i < k; i++) {
                if (corpo[i] == '.') ponto = i;
                if (corpo[i] == 'e' || corpo[i] == 'E') { fim_mantissa = i; break; }
            }
            if (ponto >= 0) {
                int j = fim_mantissa;
                while (j > ponto + 1 && corpo[j - 1] == '0') j--;
                if (j == ponto + 1) j = ponto;
                memmove(corpo + j, corpo + fim_mantissa, (size_t)(k - fim_mantissa));
                k = j + (k - fim_mantissa);
            }
        }
    }
    campo(d, e, prefixo, corpo, k, 1);
}

int __formatar(nl_saida_t saida, void *ctx, const char *fmt, va_list ap)
{
    destino_t d = { saida, ctx, 0 };
    while (*fmt) {
        if (*fmt != '%') {
            const char *ini = fmt;
            while (*fmt && *fmt != '%') fmt++;
            sai(&d, ini, (size_t)(fmt - ini));
            continue;
        }
        fmt++;
        especif_t e;
        memset(&e, 0, sizeof(e));
        e.precisao = -1;
        for (;; fmt++) {
            if      (*fmt == '-') e.menos = 1;
            else if (*fmt == '+') e.mais = 1;
            else if (*fmt == ' ') e.espaco = 1;
            else if (*fmt == '#') e.cerquilha = 1;
            else if (*fmt == '0') e.zero = 1;
            else break;
        }
        if (*fmt == '*') {
            e.largura = va_arg(ap, int);
            if (e.largura < 0) { e.menos = 1; e.largura = -e.largura; }
            fmt++;
        } else {
            while (isdigit((unsigned char)*fmt)) e.largura = e.largura * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.') {
            fmt++;
            e.precisao = 0;
            if (*fmt == '*') { e.precisao = va_arg(ap, int); fmt++; if (e.precisao < 0) e.precisao = -1; }
            else while (isdigit((unsigned char)*fmt)) e.precisao = e.precisao * 10 + (*fmt++ - '0');
        }
        int tam = 0;                            /* 0 int, 1 long, 2 long long, -1 short, -2 char */
        for (;; fmt++) {
            if      (*fmt == 'h') tam = tam == -1 ? -2 : -1;
            else if (*fmt == 'l') tam = tam == 1 ? 2 : 1;
            else if (*fmt == 'z' || *fmt == 'j' || *fmt == 't') tam = 1;
            else if (*fmt == 'L') tam = 1;
            else break;
        }
        char conv = *fmt ? *fmt++ : '\0';
        switch (conv) {
        case 'd': case 'i': {
            long long v = tam >= 1 ? (tam == 2 ? va_arg(ap, long long) : va_arg(ap, long))
                                   : va_arg(ap, int);
            if (tam == -1) v = (short)v;
            if (tam == -2) v = (signed char)v;
            unsigned long long u = v < 0 ? 0 - (unsigned long long)v : (unsigned long long)v;
            inteiro(&d, &e, u, v < 0, 10, 0, 1);
            break;
        }
        case 'u': case 'o': case 'x': case 'X': {
            unsigned long long v = tam >= 1 ? (tam == 2 ? va_arg(ap, unsigned long long)
                                                        : va_arg(ap, unsigned long))
                                            : va_arg(ap, unsigned int);
            if (tam == -1) v = (unsigned short)v;
            if (tam == -2) v = (unsigned char)v;
            inteiro(&d, &e, v, 0, conv == 'u' ? 10 : conv == 'o' ? 8 : 16, conv == 'X', 0);
            break;
        }
        case 'p': {
            especif_t ep = e;
            ep.cerquilha = 1;
            inteiro(&d, &ep, (unsigned long long)(uintptr_t)va_arg(ap, void *), 0, 16, 0, 0);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            campo(&d, &e, "", &c, 1, 0);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int n = (int)(e.precisao >= 0 ? strnlen(s, (size_t)e.precisao) : strlen(s));
            campo(&d, &e, "", s, n, 0);
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
            virgula(&d, &e, va_arg(ap, double), conv == 'a' ? 'e' : conv == 'A' ? 'E' : conv);
            break;
        case 'n':
            *va_arg(ap, int *) = d.total;
            break;
        case '%':
            sai(&d, "%", 1);
            break;
        default:                                /* desconhecido: mostra como veio */
            sai(&d, "%", 1);
            if (conv) sai(&d, &conv, 1);
            break;
        }
    }
    return d.total;
}

/* ---------------------------------- scanf --------------------------------- */

typedef struct {
    nl_le_t      le;
    nl_devolve_t devolve;
    void        *ctx;
    int          lidos;
} fonte_t;

static int pega(fonte_t *f)
{
    int c = f->le(f->ctx);
    if (c != EOF) f->lidos++;
    return c;
}

static void devolve(fonte_t *f, int c)
{
    if (c == EOF) return;
    f->devolve(f->ctx, c);
    f->lidos--;
}

/* Junta os caracteres de um número em 'buf' (no máximo 'max'). 'hexa' = 1
 * aceita a-f mesmo sem o "0x" (o %x). */
static int juntar(fonte_t *f, char *buf, int max, int virgula, int hexa)
{
    int n = 0, c = pega(f);
    if ((c == '+' || c == '-') && n < max) { buf[n++] = (char)c; c = pega(f); }
    if (c == '0' && n < max) {
        buf[n++] = (char)c;
        c = pega(f);
        if ((c == 'x' || c == 'X') && n < max) { buf[n++] = (char)c; c = pega(f); hexa = 1; }
    }
    int ponto = 0, expo = 0;
    for (; c != EOF && n < max; c = pega(f)) {
        if (isdigit(c) || (hexa && isxdigit(c))) { buf[n++] = (char)c; continue; }
        if (virgula && c == '.' && !ponto && !expo) { ponto = 1; buf[n++] = (char)c; continue; }
        if (virgula && !expo && ((!hexa && (c == 'e' || c == 'E')) || (hexa && (c == 'p' || c == 'P')))) {
            expo = 1;
            buf[n++] = (char)c;
            c = pega(f);
            if ((c == '+' || c == '-') && n < max) { buf[n++] = (char)c; continue; }
            devolve(f, c);
            continue;
        }
        break;
    }
    devolve(f, c);
    buf[n] = '\0';
    return n;
}

int __varrer(nl_le_t le, nl_devolve_t dev, void *ctx, const char *fmt, va_list ap)
{
    fonte_t f = { le, dev, ctx, 0 };
    int atribuidos = 0, c;
    while (*fmt) {
        if (isspace((unsigned char)*fmt)) {
            while (isspace((unsigned char)*fmt)) fmt++;
            while ((c = pega(&f)) != EOF && isspace(c)) ;
            devolve(&f, c);
            continue;
        }
        if (*fmt != '%' || fmt[1] == '%') {     /* um caractere que precisa bater */
            if (*fmt == '%') fmt++;
            c = pega(&f);
            if (c != (unsigned char)*fmt) { devolve(&f, c); return atribuidos ? atribuidos : (c == EOF ? EOF : 0); }
            fmt++;
            continue;
        }
        fmt++;
        int pular = 0, largura = 0, tam = 0;
        if (*fmt == '*') { pular = 1; fmt++; }
        while (isdigit((unsigned char)*fmt)) largura = largura * 10 + (*fmt++ - '0');
        for (;; fmt++) {
            if      (*fmt == 'h') tam = tam == -1 ? -2 : -1;
            else if (*fmt == 'l') tam = tam == 1 ? 2 : 1;
            else if (*fmt == 'z' || *fmt == 'j' || *fmt == 't' || *fmt == 'L') tam = 1;
            else break;
        }
        char conv = *fmt ? *fmt++ : '\0';
        if (conv == 'n') { if (!pular) *va_arg(ap, int *) = f.lidos; continue; }
        if (conv != 'c' && conv != '[') {       /* quase todos pulam espaços antes */
            while ((c = pega(&f)) != EOF && isspace(c)) ;
            if (c == EOF) return atribuidos ? atribuidos : EOF;
            devolve(&f, c);
        }
        char buf[512];
        int max = largura ? (largura < 511 ? largura : 511) : 511;
        switch (conv) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
            if (!juntar(&f, buf, max, 0, conv == 'x' || conv == 'X')) return atribuidos;
            int base = conv == 'd' || conv == 'u' ? 10 : conv == 'i' ? 0 : conv == 'o' ? 8 : 16;
            char *fim;
            unsigned long long v = (unsigned long long)strtoll(buf, &fim, base);
            if (conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o') v = strtoull(buf, &fim, base);
            if (fim == buf) return atribuidos;
            if (!pular) {
                if      (tam == 2)  *va_arg(ap, long long *) = (long long)v;
                else if (tam == 1)  *va_arg(ap, long *) = (long)v;
                else if (tam == -1) *va_arg(ap, short *) = (short)v;
                else if (tam == -2) *va_arg(ap, char *) = (char)v;
                else                *va_arg(ap, int *) = (int)v;
                atribuidos++;
            }
            break;
        }
        case 'f': case 'e': case 'g': case 'E': case 'G': case 'a': {
            if (!juntar(&f, buf, max, 1, 0)) return atribuidos;
            char *fim;
            double v = strtod(buf, &fim);
            if (fim == buf) return atribuidos;
            if (!pular) {
                if (tam >= 1) *va_arg(ap, double *) = v;
                else          *va_arg(ap, float *) = (float)v;
                atribuidos++;
            }
            break;
        }
        case 's': {
            char *dst = pular ? 0 : va_arg(ap, char *);
            int n = 0;
            while (n < max && (c = pega(&f)) != EOF && !isspace(c)) { if (dst) dst[n] = (char)c; n++; }
            if (n < max) devolve(&f, c);
            if (!n) return atribuidos;
            if (dst) { dst[n] = '\0'; atribuidos++; }
            break;
        }
        case 'c': {
            char *dst = pular ? 0 : va_arg(ap, char *);
            int n = largura ? largura : 1;
            for (int i = 0; i < n; i++) {
                if ((c = pega(&f)) == EOF) return atribuidos ? atribuidos : EOF;
                if (dst) dst[i] = (char)c;
            }
            if (dst) atribuidos++;
            break;
        }
        default:
            return atribuidos;
        }
    }
    return atribuidos;
}
