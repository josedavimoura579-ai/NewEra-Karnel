/*
 * nlibc - malloc, calloc, realloc, free
 *
 * Listas de livres separadas por tamanho: cada bloco tem 2^k bytes úteis
 * (k de 4 a 40). malloc tira um bloco da lista certa ou pede mais memória
 * ao núcleo (SYS_SBRK); free devolve o bloco à lista. Rápido e simples;
 * o preço é gastar até o dobro do que se pede.
 */
#include <stdlib.h>
#include <string.h>
#include "interno.h"

#define CLASSES 41

typedef struct nl_bloco {
    size_t        classe;        /* o bloco tem 1 << classe bytes úteis */
    struct nl_bloco *prox;       /* na lista de livres (só quando livre) */
} cabecalho_t;

static cabecalho_t *livres[CLASSES];

static size_t classe_de(size_t n)
{
    size_t c = 4;
    while (c < CLASSES - 1 && ((size_t)1 << c) < n) c++;
    return c;
}

void *malloc(size_t n)
{
    size_t c = classe_de(n ? n : 1);
    if (((size_t)1 << c) < n) return NULL;
    cabecalho_t *b = livres[c];
    if (b) {
        livres[c] = b->prox;
    } else {
        long p = sys_sbrk(sizeof(cabecalho_t) + ((size_t)1 << c));
        if (p < 0) return NULL;
        b = (cabecalho_t *)p;
        b->classe = c;
    }
    return b + 1;
}

void free(void *p)
{
    if (!p) return;
    cabecalho_t *b = (cabecalho_t *)p - 1;
    b->prox = livres[b->classe];
    livres[b->classe] = b;
}

void *calloc(size_t n, size_t tam)
{
    if (tam && n > (size_t)-1 / tam) return NULL;
    void *p = malloc(n * tam);
    if (p) memset(p, 0, n * tam);
    return p;
}

void *realloc(void *p, size_t n)
{
    if (!p) return malloc(n);
    if (!n) { free(p); return NULL; }
    cabecalho_t *b = (cabecalho_t *)p - 1;
    size_t cabe = (size_t)1 << b->classe;
    if (n <= cabe) return p;                     /* já cabe */
    void *novo = malloc(n);
    if (!novo) return NULL;
    memcpy(novo, p, cabe);
    free(p);
    return novo;
}
