/*
 * nlibc - libc/src/numeros.c
 * Números com vírgula EXATOS: strtod e os algarismos decimais de um double.
 *
 * Um double é m x 2^e (m inteiro de 53 bits). Para não errar nem uma
 * casa, as contas são feitas com inteiros grandes ("bignum"), em pedaços
 * de 32 bits, e não com o próprio double:
 *   - double -> texto: m x 2^e vira um inteiro N e uma posição de vírgula;
 *     os algarismos de N são exatos (até ~770 deles).
 *   - texto -> double: o número digitado vira D x 10^E; uma divisão longa
 *     dá os 64 bits mais altos de D x 10^E e se sobrou resto; com isso o
 *     arredondamento para 53 bits é o correto (metade vai para o par).
 */
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include "interno.h"

/* ------------------------------- Bignum ---------------------------------- */

#define PEDACOS 240                     /* 7680 bits */

typedef struct {
    u32 d[PEDACOS];                     /* d[0] = os 32 bits mais baixos */
    int n;                              /* pedaços em uso (sem zeros no topo) */
} grande_t;

static void g_aparar(grande_t *a)
{
    while (a->n > 0 && a->d[a->n - 1] == 0) a->n--;
}

static void g_de_u64(grande_t *a, u64 v)
{
    a->n = 0;
    while (v) { a->d[a->n++] = (u32)v; v >>= 32; }
}

static void g_copiar(grande_t *dst, const grande_t *src)
{
    dst->n = src->n;
    for (int i = 0; i < src->n; i++) dst->d[i] = src->d[i];
}

static void g_vezes(grande_t *a, u32 m)
{
    u64 vai = 0;
    for (int i = 0; i < a->n; i++) {
        u64 t = (u64)a->d[i] * m + vai;
        a->d[i] = (u32)t;
        vai = t >> 32;
    }
    if (vai && a->n < PEDACOS) a->d[a->n++] = (u32)vai;
}

static void g_mais(grande_t *a, u32 v)
{
    u64 vai = v;
    for (int i = 0; vai && i < a->n; i++) {
        u64 t = (u64)a->d[i] + vai;
        a->d[i] = (u32)t;
        vai = t >> 32;
    }
    if (vai && a->n < PEDACOS) a->d[a->n++] = (u32)vai;
}

static int g_bits(const grande_t *a)
{
    if (!a->n) return 0;
    u32 topo = a->d[a->n - 1];
    int b = 0;
    while (topo) { b++; topo >>= 1; }
    return (a->n - 1) * 32 + b;
}

/* a <<= s (multiplica por 2^s) */
static void g_desloca(grande_t *a, int s)
{
    if (!a->n || s <= 0) return;
    int inteiros = s / 32, bits = s % 32;
    int novo_n = a->n + inteiros + 1;
    if (novo_n > PEDACOS) novo_n = PEDACOS;
    for (int i = novo_n - 1; i >= 0; i--) {
        int src = i - inteiros;
        u32 alto  = (src >= 0 && src < a->n) ? a->d[src] : 0;
        u32 baixo = (src - 1 >= 0 && src - 1 < a->n) ? a->d[src - 1] : 0;
        a->d[i] = bits ? (alto << bits) | (baixo >> (32 - bits)) : alto;
    }
    a->n = novo_n;
    g_aparar(a);
}

static int g_comparar(const grande_t *a, const grande_t *b)
{
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; i--)
        if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}

/* a -= b (com a >= b) */
static void g_menos(grande_t *a, const grande_t *b)
{
    long long pede = 0;
    for (int i = 0; i < a->n; i++) {
        long long t = (long long)a->d[i] - (i < b->n ? (long long)b->d[i] : 0) - pede;
        pede = t < 0;
        a->d[i] = (u32)(t + (pede ? ((long long)1 << 32) : 0));
    }
    g_aparar(a);
}

/* a /= d; devolve o resto */
static u32 g_dividir_pequeno(grande_t *a, u32 d)
{
    u64 resto = 0;
    for (int i = a->n - 1; i >= 0; i--) {
        u64 t = (resto << 32) | a->d[i];
        a->d[i] = (u32)(t / d);
        resto = t % d;
    }
    g_aparar(a);
    return (u32)resto;
}

/* q = A / B, com q < 2^64; A fica com o resto; *sobrou = resto != 0 */
static u64 g_dividir(grande_t *a, const grande_t *b, int *sobrou)
{
    u64 q = 0;
    grande_t t;
    for (int i = g_bits(a) - g_bits(b); i >= 0; i--) {
        g_copiar(&t, b);
        g_desloca(&t, i);
        if (g_comparar(a, &t) >= 0) {
            g_menos(a, &t);
            if (i < 64) q |= (u64)1 << i;
        }
    }
    *sobrou = a->n != 0;
    return q;
}

