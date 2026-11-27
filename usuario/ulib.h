/*
 * ===========================================================================
 *  NovaEra - usuario/ulib.h
 *  Biblioteca mínima dos programas do usuário (anel 3).
 *  Um programa NÃO pode tocar no hardware nem na memória do núcleo:
 *  tudo o que ele faz além de calcular passa por int 0x80, quase sempre
 *  como uma mensagem para um servidor (terminal, arquivos, pipes...).
 *  Sem assembly embutido: o que precisa de assembly está em sys.s e crt0.s.
 * ===========================================================================
 */
#ifndef ULIB_H
#define ULIB_H

#include "../novaera.h"

/* ---------------------------- Chamadas de sistema ------------------------ */

/* Em sys.s: executa int 0x80 com RAX = numero, RBX = a, RCX = b. */
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
static inline long sys_linha_boot(msg_t *m)             { return syscall2(SYS_LINHA_BOOT, (u64)m, 0); }
static inline long sys_raiz(const char *nova, msg_t *m)  { return syscall2(SYS_RAIZ, (u64)nova, (u64)m); }
static inline long sys_relogio(msg_t *m)                 { return syscall2(SYS_RELOGIO, (u64)m, 0); }
static inline long sys_mapear_fisico(u64 fisico, u64 bytes) { return syscall2(SYS_MAPEAR_FISICO, fisico, bytes); }


/* Sinais. pid > 0: um processo; pid < 0: o grupo -pid; 0: o meu grupo. */
void __sinal_volta(void);                                /* sys.s: o restaurador */
static inline long sys_sinal(long pid, u32 sinal)       { return syscall2(SYS_SINAL, (u64)pid, sinal); }
static inline long sys_sinal_tratar(u32 sinal, sinal_acao_t *a) { return syscall2(SYS_SINAL_TRATAR, sinal, (u64)a); }
static inline long sys_sinal_mascara(u32 como, u32 m)   { return syscall2(SYS_SINAL_MASCARA, como, m); }
static inline long sys_grupo(u32 pid, long grupo)       { return syscall2(SYS_GRUPO, pid, (u64)grupo); }

/* O nome de cada sinal, sem o "SIG" ("INT", "KILL"...); 0 se não existe. */
static inline const char *sinal_nome(u32 s)
{
    static const char *const nomes[SINAL_MAX] = {
        0, "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "BUS", "FPE", "KILL", "USR1",
        "SEGV", "USR2", "PIPE", "ALRM", "TERM", "STKFLT", "CHLD", "CONT", "STOP", "TSTP",
        "TTIN", "TTOU", "URG", "XCPU", "XFSZ", "VTALRM", "PROF", "WINCH", "IO", "PWR", "SYS"
    };
    return s < SINAL_MAX ? nomes[s] : 0;
}

/* Threads e futex (sys.s tem as operações atômicas) */
int atomico_troca(int *p, int v);
int atomico_cmpxchg(int *p, int esperado, int novo);
int atomico_soma(int *p, int v);
static inline long sys_thread_criar(thread_t *t)           { return syscall2(SYS_THREAD_CRIAR, (u64)t, 0); }
static inline void sys_thread_sair(int codigo)             { syscall2(SYS_THREAD_SAIR, (u64)(long)codigo, 0); for (;;) ; }
static inline long sys_futex_esperar(int *p, int valor)    { return syscall2(SYS_FUTEX_ESPERAR, (u64)p, (u64)(u32)valor); }
static inline long sys_futex_acordar(int *p, int n)        { return syscall2(SYS_FUTEX_ACORDAR, (u64)p, (u64)n); }

/* mmap e memória compartilhada */
static inline long sys_mmap(mmap_t *m)                     { return syscall2(SYS_MMAP, (u64)m, 0); }
static inline long sys_munmap(u64 endereco, u64 bytes)     { return syscall2(SYS_MUNMAP, endereco, bytes); }
static inline long sys_shm_abrir(shm_pedido_t *p)          { return syscall2(SYS_SHM_ABRIR, (u64)p, 0); }
static inline long sys_shm_tamanho(u32 id, u64 bytes)      { return syscall2(SYS_SHM_TAMANHO, id, bytes); }
static inline long sys_shm_apagar(const char *nome)        { return syscall2(SYS_SHM_APAGAR, (u64)nome, 0); }

