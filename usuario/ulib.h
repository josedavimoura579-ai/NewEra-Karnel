/*
 * ===========================================================================
 *  NovaEra - usuario/ulib.h
 *  Biblioteca mínima dos programas do usuário (anel 3).
 *  Um programa NÃO pode tocar no hardware nem na memória do núcleo:
 *  tudo o que ele faz além de calcular passa por int 0x80, quase sempre
 *  como uma mensagem para um servidor (terminal, arquivos, pipes...).
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
static inline long sys_criar(criar_t *c)                { return syscall2(SYS_CRIAR, (u64)c, 0); }
static inline long sys_initrd(msg_t *m)                 { return syscall2(SYS_INITRD, (u64)m, 0); }
static inline long sys_ambiente(msg_t *m)               { return syscall2(SYS_AMBIENTE, (u64)m, 0); }
static inline long sys_pasta(const char *caminho)       { return syscall2(SYS_PASTA, (u64)caminho, 0); }
static inline long sys_nucleo(msg_t *m)                 { return syscall2(SYS_NUCLEO, (u64)m, 0); }

/* Aumenta o heap em 'bytes'; devolve o início da área nova (ou erro < 0). */
static inline long sys_sbrk(u64 bytes)                  { return syscall2(SYS_SBRK, bytes, 0); }

/* Só servidores: copiam dados da/para a memória de quem os CHAMOU. */
static inline long sys_copiar_para(u32 pid, void *meu, u64 dele, u64 bytes)
{
    copia_t c = { (u64)meu, dele, bytes };
    return syscall2(SYS_COPIAR_PARA, pid, (u64)&c);
}
static inline long sys_copiar_de(u32 pid, void *meu, u64 dele, u64 bytes)
{
    copia_t c = { (u64)meu, dele, bytes };
    return syscall2(SYS_COPIAR_DE, pid, (u64)&c);
}

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

