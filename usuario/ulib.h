/*
 * ===========================================================================
 *  NovaEra - usuario/ulib.h
 *  Biblioteca mínima dos programas do usuário (anel 3).
 *  Um programa NÃO pode tocar no hardware nem na memória do núcleo:
 *  tudo o que ele faz além de calcular passa por int 0x80.
 *  Sem assembly embutido: o que precisa de assembly está em crt0.asm.
 * ===========================================================================
 */
#ifndef ULIB_H
#define ULIB_H

#include "../novaera.h"

/* ---------------------------- Chamadas de sistema ------------------------ */

/* Em crt0.asm: executa int 0x80 com RAX = numero, RBX = a, RCX = b. */
long syscall2(u64 numero, u64 a, u64 b);

static inline long sys_enviar(u32 destino, msg_t *m)    { return syscall2(SYS_ENVIAR, destino, (u64)m); }
static inline long sys_receber(u32 origem, msg_t *m)    { return syscall2(SYS_RECEBER, origem, (u64)m); }
static inline long sys_chamar(u32 destino, msg_t *m)    { return syscall2(SYS_CHAMAR, destino, (u64)m); }
static inline long sys_responder(u32 destino, msg_t *m) { return syscall2(SYS_RESPONDER, destino, (u64)m); }
static inline long sys_ceder(void)                      { return syscall2(SYS_CEDER, 0, 0); }
static inline long sys_irq_assinar(u32 irq)             { return syscall2(SYS_IRQ_ASSINAR, irq, 0); }
static inline u32  sys_tempo(void)                      { return (u32)syscall2(SYS_TEMPO, 0, 0); }
static inline long sys_tarefa_info(u32 pid, msg_t *m)   { return syscall2(SYS_TAREFA_INFO, pid, (u64)m); }
static inline u64  sys_memoria(void)                    { return (u64)syscall2(SYS_MEMORIA, 0, 0); }
static inline void sys_sair(void)                      { syscall2(SYS_SAIR, 0, 0); for (;;) ; }

/* ----------------- Portas de E/S (só para drivers, IOPL 3) --------------- */

void outb(u16 porta, u8 valor);       /* crt0.asm */
u8   inb(u16 porta);

/* ------------------------------ Utilitários ------------------------------ */

static inline u32 str_tam(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    return n;
}

static inline int str_igual(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static inline void msg_zera(msg_t *m)
{
    u8 *p = (u8 *)m;
    for (u32 i = 0; i < sizeof(*m); i++) p[i] = 0;
}

/* Escreve n em decimal em buf (precisa de 11 bytes); devolve buf. */
static inline char *num_texto(u32 n, char *buf)
{
    char tmp[11];
    int i = 0;
    do { tmp[i++] = (char)('0' + n % 10); n /= 10; } while (n);
    int j = 0;
    while (i) buf[j++] = tmp[--i];
    buf[j] = '\0';
    return buf;
}

/* Cores VGA (texto | fundo << 4), as mesmas do núcleo */
enum {
    COR_PRETO = 0, COR_AZUL, COR_VERDE, COR_CIANO, COR_VERMELHO, COR_MAGENTA,
    COR_MARROM, COR_CINZA_CLARO, COR_CINZA_ESCURO, COR_AZUL_CLARO,
    COR_VERDE_CLARO, COR_CIANO_CLARO, COR_VERMELHO_CLARO, COR_MAGENTA_CLARO,
    COR_AMARELO, COR_BRANCO
};
#define COR(texto) ((u8)((texto) | (COR_AZUL << 4)))

/* ------------------------------ Ponto de entrada -------------------------
 * Todo programa define main(). Quem a chama é _start, em crt0.asm, que
 * entrega os dois argumentos que o núcleo deixou em RBX e RCX.            */

void main(u64 arg1, u64 arg2);

#endif