/* Usuários: quem é um processo (pid 0 = eu); só o root muda. */
static inline long sys_identidade(u32 pid, identidade_t *id)       { return syscall2(SYS_IDENTIDADE, pid, (u64)id); }
static inline long sys_identidade_mudar(u32 pid, identidade_t *id) { return syscall2(SYS_IDENTIDADE_MUDAR, pid, (u64)id); }

static inline int no_grupo(const identidade_t *id, u32 g)
{
    if (id->gid == g) return 1;
    for (u32 i = 0; i < id->n && i < GRUPOS_MAX; i++) if (id->grupos[i] == g) return 1;
    return 0;
}

/* As permissões rwx do Unix: pode 'quer' (PERM_LER | PERM_ESCREVER |
 * PERM_EXECUTAR) num arquivo com esse modo, dono e grupo? O root pode
 * tudo (rodar um programa, só se algum x estiver ligado). */
static inline int permite(const identidade_t *id, u32 modo, u32 dono, u32 grupo, u32 quer)
{
    if (id->uid == UID_ROOT)
        return !(quer & PERM_EXECUTAR) || (modo & 0xF000) == 0x4000 || (modo & 0111);
    u32 bits = id->uid == dono ? (modo >> 6) & 7 : no_grupo(id, grupo) ? (modo >> 3) & 7 : modo & 7;
    return (bits & quer) == quer;
}

/* Instala uma função como tratador do sinal (ou SINAL_PADRAO/SINAL_IGNORAR). */
static inline long sinal_tratar(u32 sinal, void (*f)(int))
{
    sinal_acao_t a;
    a.tratador    = (u64)f;
    a.restaurador = (u64)__sinal_volta;
    a.mascara     = 0;
    a.mudar       = 1;
    return sys_sinal_tratar(sinal, &a);
}

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

void outb(u16 porta, u8 valor);       /* sys.s */
void outw(u16 porta, u16 valor);
u16  inw(u16 porta);
void outl(u16 porta, u32 valor);
u32  inl(u16 porta);
u8   inb(u16 porta);