/* Copia no máximo max-1 caracteres e sempre termina com '\0'. */
static inline void str_copia(char *dst, const char *src, u32 max)
{
    u32 i = 0;
    for (; src[i] && i + 1 < max; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static inline void mem_copia(void *dst, const void *src, u64 n)
{
    u8 *d = dst;
    const u8 *s = src;
    while (n--) *d++ = *s++;
}

static inline void mem_zera(void *dst, u64 n)
{
    u8 *d = dst;
    while (n--) *d++ = 0;
}

static inline void msg_zera(msg_t *m) { mem_zera(m, sizeof(*m)); }

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

static inline u32 texto_num(const char *s)
{
    u32 n = 0;
    while (*s >= '0' && *s <= '9') n = n * 10 + (u32)(*s++ - '0');
    return n;
}

/* Cores VGA (texto | fundo << 4), as mesmas do núcleo */
enum {
    COR_PRETO = 0, COR_AZUL, COR_VERDE, COR_CIANO, COR_VERMELHO, COR_MAGENTA,
    COR_MARROM, COR_CINZA_CLARO, COR_CINZA_ESCURO, COR_AZUL_CLARO,
    COR_VERDE_CLARO, COR_CIANO_CLARO, COR_VERMELHO_CLARO, COR_MAGENTA_CLARO,
    COR_AMARELO, COR_BRANCO
};
#define COR(texto) ((u8)((texto) | (COR_AZUL << 4)))

/* --------------------------------- Memória ------------------------------
 * malloc/free simples: blocos com um cabeçalho de 16 bytes numa lista de
 * livres (o primeiro que couber). O heap cresce com SYS_SBRK.            */

typedef struct bloco {
    u64           tam;               /* bytes úteis do bloco */
    struct bloco *prox;              /* próximo livre (só quando livre) */
} bloco_t;

static bloco_t *ulib_livres;

static inline void *malloc(u64 n)
{
    n = (n + 15) & ~15ull;
    if (n == 0) n = 16;
    for (bloco_t **p = &ulib_livres; *p; p = &(*p)->prox) {
        bloco_t *b = *p;
        if (b->tam >= n) {
            *p = b->prox;
            return b + 1;
        }
    }
    long ini = sys_sbrk(sizeof(bloco_t) + n);
    if (ini < 0) return 0;
    bloco_t *b = (bloco_t *)ini;
    b->tam = n;
    return b + 1;
}

static inline void free(void *p)
{
    if (!p) return;
    bloco_t *b = (bloco_t *)p - 1;
    b->prox = ulib_livres;
    ulib_livres = b;
}

/* -------------------------- Entrada e saída padrão -----------------------
 * Cada processo tem uma entrada e uma saída padrão: um servidor (terminal,
 * pipes ou arquivos) e um canal. O núcleo guarda e os filhos herdam.    */

typedef struct {
    u32  entrada, entrada_canal, saida, saida_canal;
    char pasta[CAMINHO_MAX];
} ambiente_t;

static ambiente_t ulib_amb;
static int        ulib_amb_lido;

static inline ambiente_t *ambiente(void)
{
    if (!ulib_amb_lido) {
        msg_t m;
        sys_ambiente(&m);
        ulib_amb.entrada = m.arg[0];
        ulib_amb.entrada_canal = m.arg[1];
        ulib_amb.saida = m.arg[2];
        ulib_amb.saida_canal = m.arg[3];
        str_copia(ulib_amb.pasta, m.texto, CAMINHO_MAX);
        ulib_amb_lido = 1;
    }
    return &ulib_amb;
}

/* Escreve n bytes num servidor de saída (pedaços de até 63 bytes). */
static inline long escreve_em(u32 servidor, u32 canal, const char *dados, u64 n, u8 cor)
{
    msg_t m;
    u64 total = 0;
    while (total < n) {
        msg_zera(&m);
        m.tipo   = MSG_ESCREVER;
        m.arg[0] = canal;
        m.arg[1] = cor;
        u32 i = 0;
        while (total < n && i < MSG_TEXTO_MAX - 1) m.texto[i++] = dados[total++];
        m.arg[2] = i;
        if (sys_chamar(servidor, &m) < 0 || m.tipo == MSG_ERRO) return ERRO_INVALIDO;
    }
    return (long)total;
}

static inline void escreve(const char *s, u8 cor)
{
    ambiente_t *a = ambiente();
    escreve_em(a->saida, a->saida_canal, s, str_tam(s), cor);
}

static inline void escreve_num(u32 n, u8 cor)
{
    char buf[11];
    escreve(num_texto(n, buf), cor);
}

/* Lê até 'max' bytes de um servidor de entrada; 0 = acabou. */
static inline long ler_de(u32 servidor, u32 canal, char *buf, u32 max)
{
    msg_t m;
    msg_zera(&m);
    m.tipo   = MSG_LER;
    m.arg[0] = canal;
    m.arg[1] = max;
    m.arg[2] = (u32)(u64)buf;          /* os endereços do usuário cabem em 32 bits */
    if (sys_chamar(servidor, &m) < 0) return ERRO_PID;
    if (m.tipo == MSG_ERRO) return -(long)m.arg[0];
    return m.arg[0];
}

/* Lê da entrada padrão. No terminal, devolve uma linha inteira (com '\n'). */
static inline long ler(char *buf, u32 max)
{
    ambiente_t *a = ambiente();
    return ler_de(a->entrada, a->entrada_canal, buf, max);
}

/* -------------------------------- Caminhos -------------------------------
 * O servidor de arquivos só entende caminhos absolutos ("/bin/ola"). Os
 * relativos ("../etc") são resolvidos aqui, a partir da pasta atual.     */

static inline int caminho_absoluto(const char *rel, char *saida)
{
    char tmp[2 * CAMINHO_MAX];
    u32 n = 0;
    if (rel[0] != '/') {
        const char *p = ambiente()->pasta;
        while (*p && n < sizeof(tmp) - 2) tmp[n++] = *p++;
        tmp[n++] = '/';
    }
    while (*rel && n < sizeof(tmp) - 1) tmp[n++] = *rel++;
    tmp[n] = '\0';

    /* Normaliza: tira "//", "." e resolve "..". */
    u32 s = 0;
    for (u32 i = 0; tmp[i]; ) {
        while (tmp[i] == '/') i++;
        if (!tmp[i]) break;
        u32 ini = i;
        while (tmp[i] && tmp[i] != '/') i++;
        u32 tam = i - ini;
        if (tam == 1 && tmp[ini] == '.') continue;
        if (tam == 2 && tmp[ini] == '.' && tmp[ini + 1] == '.') {
            while (s > 0 && saida[s - 1] != '/') s--;
            if (s > 0) s--;
            continue;
        }
        if (s + 1 + tam >= CAMINHO_MAX) return ERRO_INVALIDO;
        saida[s++] = '/';
        for (u32 k = 0; k < tam; k++) saida[s++] = tmp[ini + k];
    }
    if (s == 0) saida[s++] = '/';
    saida[s] = '\0';
    return 0;
}

/* -------------------------------- Arquivos -------------------------------- */

/* Pedido ao servidor de arquivos com um caminho; devolve a resposta em m. */
static inline long arq_pedido(u32 tipo, const char *caminho, msg_t *m)
{
    char abs[CAMINHO_MAX];
    if (caminho_absoluto(caminho, abs) < 0) return ERRO_INVALIDO;
    u32 a0 = m->arg[0];
    msg_zera(m);
    m->tipo   = tipo;
    m->arg[0] = a0;
    str_copia(m->texto, abs, MSG_TEXTO_MAX);
    if (sys_chamar(PID_ARQUIVOS, m) < 0) return ERRO_PID;
    if (m->tipo == MSG_ERRO) return -(long)m->arg[0];
    return 0;
}

/* Tipo (ARQ_TIPO_*) e tamanho; erro < 0 se não existir. */
static inline long arq_info(const char *caminho, u32 *tamanho)
{
    msg_t m;
    m.arg[0] = 0;
    long r = arq_pedido(ARQ_INFO, caminho, &m);
    if (r < 0) return r;
    if (tamanho) *tamanho = m.arg[1];
    return m.arg[0];
}

/* O i-ésimo item de uma pasta; devolve o tipo, ou erro < 0 quando acabar. */
static inline long arq_listar(const char *pasta, u32 i, char *nome, u32 *tamanho)
{
    msg_t m;
    m.arg[0] = i;
    long r = arq_pedido(ARQ_LISTAR, pasta, &m);
    if (r < 0) return r;
    str_copia(nome, m.texto, MSG_TEXTO_MAX);
    if (tamanho) *tamanho = m.arg[1];
    return m.arg[0];
}

static inline long arq_abrir(const char *caminho, u32 modo)
{
    msg_t m;
    m.arg[0] = modo;
    long r = arq_pedido(ARQ_ABRIR, caminho, &m);
    return r < 0 ? r : (long)m.arg[0];
}

static inline void arq_fechar(u32 canal)
{
    msg_t m;
    msg_zera(&m);
    m.tipo   = ARQ_FECHAR;
    m.arg[0] = canal;
    sys_chamar(PID_ARQUIVOS, &m);
}

static inline long arq_pasta(const char *caminho)
{
    msg_t m;
    m.arg[0] = 0;
    return arq_pedido(ARQ_PASTA, caminho, &m);
}

static inline long arq_apagar(const char *caminho)
{
    msg_t m;
    m.arg[0] = 0;
    return arq_pedido(ARQ_APAGAR, caminho, &m);
}

/* Lê o arquivo inteiro para um bloco novo do heap (libere com free). */
static inline u8 *arq_carregar(const char *caminho, u32 *tamanho)
{
    u32 tam;
    if (arq_info(caminho, &tam) != ARQ_TIPO_ARQUIVO) return 0;
    long canal = arq_abrir(caminho, ARQ_MODO_LER);
    if (canal < 0) return 0;
    u8 *buf = malloc(tam ? tam : 1);
    u32 lidos = 0;
    while (buf && lidos < tam) {
        long n = ler_de(PID_ARQUIVOS, (u32)canal, (char *)buf + lidos, tam - lidos);
        if (n <= 0) break;
        lidos += (u32)n;
    }
    arq_fechar((u32)canal);
    if (!buf || lidos != tam) { free(buf); return 0; }
    *tamanho = tam;
    return buf;
}

/* ---------------------------------- Pipes --------------------------------- */

static inline long tubo_pedido(u32 tipo, u32 canal)
{
    msg_t m;
    msg_zera(&m);
    m.tipo   = tipo;
    m.arg[0] = canal;
    if (sys_chamar(PID_TUBOS, &m) < 0) return ERRO_PID;
    if (m.tipo == MSG_ERRO) return -(long)m.arg[0];
    return m.arg[0];
}

static inline long tubo_criar(void)               { return tubo_pedido(TUBO_CRIAR, 0); }
static inline void tubo_fechar_escrita(u32 canal) { tubo_pedido(TUBO_FECHAR_ESCRITA, canal); }
static inline void tubo_liberar(u32 canal)        { tubo_pedido(TUBO_LIBERAR, canal); }

/* ---------------------------- Executar programas --------------------------
 * Lê o ELF do servidor de arquivos e pede ao núcleo um processo novo com
 * ele. entrada/saida = 0: o filho herda as minhas. Devolve o PID.        */

static inline long executar(const char *caminho, char **argv,
                            u32 entrada, u32 entrada_canal, u32 saida, u32 saida_canal)
{
    u32 tam;
    u8 *imagem = arq_carregar(caminho, &tam);
    if (!imagem) return ERRO_NAO_ACHADO;
    criar_t c;
    c.imagem        = (u64)imagem;
    c.tamanho       = tam;
    c.argv          = (u64)argv;
    c.entrada       = entrada;
    c.entrada_canal = entrada_canal;
    c.saida         = saida;
    c.saida_canal   = saida_canal;
    long pid = sys_criar(&c);
    free(imagem);                        /* o núcleo já copiou o programa */
    return pid;
}

/* ------------------------------ Ponto de entrada -------------------------
 * Todo programa define main(argc, argv), como em qualquer C. Quem a chama
 * é _start, em crt0.asm; o valor devolvido vira o código de saída.        */

int main(int argc, char **argv);

#endif