static grande_t g1, g2;                 /* grandes demais para a pilha */

/* ---------------------------- Os bits de um double ----------------------- */

typedef union { double d; u64 u; } dbits_t;

static double de_bits(u64 u)
{
    dbits_t x;
    x.u = u;
    return x.d;
}

/* q x 2^e2 (mais um "sobrou algo abaixo") arredondado para o double mais
 * próximo; empate vai para o par. */
static double montar(u64 q, int e2, int sobrou, int neg)
{
    u64 sinal = neg ? (u64)1 << 63 : 0;
    if (!q) return de_bits(sinal);
    while (!(q >> 63)) { q <<= 1; e2--; }       /* bit 63 ligado */
    int expoente = e2 + 63;                     /* do bit mais alto */

    if (expoente > 1023) { errno = ERANGE; return de_bits(sinal | 0x7FF0000000000000ull); }

    int corte = 11;                             /* bits que sobram além dos 53 */
    if (expoente < -1022) corte += -1022 - expoente;   /* subnormal */
    u64 m, resto, metade;
    if (corte >= 65) {
        errno = ERANGE;
        return de_bits(sinal);                  /* menor que meio subnormal */
    } else if (corte == 64) {
        m = 0; resto = q; metade = (u64)1 << 63;
    } else {
        m = q >> corte;
        resto = q & (((u64)1 << corte) - 1);
        metade = (u64)1 << (corte - 1);
    }
    if (resto > metade || (resto == metade && (sobrou || (m & 1)))) m++;

    if (expoente < -1022) {                     /* subnormal (ou virou o menor normal) */
        if (!m) errno = ERANGE;
        return de_bits(sinal | m);
    }
    if (m >> 53) { m >>= 1; expoente++; }       /* arredondou para cima: 1.111 -> 10.000 */
    if (expoente > 1023) { errno = ERANGE; return de_bits(sinal | 0x7FF0000000000000ull); }
    return de_bits(sinal | ((u64)(expoente + 1023) << 52) | (m & (((u64)1 << 52) - 1)));
}

/* --------------------------------- strtod -------------------------------- */

#define MAX_ALGARISMOS 800

static int igual_sem_caixa(const char *p, const char *palavra)
{
    for (; *palavra; p++, palavra++)
        if (tolower((unsigned char)*p) != *palavra) return 0;
    return 1;
}

static double ler_hexa(const char *p, char **fim, int neg)
{
    u64 m = 0;
    int e2 = 0, sobrou = 0, ponto = 0, algum = 0;
    for (;; p++) {
        int d;
        if (*p == '.' && !ponto) { ponto = 1; continue; }
        if (isdigit((unsigned char)*p)) d = *p - '0';
        else if (isxdigit((unsigned char)*p)) d = tolower((unsigned char)*p) - 'a' + 10;
        else break;
        algum = 1;
        if (m >> 60) {                          /* não cabe mais: vira "sobrou" */
            if (d) sobrou = 1;
            if (!ponto) e2 += 4;
        } else {
            m = (m << 4) | (u64)d;
            if (ponto) e2 -= 4;
        }
    }
    if (algum && (*p == 'p' || *p == 'P')) {
        const char *q = p + 1;
        int sinal = 1, v = 0;
        if (*q == '+' || *q == '-') sinal = *q++ == '-' ? -1 : 1;
        if (isdigit((unsigned char)*q)) {
            while (isdigit((unsigned char)*q)) { if (v < 100000) v = v * 10 + (*q - '0'); q++; }
            e2 += sinal * v;
            p = q;
        }
    }
    if (fim) *fim = (char *)p;
    return montar(m, e2, sobrou, neg);
}

