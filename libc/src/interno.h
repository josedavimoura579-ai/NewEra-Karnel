/*
 * nlibc - libc/src/interno.h
 * O que as partes da libc compartilham entre si (não é para os programas).
 * Por baixo, tudo vira mensagens para os servidores do NovaEra (ulib.h).
 */
#ifndef NLIBC_INTERNO_H
#define NLIBC_INTERNO_H

#include <stddef.h>
#include <stdarg.h>
#include "ulib.h"

/* ------------------------- Descritores de arquivo -------------------------
 * Um fd é um servidor (terminal, arquivos ou pipes) + um canal. 'dono' diz
 * se este processo tem uma referência para fechar (pipes e arquivos). */
#define MAX_FDS 32

typedef struct {
    int usado;
    u32 servidor, canal;
    u32 ponta;              /* pipes: TUBO_LEITURA ou TUBO_ESCRITA */
    int dono;
} nfd_t;

extern nfd_t __fds[MAX_FDS];
nfd_t *__fd(int fd);                     /* 0 se o fd não existe */
int    __fd_novo(u32 servidor, u32 canal, u32 ponta, int dono);
void   __fds_fechar_todos(void);

/* ---------------------------------- stdio --------------------------------- */
#define NLIBC_BUF 1024

struct _FILE {
    int  usado;
    int  fd;
    int  ler, escrever;          /* o que o modo permite */
    int  modo_buffer;            /* _IOFBF, _IOLBF ou _IONBF */
    char buf[NLIBC_BUF];
    int  pos, n;                 /* leitura: buf[pos..n); escrita: n bytes pendentes */
    int  escrevendo;
    int  eof, erro;
    int  devolvido;              /* ungetc: o caractere, ou -1 */
};

void __stdio_iniciar(void);
void __stdio_esvaziar_todos(void);

/* --------------------------- printf e scanf -------------------------------- */
typedef void (*nl_saida_t)(void *ctx, const char *dados, size_t n);
int __formatar(nl_saida_t saida, void *ctx, const char *fmt, va_list ap);

typedef int  (*nl_le_t)(void *ctx);
typedef void (*nl_devolve_t)(void *ctx, int c);
int __varrer(nl_le_t le, nl_devolve_t devolve, void *ctx, const char *fmt, va_list ap);

/* ------------------ Números com vírgula: exatos, com bignum ----------------
 * Gera os algarismos decimais EXATOS de um double: value = 0.DIGITOS x 10^exp.
 * 'digitos' recebe até 'max' algarismos (sem zeros à esquerda), terminados
 * em '\0'; devolve quantos. Para 0, devolve "0" e exp = 1.               */
int __dbl_digitos(double x, char *digitos, int max, int *exp10);

/* Converte de errno do NovaEra (ERRO_*) para errno do C. */
int __erro_c(long erro_novaera);

#endif