/* Memória de dispositivo (sys.s). O cproc ainda não suporta "volatile":
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

static inline void mem_set(void *dst, u8 v, u64 n)
{
    u8 *d = dst;
    while (n--) *d++ = v;
}

static inline int mem_igual(const void *a, const void *b, u64 n)
{
    const u8 *x = a, *y = b;
    while (n--) if (*x++ != *y++) return 0;
    return 1;
}

/* O caractere c aparece no texto s? */
static inline int str_tem(const char *s, char c)
{
    for (; *s; s++) if (*s == c) return 1;
    return 0;
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
 * umalloc/ufree simples: blocos com um cabeçalho de 16 bytes numa lista de
 * livres (o primeiro que couber). O heap cresce com SYS_SBRK.            */

typedef struct bloco {
    u64           tam;               /* bytes úteis do bloco */
    struct bloco *prox;              /* próximo livre (só quando livre) */
} bloco_t;

static bloco_t *ulib_livres;

/* (umalloc/ufree, e não malloc/free: os nomes padrão são da libc.) */
static inline void *umalloc(u64 n)
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

static inline void ufree(void *p)
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

/* Escreve n bytes num servidor de saída: no terminal e nos pipes, em
 * pedaços de até 63 bytes; nos arquivos, até ESCRITA_MAX de uma vez (o
 * servidor copia direto da minha memória). */
static inline long escreve_em(u32 servidor, u32 canal, const char *dados, u64 n, u8 cor)
{
    msg_t m;
    u64 total = 0;
    if (SERVIDOR_DE_ARQUIVOS(servidor)) {
        while (total < n) {
            u64 k = n - total > ESCRITA_MAX ? ESCRITA_MAX : n - total;
            msg_zera(&m);
            m.tipo   = MSG_ESCREVER;
            m.arg[0] = canal;
            m.arg[2] = (u32)k;
            m.arg[3] = (u32)(u64)(dados + total);
            long r = sys_chamar(servidor, &m);
            if (r == ERRO_INTERROMPIDO) return total ? (long)total : r;
            if (r < 0) return ERRO_INVALIDO;
            if (m.tipo == MSG_ERRO) {
                if (total) return (long)total;
                return m.arg[0] == (u32)(-ERRO_TUBO) ? ERRO_TUBO : -(long)m.arg[0];
            }
            if (m.arg[0] == 0) return total ? (long)total : ERRO_INVALIDO;
            total += m.arg[0];
        }
        return (long)total;
    }
    while (total < n) {
        msg_zera(&m);
        m.tipo   = MSG_ESCREVER;
        m.arg[0] = canal;
        m.arg[1] = cor;
        u32 i = 0;
        while (total < n && i < MSG_TEXTO_MAX - 1) m.texto[i++] = dados[total++];
        m.arg[2] = i;
        long r = sys_chamar(servidor, &m);
        if (r == ERRO_INTERROMPIDO) return r;
        if (r < 0) return ERRO_INVALIDO;
        if (m.tipo == MSG_ERRO) return m.arg[0] == (u32)(-ERRO_TUBO) ? ERRO_TUBO : ERRO_INVALIDO;
    }
    return (long)total;
}

static inline void escreve(const char *s, u8 cor)
{
    ambiente_t *a = ambiente();
    escreve_em(a->saida, a->saida_canal, s, str_tam(s), cor);
}

/* O terminal deste programa: o canal da entrada ou da saída padrão, se
 * uma delas for o terminal; senão, o console (0). */
static inline u32 terminal_canal(void)
{
    ambiente_t *a = ambiente();
    if (a->entrada == PID_TERMINAL) return a->entrada_canal & ~CANAL_DONO;
    if (a->saida == PID_TERMINAL) return a->saida_canal & ~CANAL_DONO;
    return 0;
}

/* Mensagens de erro vão sempre para o terminal (a "saída de erro"), mesmo
 * quando a saída padrão foi ligada a um pipe ou a um arquivo. */
static inline void escreve_erro(const char *s)
{
    escreve_em(PID_TERMINAL, terminal_canal(), s, str_tam(s), COR(COR_VERMELHO_CLARO));
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
    long r = sys_chamar(servidor, &m);
    if (r < 0) return r == ERRO_INTERROMPIDO ? r : ERRO_PID;
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

/* A raiz do processo (o chroot do "entrar"): "" = a de verdade. */
static char ulib_raiz[CAMINHO_MAX];
static int  ulib_raiz_lida;
static inline const char *raiz_atual(void)
{
    if (!ulib_raiz_lida) {
        msg_t m;
        if (sys_raiz(0, &m) == 0) str_copia(ulib_raiz, m.texto, CAMINHO_MAX);
        ulib_raiz_lida = 1;
    }
    return ulib_raiz;
}

/* Pedido com um caminho; devolve a resposta em m (em m->origem fica quem
 * respondeu). Todo caminho começa no servidor de arquivos, que é a raiz;
 * se ele cair num disco montado, a resposta diz quem cuida dele
 * (ARQ_OUTRO_SERVIDOR) e o pedido vai de novo, para lá. */
static inline long arq_pedido(u32 tipo, const char *caminho, msg_t *m)
{
    char abs[CAMINHO_MAX];
    if (caminho_absoluto(caminho, abs) < 0) return ERRO_INVALIDO;
    const char *raiz = raiz_atual();
    if (raiz[0]) {                               /* dentro do "entrar": /etc é <raiz>/etc */
        char rel[CAMINHO_MAX];
        str_copia(rel, abs, sizeof rel);
        u32 nr = str_tam(raiz), na = str_tam(rel);
        if (nr + na >= CAMINHO_MAX) return ERRO_INVALIDO;
        str_copia(abs, raiz, sizeof abs);
        if (!(rel[0] == '/' && rel[1] == '\0')) str_copia(abs + nr, rel, sizeof abs - nr);
    }
    u32 a0 = m->arg[0], srv = PID_ARQUIVOS;
    u32 a1 = (tipo == ARQ_ABRIR || tipo == ARQ_DONO) ? m->arg[1] : 0;
    for (int tentativa = 0; ; tentativa++) {
        msg_zera(m);
        m->tipo   = tipo;
        m->arg[0] = a0;
        m->arg[1] = a1;
        str_copia(m->texto, abs, MSG_TEXTO_MAX);
        if (sys_chamar(srv, m) < 0) return ERRO_PID;
        if (m->tipo != ARQ_OUTRO_SERVIDOR) break;
        if (tentativa || m->arg[0] == srv) return ERRO_NAO_ACHADO;   /* um só desvio */
        srv = m->arg[0];
    }
    m->origem = srv;
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

/* Tudo o que o servidor sabe de um arquivo (o "stat"). */
typedef struct {
    u32 tipo, tam;
    u32 modo;                /* tipo (0x4000 pasta, 0x8000 arquivo) | permissões */
    u32 dono, grupo;
} arq_estado_t;

static inline void arq_estado_de(const msg_t *m, arq_estado_t *e)
{
    e->tipo  = m->arg[0];
    e->tam   = m->arg[1];
    e->modo  = m->arg[2];
    e->dono  = m->arg[3] & 0xFFFF;
    e->grupo = m->arg[3] >> 16;
}

static inline long arq_estado(const char *caminho, arq_estado_t *e)
{
    msg_t m;
    m.arg[0] = 0;
    long r = arq_pedido(ARQ_INFO, caminho, &m);
    if (r < 0) return r;
    arq_estado_de(&m, e);
    return 0;
}

/* Como arq_listar, com o modo, o dono e o grupo (para o ls -l). */
static inline long arq_listar_estado(const char *pasta, u32 i, char *nome, arq_estado_t *e)
{
    msg_t m;
    m.arg[0] = i;
    long r = arq_pedido(ARQ_LISTAR, pasta, &m);
    if (r < 0) return r;
    str_copia(nome, m.texto, MSG_TEXTO_MAX);
    arq_estado_de(&m, e);
    return 0;
}

/* chmod e chown */
static inline long arq_permissoes(const char *caminho, u32 modo)
{
    msg_t m;
    m.arg[0] = modo;
    return arq_pedido(ARQ_PERMISSOES, caminho, &m);
}

static inline long arq_dono(const char *caminho, u32 uid, u32 gid)
{
    msg_t m;
    m.arg[0] = uid;
    m.arg[1] = gid;
    return arq_pedido(ARQ_DONO, caminho, &m);
}

/* O terminal mostra (1) ou esconde (0) o que se digita: para senhas. */
static inline void terminal_eco(int ligado)
{
    msg_t m;
    msg_zera(&m);
    m.tipo   = TERM_ECO;
    m.arg[0] = (u32)ligado;
    m.arg[1] = terminal_canal();
    sys_chamar(PID_TERMINAL, &m);
}

/* O canal devolvido leva o servidor nos bits 8-15: ARQ_SERVIDOR(canal)
 * diz com quem falar para ler, escrever e fechar. */
/* 'permissoes': as do arquivo, se ele for criado (0 = 0644). */
static inline long arq_abrir_com(const char *caminho, u32 modo, u32 permissoes)
{
    msg_t m;
    m.arg[0] = modo;
    m.arg[1] = permissoes;
    long r = arq_pedido(ARQ_ABRIR, caminho, &m);
    return r < 0 ? r : (long)(m.origem << 8 | ARQ_CANAL(m.arg[0]));
}

static inline long arq_abrir(const char *caminho, u32 modo)
{
    msg_t m;
    m.arg[0] = modo;
    m.arg[1] = 0;
    long r = arq_pedido(ARQ_ABRIR, caminho, &m);
    return r < 0 ? r : (long)(m.origem << 8 | ARQ_CANAL(m.arg[0]));
}

static inline void arq_fechar(u32 canal)
{
    msg_t m;
    msg_zera(&m);
    m.tipo   = ARQ_FECHAR;
    m.arg[0] = ARQ_CANAL(canal);
    sys_chamar(ARQ_SERVIDOR(canal), &m);
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

/* Lê o arquivo inteiro para um bloco novo do heap (libere com ufree). */
static inline u8 *arq_carregar(const char *caminho, u32 *tamanho)
{
    u32 tam;
    if (arq_info(caminho, &tam) != ARQ_TIPO_ARQUIVO) return 0;
    long canal = arq_abrir(caminho, ARQ_MODO_LER);
    if (canal < 0) return 0;
    u8 *buf = umalloc(tam ? tam : 1);
    u32 lidos = 0;
    while (buf && lidos < tam) {
        long n = ler_de(ARQ_SERVIDOR(canal), (u32)canal, (char *)buf + lidos, tam - lidos);
        if (n <= 0) break;
        lidos += (u32)n;
    }
    arq_fechar((u32)canal);
    if (!buf || lidos != tam) { ufree(buf); return 0; }
    *tamanho = tam;
    return buf;
}

/* ---------------------------------- Pipes --------------------------------- */

static inline long tubo_pedido(u32 tipo, u32 canal, u32 ponta)
{
    msg_t m;
    msg_zera(&m);
    m.tipo   = tipo;
    m.arg[0] = canal;
    m.arg[1] = ponta;
    if (sys_chamar(PID_TUBOS, &m) < 0) return ERRO_PID;
    if (m.tipo == MSG_ERRO) return -(long)m.arg[0];
    return m.arg[0];
}

static inline long tubo_criar(void)                  { return tubo_pedido(TUBO_CRIAR, 0, 0); }
static inline long tubo_abrir(u32 canal, u32 ponta)  { return tubo_pedido(TUBO_ABRIR, canal, ponta); }
static inline void tubo_fechar(u32 canal, u32 ponta) { tubo_pedido(TUBO_FECHAR, canal, ponta); }

/* ---------------------------- Executar programas --------------------------
 * Lê o ELF do servidor de arquivos e pede ao núcleo um processo novo com
 * ele. entrada/saida = 0: o filho herda as minhas. Devolve o PID.        */

/* Como executar(), escolhendo o grupo do filho (0 = o meu; GRUPO_NOVO =
 * um grupo novo, com o número do filho; ou um grupo que já existe). */
static inline long executar_grupo(const char *caminho, char **argv, u32 entrada, u32 entrada_canal,
                                  u32 saida, u32 saida_canal, u32 grupo)
{
    /* rodar um programa: precisa do x (o núcleo só vê os bytes) */
    arq_estado_t e;
    identidade_t eu;
    if (arq_estado(caminho, &e) < 0) return ERRO_NAO_ACHADO;
    if (e.modo && sys_identidade(0, &eu) == 0 && !permite(&eu, e.modo, e.dono, e.grupo, PERM_EXECUTAR))
        return ERRO_SEM_PERMISSAO;
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
    c.grupo         = grupo;
    c.reservado     = 0;
    long pid = sys_criar(&c);
    ufree(imagem);                       /* o núcleo já copiou o programa */
    return pid;
}

static inline long executar(const char *caminho, char **argv,
                            u32 entrada, u32 entrada_canal, u32 saida, u32 saida_canal)
{
    return executar_grupo(caminho, argv, entrada, entrada_canal, saida, saida_canal, 0);
}

/* -------------------------- Discos e montagens ---------------------------
 * Os nomes dos dispositivos são os do Linux: sda (o disco inteiro), sda1
 * a sda4 (as partições), sdb, sdc, sdd. "/dev/sda1" também vale. */
static inline long dispositivo_de(const char *nome)
{
    const char *p = nome;
    if (p[0] == '/' && p[1] == 'd' && p[2] == 'e' && p[3] == 'v' && p[4] == '/') p += 5;
    if (p[0] != 's' || p[1] != 'd' || p[2] < 'a' || p[2] >= 'a' + (int)DISCOS_MAX) return ERRO_INVALIDO;
    u32 d = (u32)(p[2] - 'a'), part = 0;
    if (p[3]) {
        if (p[3] < '1' || p[3] > '4' || p[4]) return ERRO_INVALIDO;
        part = (u32)(p[3] - '0');
    }
    return DISCO_DEV(d, part);
}

/* "sda1" (buf com pelo menos 5 bytes). */
static inline char *dispositivo_nome(u32 dev, char *buf)
{
    buf[0] = 's'; buf[1] = 'd'; buf[2] = (char)('a' + DEV_DISCO(dev));
    u32 i = 3;
    if (DEV_PARTICAO(dev)) buf[i++] = (char)('0' + DEV_PARTICAO(dev));
    buf[i] = '\0';
    return buf;
}

/* Setores do dispositivo (0 = não existe); m recebe o resto da resposta
 * (arg[1] = início no disco, arg[2] = tipo da partição, texto = modelo). */
static inline u32 disco_info(u32 dev, msg_t *m)
{
    msg_zera(m);
    m->tipo   = DISCO_INFO;
    m->arg[3] = dev;
    if (sys_chamar(PID_DISCO, m) < 0 || m->tipo != MSG_OK) return 0;
    return m->arg[0];
}

/* A montagem número i: 0 = existe, < 0 = acabou a lista. */
static inline long montagem(u32 i, char *pasta, u32 *servidor, u32 *dev)
{
    msg_t m;
    msg_zera(&m);
    m.tipo   = MONT_LISTAR;
    m.arg[0] = i;
    if (sys_chamar(PID_ARQUIVOS, &m) < 0 || m.tipo != MSG_OK) return ERRO_NAO_ACHADO;
    if (pasta) str_copia(pasta, m.texto, CAMINHO_MAX);
    if (servidor) *servidor = m.arg[0];
    if (dev) *dev = m.arg[1];
    return 0;
}

/* Onde o dispositivo está montado (0 = em nenhum lugar). */
static inline int montado_em(u32 dev, char *pasta)
{
    u32 s, d;
    for (u32 i = 0; montagem(i, pasta, &s, &d) == 0; i++)
        if (d == dev) return 1;
    return 0;
}

/* O nome do sistema de arquivos de cada servidor. */
static inline const char *servidor_tipo(u32 servidor)
{
    return servidor == PID_FAT ? "fat32" : servidor == PID_EXT2 ? "ext2" : "?";
}

/* Monta e desmonta: o servidor do sistema de arquivos e a tabela. Cada
 * servidor é perguntado; o que não reconhece o disco diz ERRO_FORMATO. */
static inline long montar_em(u32 dev, const char *pasta)
{
    static const u32 servidores[] = { PID_FAT, PID_EXT2 };
    msg_t m;
    long r = ERRO_FORMATO;
    u32 srv = 0;
    for (u32 i = 0; i < sizeof(servidores) / sizeof(servidores[0]); i++) {
        srv = servidores[i];
        msg_zera(&m);
        m.tipo   = SA_MONTAR;
        m.arg[0] = dev;
        str_copia(m.texto, pasta, MSG_TEXTO_MAX);
        if (sys_chamar(srv, &m) < 0) { r = ERRO_PID; continue; }
        r = m.tipo == MSG_ERRO ? -(long)m.arg[0] : 0;
        if (r != ERRO_FORMATO) break;
    }
    if (r < 0) return r;
    msg_zera(&m);
    m.tipo   = MONT_ADICIONAR;
    m.arg[0] = srv;
    m.arg[1] = dev;
    str_copia(m.texto, pasta, MSG_TEXTO_MAX);
    if (sys_chamar(PID_ARQUIVOS, &m) < 0 || m.tipo == MSG_ERRO) {
        long e = m.tipo == MSG_ERRO ? -(long)m.arg[0] : ERRO_PID;
        msg_zera(&m);
        m.tipo = SA_DESMONTAR;                     /* desfaz */
        str_copia(m.texto, pasta, MSG_TEXTO_MAX);
        sys_chamar(srv, &m);
        return e;
    }
    return 0;
}

static inline long desmontar_de(u32 servidor, const char *pasta)
{
    msg_t m;
    msg_zera(&m);
    m.tipo = SA_DESMONTAR;
    str_copia(m.texto, pasta, MSG_TEXTO_MAX);
    if (sys_chamar(servidor, &m) < 0) return ERRO_PID;
    if (m.tipo == MSG_ERRO && m.arg[0] != (u32)(-ERRO_NAO_ACHADO)) return -(long)m.arg[0];
    msg_zera(&m);
    m.tipo = MONT_REMOVER;
    str_copia(m.texto, pasta, MSG_TEXTO_MAX);
    if (sys_chamar(PID_ARQUIVOS, &m) < 0) return ERRO_PID;
    return m.tipo == MSG_ERRO ? -(long)m.arg[0] : 0;
}

/* Antes de desligar: grava tudo nos discos e os desmonta (a raiz por
 * último: o servidor dela grava e marca "desmontado direito"). */
static inline void discos_sincronizar(void)
{
    char pasta[CAMINHO_MAX], raiz_pasta[CAMINHO_MAX];
    u32 srv, dev, srv_raiz = 0;
    for (u32 i = 0; montagem(i, pasta, &srv, &dev) == 0; ) {
        if (str_igual(pasta, "/")) { srv_raiz = srv; i++; continue; }
        if (desmontar_de(srv, pasta) < 0) i++;     /* em uso: fica, e vai o próximo */
    }
    if (srv_raiz) {
        msg_t m;
        msg_zera(&m);
        m.tipo = SA_DESMONTAR;
        str_copia(raiz_pasta, "/", sizeof raiz_pasta);
        str_copia(m.texto, raiz_pasta, MSG_TEXTO_MAX);
        sys_chamar(srv_raiz, &m);
    }
}

/* ------------------------------ Ponto de entrada -------------------------
 * Todo programa define main(argc, argv), como em qualquer C. Quem a chama
 * é _start, em crt0.s; o valor devolvido vira o código de saída.        */

int main(int argc, char **argv);

#endif