double strtod(const char *s, char **fim)
{
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;
    int neg = 0;
    if (*p == '+' || *p == '-') neg = *p++ == '-';

    if (igual_sem_caixa(p, "inf")) {
        p += igual_sem_caixa(p, "infinity") ? 8 : 3;
        if (fim) *fim = (char *)p;
        return de_bits((neg ? (u64)1 << 63 : 0) | 0x7FF0000000000000ull);
    }
    if (igual_sem_caixa(p, "nan")) {
        p += 3;
        if (*p == '(') { const char *q = p; while (*q && *q != ')') q++; if (*q) p = q + 1; }
        if (fim) *fim = (char *)p;
        return de_bits(0x7FF8000000000000ull);
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        (isxdigit((unsigned char)p[2]) || (p[2] == '.' && isxdigit((unsigned char)p[3]))))
        return ler_hexa(p + 2, fim, neg);

    /* Decimal: o número vira D x 10^exp10 */
    grande_t *d = &g1;
    d->n = 0;
    int ndig = 0, exp10 = 0, algum = 0, ponto = 0, perdeu = 0;
    for (;; p++) {
        if (*p == '.' && !ponto) { ponto = 1; continue; }
        if (!isdigit((unsigned char)*p)) break;
        algum = 1;
        int c = *p - '0';
        if (c == 0 && ndig == 0) { if (ponto) exp10--; continue; }      /* zeros à esquerda */
        if (ndig < MAX_ALGARISMOS) {
            g_vezes(d, 10);
            g_mais(d, (u32)c);
            ndig++;
            if (ponto) exp10--;
        } else {                                /* algarismos demais: só importa se há algo */
            if (c) perdeu = 1;
            if (!ponto) exp10++;
        }
    }
    if (!algum) { if (fim) *fim = (char *)s; return 0; }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        int sinal = 1, v = 0;
        if (*q == '+' || *q == '-') sinal = *q++ == '-' ? -1 : 1;
        if (isdigit((unsigned char)*q)) {
            while (isdigit((unsigned char)*q)) { if (v < 100000) v = v * 10 + (*q - '0'); q++; }
            exp10 += sinal * v;
            p = q;
        }
    }
    if (fim) *fim = (char *)p;
    if (!d->n) return neg ? de_bits((u64)1 << 63) : 0.0;
    if (perdeu) { g_vezes(d, 10); g_mais(d, 1); ndig++; exp10--; }   /* desempata para cima */

    int ordem = ndig + exp10;                   /* o valor fica abaixo de 10^ordem */
    if (ordem > 310) { errno = ERANGE; return de_bits((neg ? (u64)1 << 63 : 0) | 0x7FF0000000000000ull); }
    if (ordem < -330) { errno = ERANGE; return neg ? de_bits((u64)1 << 63) : 0.0; }

    grande_t *pot = &g2;                        /* 10^-exp10, quando exp10 < 0 */
    g_de_u64(pot, 1);
    for (; exp10 > 0; exp10--) g_vezes(d, 10);
    for (; exp10 < 0; exp10++) g_vezes(pot, 10);

    /* q = D x 2^s / pot fica entre 2^62 e 2^64: os bits que importam. */
    int desloc = 63 + g_bits(pot) - g_bits(d);
    if (desloc >= 0) g_desloca(d, desloc); else g_desloca(pot, -desloc);
    int sobrou;
    u64 q = g_dividir(d, pot, &sobrou);
    return montar(q, -desloc, sobrou, neg);
}

float strtof(const char *s, char **fim)
{
    return (float)strtod(s, fim);
}

/* ---------------------- Os algarismos exatos de um double ----------------- */

int __dbl_digitos(double x, char *digitos, int max, int *exp10)
{
    dbits_t b;
    b.d = x;
    int campo = (int)((b.u >> 52) & 0x7FF);
    u64 m = b.u & (((u64)1 << 52) - 1);
    if (!campo && !m) { digitos[0] = '0'; digitos[1] = '\0'; *exp10 = 1; return 1; }
    int e = campo ? campo - 1075 : -1074;
    if (campo) m |= (u64)1 << 52;

    /* x = m x 2^e. Com e < 0: x = (m x 5^-e) / 10^-e. */
    grande_t *n = &g1;
    g_de_u64(n, m);
    int casas = 0;                              /* algarismos depois da vírgula */
    if (e >= 0) {
        g_desloca(n, e);
    } else {
        int k = -e;
        for (; k >= 13; k -= 13) g_vezes(n, 1220703125u);   /* 5^13 de cada vez */
        for (; k > 0; k--) g_vezes(n, 5);
        casas = -e;
    }

    char tmp[1200];
    int t = 0;
    while (n->n) {                              /* blocos de 9 algarismos */
        u32 r = g_dividir_pequeno(n, 1000000000u);
        for (int k = 0; k < 9; k++) { tmp[t++] = (char)('0' + r % 10); r /= 10; }
    }
    while (t > 1 && tmp[t - 1] == '0') t--;     /* zeros à esquerda (o texto está ao contrário) */
    int saida = 0;
    for (int i = t - 1; i >= 0 && saida < max - 1; i--) digitos[saida++] = tmp[i];
    *exp10 = t - casas;
    while (saida > 1 && digitos[saida - 1] == '0') saida--;   /* zeros à direita não mudam o valor */
    digitos[saida] = '\0';
    return saida;
}
