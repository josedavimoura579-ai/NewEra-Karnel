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
static inline void sys_sair(int codigo)                 { syscall2(SYS_SAIR, (u64)(long)codigo, 0); for (;;) ; }
static inline long sys_esperar(u32 pid)                 { return syscall2(SYS_ESPERAR, pid, 0); }
static inline long sys_pid(void)                        { return syscall2(SYS_PID, 0, 0); }
static inline long sys_initrd_info(u32 i, msg_t *m)     { return syscall2(SYS_INITRD_INFO, i, (u64)m); }

/* Executa um programa do initrd (ex.: "bin/ola"). argv termina em 0; com
 * argv = 0 o programa recebe só o caminho. Devolve o PID do filho. */
static inline long sys_criar(const char *caminho, char *const *argv)
{
    return syscall2(SYS_CRIAR, (u64)caminho, (u64)argv);
}

/* Aumenta o heap em 'bytes'; devolve o início da área nova (ou erro < 0). */
static inline long sys_sbrk(u64 bytes)                  { return syscall2(SYS_SBRK, bytes, 0); }

/* ----------------- Portas de E/S (só para drivers, IOPL 3) --------------- */

void outb(u16 porta, u8 valor);       /* crt0.asm */
u8   inb(u16 porta);

/* Memória de dispositivo (crt0.asm). O cproc ainda não suporta "volatile":
 * estas funções garantem que o acesso acontece de verdade. */
void mem_escreve16(u64 endereco, u16 valor);
u16  mem_le16(u64 endereco);
u32  mem_le32(u64 endereco);

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

/* ------------------------------- Na tela ---------------------------------
 * Escrever é pedir ao servidor de vídeo (PID 1); textos longos vão em
 * pedaços que cabem numa mensagem.                                         */

static inline void escreve(const char *s, u8 cor)
{
    msg_t m;
    while (*s) {
        msg_zera(&m);
        m.tipo   = MSG_VIDEO_ESCREVER;
        m.arg[0] = cor;
        u32 i = 0;
        while (*s && i < MSG_TEXTO_MAX - 1) m.texto[i++] = *s++;
        sys_enviar(PID_VIDEO, &m);
    }
}

static inline void escreve_num(u32 n, u8 cor)
{
    char buf[11];
    escreve(num_texto(n, buf), cor);
}

/* ------------------------------ Ponto de entrada -------------------------
 * Todo programa define main(argc, argv), como em qualquer C. Quem a chama
 * é _start, em crt0.asm; o valor devolvido vira o código de saída.        */

int main(int argc, char **argv);

#endif
