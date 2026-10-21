/*
 * ===========================================================================
 *  NEFS - ferramentas/nefs-as.c
 *  O montador (assembler) do NEFS: lê assembly x86-64 na sintaxe AT&T (a
 *  que o QBE escreve) e grava um objeto ELF64 relocável (.o).
 *
 *    nefs-as [-o saída.o] entrada.s
 *
 *  Entende o subconjunto de instruções que o QBE gera, mais as que o núcleo
 *  do NovaEra precisa (modo de 32 bits do boot, registradores de controle,
 *  portas de E/S...). O objetivo é gerar os MESMOS bytes que o GNU as: as
 *  escolhas de codificação seguem as dele, e isso é testado.
 *
 *  Como funciona, em quatro passos:
 *    1. cada linha vira bytes (instruções e dados) em "pedaços" da seção
 *       atual; um salto para um rótulo vira um pedaço próprio, porque o
 *       tamanho dele (2 ou 5/6 bytes) depende da distância
 *    2. disposição: calcula o endereço de cada pedaço; todo salto começa
 *       curto e cresce se o destino ficar longe, até nada mais mudar
 *    3. correções: cada referência a um símbolo vira um número (se o
 *       destino já é conhecido) ou uma relocação, para o ligador resolver
 *    4. grava o ELF: seções, relocações, tabela de símbolos
 *
 *  Escrito em C padrão: compila no Linux e dentro do próprio NovaEra.
 * ===========================================================================
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t  i64;

/* ------------------------------ Erros ----------------------------------- */

static const char *arquivo = "?";
static int num_linha;

static void erro(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:%d: erro: ", arquivo, num_linha);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static void *xmalloc(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    if (!p) { fprintf(stderr, "nefs-as: sem memória\n"); exit(1); }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) { fprintf(stderr, "nefs-as: sem memória\n"); exit(1); }
    return p;
}

static char *xstrdup(const char *s)
{
    char *d = xmalloc(strlen(s) + 1);
    strcpy(d, s);
    return d;
}

/* Bytes que crescem conforme a necessidade. */
typedef struct {
    u8    *d;
    size_t n, cap;
} buf_t;

static void buf_poe(buf_t *b, const void *p, size_t n)
{
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 64;
        b->d = xrealloc(b->d, b->cap);
    }
    memcpy(b->d + b->n, p, n);
    b->n += n;
}

static void buf_byte(buf_t *b, u8 v) { buf_poe(b, &v, 1); }

static void buf_le(buf_t *b, u64 v, int tam)
{
    for (int i = 0; i < tam; i++) buf_byte(b, (u8)(v >> (8 * i)));
}

/* ------------------------- Seções e pedaços ----------------------------- */

/* Constantes do ELF que o montador usa. */
#define SHT_PROGBITS  1u
#define SHT_SYMTAB    2u
#define SHT_STRTAB    3u
#define SHT_RELA      4u
#define SHT_NOTE      7u
#define SHT_NOBITS    8u
#define SHF_WRITE     0x1u
#define SHF_ALLOC     0x2u
#define SHF_EXEC      0x4u
#define SHF_MERGE     0x10u
#define SHF_STRINGS   0x20u
#define SHF_INFO_LINK 0x40u

#define R_X86_64_64            1
#define R_X86_64_PC32          2
#define R_X86_64_PLT32         4
#define R_X86_64_GOTPCREL      9
#define R_X86_64_32           10
#define R_X86_64_32S          11
#define R_X86_64_16           12
#define R_X86_64_8            14
#define R_X86_64_GOTPCRELX    41
#define R_X86_64_REX_GOTPCRELX 42

enum { P_DADOS, P_SALTO, P_ALINHA };

typedef struct secao secao_t;
typedef struct simbolo simbolo_t;

/* Uma expressão já lida: simbolo - menos + c (cada parte é opcional). */
enum { MOD_NADA, MOD_PLT, MOD_GOTPCREL };
typedef struct {
    simbolo_t *s, *menos;
    i64        c;
    int        mod;
} expr_t;

typedef struct pedaco {
    int     tipo;
    buf_t   b;              /* P_DADOS: os bytes */
    u64     espaco;         /* P_DADOS em seção sem bytes (.bss): tamanho */
    /* P_SALTO */
    int     cond;           /* -1 = jmp; 0..15 = jcc */
    expr_t  alvo;
    int     longo;
    /* P_ALINHA */
    u32     alinh;
    u8      preenche;
    int     nops;           /* em código: preenche com NOPs */
    u64     ender;          /* endereço (dentro da seção), após a disposição */
    int     passada;        /* a última passada da disposição que o viu */
    int     regiao;         /* muda a cada alinhamento */
    u64     tam;
    struct pedaco *prox;
} pedaco_t;

struct secao {
    char     *nome;
    u32       tipo;
    u64       flags;
    u32       alinh;
    pedaco_t *prim, *ult;
    u64       tam;
    buf_t     final;         /* os bytes depois da disposição */
    u32       indice;        /* no ELF */
    u32       sym_secao;     /* o símbolo da seção, no ELF */
    struct correcao *corr;   /* as referências a símbolos nesta seção */
    u32       nrel;
    buf_t     rel;
    secao_t  *prox;
};

static secao_t *secoes, *secoes_ult, *atual;
static int      modo32;    /* .code32: codifica para o modo de 32 bits */

/* ------------------------------ Símbolos -------------------------------- */

#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2
#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3

struct simbolo {
    char      *nome;
    int        definido;
    secao_t   *sec;          /* 0 com definido = absoluto (.set) */
    pedaco_t  *ped;
    u64        ofs;          /* dentro do pedaço */
    i64        valor;        /* absoluto */
    int        ligacao;      /* STB_LOCAL / GLOBAL / WEAK */
    int        tipo;
    int        usado;
    int        comum;        /* .comm */
    u64        tam_comum, alinh_comum;
    expr_t     tam_expr;     /* .size */
    int        tem_tam;
    u32        indice;       /* no ELF */
    simbolo_t *prox;
};

#define NTAB 1024u
static simbolo_t *tabela[NTAB];
static int        anonimos;   /* rótulos criados para "." */

/* A ordem de criação, para a tabela de símbolos sair sempre igual. */
typedef struct no_simb { simbolo_t *s; struct no_simb *prox; } no_simb_t;
static no_simb_t *ordem, *ordem_ult;

static u32 hash(const char *s)
{
    u32 h = 2166136261u;
    while (*s) h = (h ^ (u8)*s++) * 16777619u;
    return h % NTAB;
}

static simbolo_t *simbolo_registra(const char *nome)
{
    u32 h = hash(nome);
    for (simbolo_t *s = tabela[h]; s; s = s->prox)
        if (strcmp(s->nome, nome) == 0) return s;
    simbolo_t *s = xmalloc(sizeof *s);
    s->nome = xstrdup(nome);
    s->prox = tabela[h];
    tabela[h] = s;
    no_simb_t *n = xmalloc(sizeof *n);
    n->s = s;
    if (ordem_ult) ordem_ult->prox = n; else ordem = n;
    ordem_ult = n;
    return s;
}

static int eh_local_L(const simbolo_t *s)
{
    return s->nome[0] == '.' && s->nome[1] == 'L';
}

/* O pedaço de dados onde os próximos bytes entram (cria se preciso). */
static pedaco_t *pedaco_novo(int tipo)
{
    pedaco_t *p = xmalloc(sizeof *p);
    p->tipo = tipo;
    if (atual->ult) atual->ult->prox = p; else atual->prim = p;
    atual->ult = p;
    return p;
}

static pedaco_t *dados_atual(void)
{
    if (atual->ult && atual->ult->tipo == P_DADOS) return atual->ult;
    return pedaco_novo(P_DADOS);
}

static u64 pos_atual(pedaco_t **ped)
{
    pedaco_t *p = dados_atual();
    *ped = p;
    return atual->tipo == SHT_NOBITS ? p->espaco : p->b.n;
}

static void define_rotulo(const char *nome)
{
    simbolo_t *s = simbolo_registra(nome);
    if (s->definido) erro("o rótulo '%s' foi definido duas vezes", nome);
    s->definido = 1;
    s->sec = atual;
    s->ofs = pos_atual(&s->ped);
}

/* --------------------------- Correções ---------------------------------- */

/* Uma referência a um símbolo dentro dos bytes: resolvida no fim. */
typedef struct correcao {
    pedaco_t *ped;
    u64       ofs;           /* do campo, dentro do pedaço */
    int       tam;           /* 1, 2, 4 ou 8 bytes */
    expr_t    e;
    int       pcrel;         /* relativo à posição (RIP) */
    i64       ajuste;        /* pcrel: subtrai o resto da instrução */
    int       assinado;      /* para conferir o alcance e escolher 32/32S */
    int       tipo_got;      /* relocação GOT escolhida pela instrução */
    int       chamada;       /* call/jmp: relocação PLT32 */
    int       salto;         /* jmp/jcc: resolve até símbolos globais */
    int       linha;
    struct correcao *prox;
} correcao_t;

static void correcao_nova(pedaco_t *ped, u64 ofs, int tam, expr_t e, int pcrel,
                          i64 ajuste, int assinado, int tipo_got, int chamada)
{
    correcao_t *c = xmalloc(sizeof *c);
    c->ped = ped; c->ofs = ofs; c->tam = tam; c->e = e; c->pcrel = pcrel;
    c->ajuste = ajuste; c->assinado = assinado; c->tipo_got = tipo_got;
    c->chamada = chamada; c->linha = num_linha;
    c->prox = atual->corr;
    atual->corr = c;
}

/* ------------------------- Leitura do texto ----------------------------- */

static const char *p;   /* onde a leitura está, na instrução atual */

static void pula_espacos(void)
{
    while (*p == ' ' || *p == '\t') p++;
}

static int eh_letra(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.' || c == '$';
}

static int eh_digito(int c) { return c >= '0' && c <= '9'; }

/* Lê um nome (símbolo, diretiva ou instrução). */
static int le_nome(char *dest, int max)
{
    int n = 0;
    if (*p == '"') {                       /* "nome entre aspas" */
        p++;
        while (*p && *p != '"') {
            if (n < max - 1) dest[n++] = *p;
            p++;
        }
        if (*p != '"') erro("nome sem aspas de fechamento");
        p++;
        dest[n] = '\0';
        return n;
    }
    if (!eh_letra(*p) || *p == '$') return 0;
    while (eh_letra(*p) || eh_digito(*p)) {
        if (n < max - 1) dest[n++] = *p;
        p++;
    }
    dest[n] = '\0';
    return n;
}

static int espera(char c)
{
    pula_espacos();
    if (*p != c) return 0;
    p++;
    return 1;
}

/* ---------------------------- Expressões -------------------------------- */

static expr_t expr_cte(i64 v)
{
    expr_t e;
    memset(&e, 0, sizeof e);
    e.c = v;
    return e;
}

static int expr_eh_cte(const expr_t *e) { return !e->s && !e->menos && e->mod == MOD_NADA; }

/* Um símbolo absoluto (.set) já conhecido vira número. */
static void expr_simplifica(expr_t *e)
{
    if (e->s && e->s->definido && !e->s->sec && !e->s->comum) {
        e->c += e->s->valor;
        e->s = 0;
    }
    if (e->menos && e->menos->definido && !e->menos->sec && !e->menos->comum) {
        e->c -= e->menos->valor;
        e->menos = 0;
    }
}

static expr_t le_expr(void);

static i64 le_numero(void)
{
    i64 v = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        int n = 0;
        for (;; p++, n++) {
            int c = *p, d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            v = (i64)((u64)v * 16 + (u64)d);
        }
        if (!n) erro("número hexadecimal sem dígitos");
    } else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) {
        p += 2;
        while (*p == '0' || *p == '1') v = (i64)((u64)v * 2 + (u64)(*p++ - '0'));
    } else if (p[0] == '0') {
        while (*p >= '0' && *p <= '7') v = (i64)((u64)v * 8 + (u64)(*p++ - '0'));
    } else {
        while (eh_digito(*p)) v = (i64)((u64)v * 10 + (u64)(*p++ - '0'));
    }
    return v;
}

static expr_t le_primario(void)
{
    pula_espacos();
    if (*p == '(') {
        p++;
        expr_t e = le_expr();
        if (!espera(')')) erro("falta ')'");
        return e;
    }
    if (*p == '-') { p++; expr_t e = le_primario(); if (!expr_eh_cte(&e)) erro("'-' só vale para números"); e.c = -e.c; return e; }
    if (*p == '+') { p++; return le_primario(); }
    if (*p == '~') { p++; expr_t e = le_primario(); if (!expr_eh_cte(&e)) erro("'~' só vale para números"); e.c = ~e.c; return e; }
    if (eh_digito(*p)) return expr_cte(le_numero());
    if (*p == '\'') {
        p++;
        i64 v = (u8)*p++;
        if (*p == '\'') p++;
        return expr_cte(v);
    }
    if (*p == '.' && !eh_letra(p[1]) && !eh_digito(p[1])) {
        /* "." = a posição atual: um rótulo sem nome criado aqui */
        p++;
        char nome[32];
        snprintf(nome, sizeof nome, ".L.ponto.%d", anonimos++);
        define_rotulo(nome);
        expr_t e = expr_cte(0);
        e.s = simbolo_registra(nome);
        return e;
    }
    char nome[256];
    if (le_nome(nome, sizeof nome)) {
        expr_t e = expr_cte(0);
        e.s = simbolo_registra(nome);
        e.s->usado = 1;
        if (*p == '@') {
            p++;
            char m[32];
            le_nome(m, sizeof m);
            if (strcmp(m, "plt") == 0 || strcmp(m, "PLT") == 0) e.mod = MOD_PLT;
            else if (strcmp(m, "gotpcrel") == 0 || strcmp(m, "GOTPCREL") == 0) {
                e.mod = MOD_GOTPCREL;
                /* como o GNU as: quem usa a GOT cita a tabela */
                simbolo_registra("_GLOBAL_OFFSET_TABLE_")->usado = 1;
            }
            else erro("modificador '@%s' não suportado", m);
        }
        expr_simplifica(&e);
        return e;
    }
    erro("expressão inválida perto de '%s'", p);
    return expr_cte(0);
}

static expr_t le_mult(void)
{
    expr_t a = le_primario();
    for (;;) {
        pula_espacos();
        int op = 0;
        if (*p == '*') op = '*';
        else if (*p == '/') op = '/';
        else if (*p == '%') op = '%';
        else if (p[0] == '<' && p[1] == '<') op = 'l';
        else if (p[0] == '>' && p[1] == '>') op = 'r';
        if (!op) return a;
        p += (op == 'l' || op == 'r') ? 2 : 1;
        expr_t b = le_primario();
        if (!expr_eh_cte(&a) || !expr_eh_cte(&b)) erro("operação só vale para números");
        if ((op == '/' || op == '%') && b.c == 0) erro("divisão por zero");
        if (op == '*') a.c *= b.c;
        else if (op == '/') a.c /= b.c;
        else if (op == '%') a.c %= b.c;
        else if (op == 'l') a.c = (i64)((u64)a.c << b.c);
        else a.c >>= b.c;
    }
}

static expr_t le_soma(void)
{
    expr_t a = le_mult();
    for (;;) {
        pula_espacos();
        if (*p != '+' && *p != '-') return a;
        int op = *p++;
        expr_t b = le_mult();
        if (op == '+') {
            if (b.s && a.s) erro("soma de dois símbolos");
            if (b.menos) erro("expressão complicada demais");
            if (b.s) { a.s = b.s; a.mod = b.mod; }
            a.c += b.c;
        } else {
            if (b.menos || b.mod) erro("expressão complicada demais");
            if (b.s) {
                if (a.menos) erro("expressão complicada demais");
                if (a.s == b.s) a.s = 0;        /* x - x = 0 */
                else a.menos = b.s;
            }
            a.c -= b.c;
        }
    }
}

static expr_t le_bits(int nivel)
{
    /* nível 0: |   1: ^   2: & */
    static const char ops[] = "|^&";
    expr_t a = nivel < 2 ? le_bits(nivel + 1) : le_soma();
    for (;;) {
        pula_espacos();
        if (*p != ops[nivel] || p[1] == ops[nivel]) return a;
        p++;
        expr_t b = nivel < 2 ? le_bits(nivel + 1) : le_soma();
        if (!expr_eh_cte(&a) || !expr_eh_cte(&b)) erro("operação só vale para números");
        if (nivel == 0) a.c |= b.c; else if (nivel == 1) a.c ^= b.c; else a.c &= b.c;
    }
}

static expr_t le_expr(void) { return le_bits(0); }

/* ---------------------------- Registradores ----------------------------- */

enum { R_GPR, R_XMM, R_CR, R_SEG, R_RIP };

typedef struct {
    const char *nome;
    u8 num, tam, classe;
    u8 rex;      /* spl, bpl, sil, dil: exigem um prefixo REX */
    u8 alto;     /* ah, ch, dh, bh: proibidos junto com REX */
} reg_info_t;

static const reg_info_t regs[] = {
    {"rax",0,8,R_GPR,0,0},{"rcx",1,8,R_GPR,0,0},{"rdx",2,8,R_GPR,0,0},{"rbx",3,8,R_GPR,0,0},
    {"rsp",4,8,R_GPR,0,0},{"rbp",5,8,R_GPR,0,0},{"rsi",6,8,R_GPR,0,0},{"rdi",7,8,R_GPR,0,0},
    {"r8",8,8,R_GPR,0,0},{"r9",9,8,R_GPR,0,0},{"r10",10,8,R_GPR,0,0},{"r11",11,8,R_GPR,0,0},
    {"r12",12,8,R_GPR,0,0},{"r13",13,8,R_GPR,0,0},{"r14",14,8,R_GPR,0,0},{"r15",15,8,R_GPR,0,0},
    {"eax",0,4,R_GPR,0,0},{"ecx",1,4,R_GPR,0,0},{"edx",2,4,R_GPR,0,0},{"ebx",3,4,R_GPR,0,0},
    {"esp",4,4,R_GPR,0,0},{"ebp",5,4,R_GPR,0,0},{"esi",6,4,R_GPR,0,0},{"edi",7,4,R_GPR,0,0},
    {"r8d",8,4,R_GPR,0,0},{"r9d",9,4,R_GPR,0,0},{"r10d",10,4,R_GPR,0,0},{"r11d",11,4,R_GPR,0,0},
    {"r12d",12,4,R_GPR,0,0},{"r13d",13,4,R_GPR,0,0},{"r14d",14,4,R_GPR,0,0},{"r15d",15,4,R_GPR,0,0},
    {"ax",0,2,R_GPR,0,0},{"cx",1,2,R_GPR,0,0},{"dx",2,2,R_GPR,0,0},{"bx",3,2,R_GPR,0,0},
    {"sp",4,2,R_GPR,0,0},{"bp",5,2,R_GPR,0,0},{"si",6,2,R_GPR,0,0},{"di",7,2,R_GPR,0,0},
    {"r8w",8,2,R_GPR,0,0},{"r9w",9,2,R_GPR,0,0},{"r10w",10,2,R_GPR,0,0},{"r11w",11,2,R_GPR,0,0},
    {"r12w",12,2,R_GPR,0,0},{"r13w",13,2,R_GPR,0,0},{"r14w",14,2,R_GPR,0,0},{"r15w",15,2,R_GPR,0,0},
    {"al",0,1,R_GPR,0,0},{"cl",1,1,R_GPR,0,0},{"dl",2,1,R_GPR,0,0},{"bl",3,1,R_GPR,0,0},
    {"ah",4,1,R_GPR,0,1},{"ch",5,1,R_GPR,0,1},{"dh",6,1,R_GPR,0,1},{"bh",7,1,R_GPR,0,1},
    {"spl",4,1,R_GPR,1,0},{"bpl",5,1,R_GPR,1,0},{"sil",6,1,R_GPR,1,0},{"dil",7,1,R_GPR,1,0},
    {"r8b",8,1,R_GPR,0,0},{"r9b",9,1,R_GPR,0,0},{"r10b",10,1,R_GPR,0,0},{"r11b",11,1,R_GPR,0,0},
    {"r12b",12,1,R_GPR,0,0},{"r13b",13,1,R_GPR,0,0},{"r14b",14,1,R_GPR,0,0},{"r15b",15,1,R_GPR,0,0},
    {"cr0",0,8,R_CR,0,0},{"cr2",2,8,R_CR,0,0},{"cr3",3,8,R_CR,0,0},{"cr4",4,8,R_CR,0,0},
    {"es",0,2,R_SEG,0,0},{"cs",1,2,R_SEG,0,0},{"ss",2,2,R_SEG,0,0},{"ds",3,2,R_SEG,0,0},
    {"fs",4,2,R_SEG,0,0},{"gs",5,2,R_SEG,0,0},
    {"rip",0,8,R_RIP,0,0},
};

static const reg_info_t *reg_procura(const char *nome)
{
    for (size_t i = 0; i < sizeof regs / sizeof regs[0]; i++)
        if (strcmp(regs[i].nome, nome) == 0) return &regs[i];
    if (strncmp(nome, "xmm", 3) == 0 && eh_digito(nome[3])) {
        static reg_info_t x[16];
        int n = atoi(nome + 3);
        if (n < 0 || n > 15) return 0;
        x[n].nome = "xmm"; x[n].num = (u8)n; x[n].tam = 16; x[n].classe = R_XMM;
        return &x[n];
    }
    return 0;
}

/* ------------------------------ Operandos ------------------------------- */

enum { O_REG, O_IMED, O_MEM };

typedef struct {
    int tipo;
    int indireto;              /* "*" antes do operando (call/jmp) */
    const reg_info_t *r;       /* O_REG */
    expr_t e;                  /* O_IMED; O_MEM: o deslocamento */
    int    tem_e;
    int    base, indice, escala, rip;   /* O_MEM: -1 = sem */
    int    tam_ender;          /* 4 ou 8: registradores do endereço */
} operando_t;

static const reg_info_t *le_registrador(void)
{
    if (*p != '%') erro("esperava um registrador");
    p++;
    char nome[16];
    int n = 0;
    while ((*p >= 'a' && *p <= 'z') || eh_digito(*p)) { if (n < 15) nome[n++] = *p; p++; }
    nome[n] = '\0';
    const reg_info_t *r = reg_procura(nome);
    if (!r) erro("registrador desconhecido '%%%s'", nome);
    return r;
}

static void le_operando(operando_t *o)
{
    memset(o, 0, sizeof *o);
    o->base = o->indice = -1;
    pula_espacos();
    if (*p == '*') { o->indireto = 1; p++; pula_espacos(); }
    if (*p == '$') {
        p++;
        o->tipo = O_IMED;
        o->e = le_expr();
        o->tem_e = 1;
        return;
    }
    if (*p == '%') {
        const reg_info_t *r = le_registrador();
        pula_espacos();
        if (*p == ':') erro("prefixo de segmento não suportado");
        o->tipo = O_REG;
        o->r = r;
        return;
    }
    o->tipo = O_MEM;
    if (*p != '(') { o->e = le_expr(); o->tem_e = 1; pula_espacos(); }
    else o->e = expr_cte(0);
    if (*p != '(') return;               /* endereço absoluto */
    p++;
    pula_espacos();
    if (*p == '%') {
        const reg_info_t *b = le_registrador();
        if (b->classe == R_RIP) o->rip = 1;
        else if (b->classe != R_GPR || b->tam < 4) erro("base inválida");
        else { o->base = b->num; o->tam_ender = b->tam; }
    }
    pula_espacos();
    if (*p == ',') {
        p++;
        pula_espacos();
        const reg_info_t *x = le_registrador();
        if (x->classe != R_GPR || x->tam < 4 || x->num == 4) erro("índice inválido");
        o->indice = x->num;
        if (o->tam_ender && o->tam_ender != x->tam) erro("base e índice de tamanhos diferentes");
        o->tam_ender = x->tam;
        o->escala = 1;
        if (espera(',')) {
            pula_espacos();
            i64 e = le_numero();
            if (e != 1 && e != 2 && e != 4 && e != 8) erro("escala deve ser 1, 2, 4 ou 8");
            o->escala = (int)e;
        }
    }
    if (!espera(')')) erro("falta ')' no endereço");
    if (o->tam_ender == 8 && modo32) erro("registrador de 64 bits num endereço em .code32");
    if (o->tam_ender == 4 && !modo32) erro("endereço com registradores de 32 bits não suportado");
}

/* -------------------------- Codificação --------------------------------- */

/* Uma instrução em montagem: prefixos, REX, opcode, ModRM/SIB, imediato. */
typedef struct {
    u8  pref[4];
    int npref;
    int w;                 /* REX.W: operando de 64 bits */
    int rex_forcado;       /* spl/bpl/sil/dil */
    int proibe_rex;        /* ah/ch/dh/bh */
    u8  op[3];
    int nop;
    int reg_op;            /* registrador somado ao opcode (+r); -1 = não */
    int tem_modrm;
    int campo_reg;         /* o campo "reg" do ModRM (registrador ou /n) */
    const operando_t *rm;
    int    tam_imed;       /* 0 = sem imediato */
    expr_t imed;
    int    imed_assinado;
    int    tam_imed2;      /* ljmp: o seletor, depois do endereço */
    i64    imed2;
    int    tipo_got;       /* para endereços @gotpcrel */
} cod_t;

static void cod_inicia(cod_t *c)
{
    memset(c, 0, sizeof *c);
    c->reg_op = -1;
}

static void cod_op(cod_t *c, int a, int b, int d)
{
    c->op[0] = (u8)a; c->nop = 1;
    if (b >= 0) { c->op[1] = (u8)b; c->nop = 2; }
    if (d >= 0) { c->op[2] = (u8)d; c->nop = 3; }
}

static void cod_pref(cod_t *c, int v) { c->pref[c->npref++] = (u8)v; }

/* Registradores de 1 byte que mudam o REX. */
static void nota_reg(cod_t *c, const reg_info_t *r)
{
    if (r->rex) c->rex_forcado = 1;
    if (r->alto) c->proibe_rex = 1;
}

static int cabe8(i64 v) { return v >= -128 && v <= 127; }

/* O imediato de 'tam' bytes, normalizado para o tamanho da operação. */
static i64 normaliza(i64 v, int tam)
{
    if (tam == 1) return (int8_t)v;
    if (tam == 2) return (int16_t)v;
    if (tam == 4) return (int32_t)v;
    return v;
}

static void emite(cod_t *c)
{
    u8 t[32];
    int n = 0;
    int rex = 0;
    int campo_reg = c->campo_reg;
    const operando_t *rm = c->rm;
    struct { int ofs, tam, assinado, pcrel, got, chamada; expr_t e; } fix[2];
    int nfix = 0;

    if (c->w) rex |= 8;
    if (c->tem_modrm && (campo_reg & 8)) rex |= 4;
    if (rm && rm->tipo == O_REG && (rm->r->num & 8)) rex |= 1;
    if (rm && rm->tipo == O_MEM) {
        if (rm->base >= 0 && (rm->base & 8)) rex |= 1;
        if (rm->indice >= 0 && (rm->indice & 8)) rex |= 2;
    }
    if (c->reg_op >= 0 && (c->reg_op & 8)) rex |= 1;
    if (rex || c->rex_forcado) {
        if (modo32) erro("instrução de 64 bits em .code32");
        if (c->proibe_rex) erro("ah/bh/ch/dh não podem ser usados aqui");
    }

    for (int i = 0; i < c->npref; i++) t[n++] = c->pref[i];
    if (rex || c->rex_forcado) t[n++] = (u8)(0x40 | rex);
    for (int i = 0; i < c->nop; i++) t[n++] = c->op[i];
    if (c->reg_op >= 0) t[n - 1] = (u8)(t[n - 1] + (c->reg_op & 7));

    if (c->tem_modrm) {
        int reg = campo_reg & 7;
        if (rm->tipo == O_REG) {
            t[n++] = (u8)(0xC0 | reg << 3 | (rm->r->num & 7));
        } else {
            expr_t e = rm->e;
            int cte = expr_eh_cte(&e);
            if (rm->rip) {
                t[n++] = (u8)(reg << 3 | 5);
                fix[nfix].ofs = n; fix[nfix].tam = 4; fix[nfix].pcrel = 1;
                fix[nfix].assinado = 1; fix[nfix].e = e; fix[nfix].chamada = 0;
                fix[nfix].got = e.mod == MOD_GOTPCREL ? c->tipo_got : 0;
                nfix++;
                n += 4;
            } else if (rm->base < 0 && rm->indice < 0) {
                /* endereço absoluto */
                if (modo32) t[n++] = (u8)(reg << 3 | 5);
                else { t[n++] = (u8)(reg << 3 | 4); t[n++] = 0x25; }
                fix[nfix].ofs = n; fix[nfix].tam = 4; fix[nfix].pcrel = 0;
                fix[nfix].assinado = !modo32; fix[nfix].e = e; fix[nfix].got = 0; fix[nfix].chamada = 0;
                nfix++;
                n += 4;
            } else {
                int mod, disp;
                int base = rm->base, sib = rm->indice >= 0 || base == 4 || base == 12;
                if (base < 0) { mod = 0; disp = 4; }
                else if (cte && e.c == 0 && (base & 7) != 5) { mod = 0; disp = 0; }
                else if (cte && cabe8(e.c)) { mod = 1; disp = 1; }
                else { mod = 2; disp = 4; }
                if (sib) {
                    t[n++] = (u8)(mod << 6 | reg << 3 | 4);
                    int esc = rm->escala == 8 ? 3 : rm->escala == 4 ? 2 : rm->escala == 2 ? 1 : 0;
                    int idx = rm->indice >= 0 ? (rm->indice & 7) : 4;
                    t[n++] = (u8)(esc << 6 | idx << 3 | (base < 0 ? 5 : (base & 7)));
                } else {
                    t[n++] = (u8)(mod << 6 | reg << 3 | (base & 7));
                }
                if (disp == 1) t[n++] = (u8)e.c;
                else if (disp == 4) {
                    fix[nfix].ofs = n; fix[nfix].tam = 4; fix[nfix].pcrel = 0;
                    fix[nfix].assinado = !modo32; fix[nfix].e = e; fix[nfix].got = 0; fix[nfix].chamada = 0;
                    nfix++;
                    n += 4;
                }
            }
        }
    }
    if (c->tam_imed) {
        fix[nfix].ofs = n; fix[nfix].tam = c->tam_imed; fix[nfix].pcrel = 0;
        fix[nfix].assinado = c->imed_assinado; fix[nfix].e = c->imed; fix[nfix].got = 0;
        fix[nfix].chamada = 0;
        nfix++;
        n += c->tam_imed;
    }
    if (c->tam_imed2) {
        for (int i = 0; i < c->tam_imed2; i++) t[n++] = (u8)(c->imed2 >> (8 * i));
    }

    /* Grava: números já conhecidos vão direto; o resto vira correção. */
    pedaco_t *ped = dados_atual();
    u64 ini = ped->b.n;
    for (int i = 0; i < nfix; i++) {
        expr_t e = fix[i].e;
        if (expr_eh_cte(&e) && !fix[i].pcrel) {
            for (int k = 0; k < fix[i].tam; k++) t[fix[i].ofs + k] = (u8)((u64)e.c >> (8 * k));
        } else {
            for (int k = 0; k < fix[i].tam; k++) t[fix[i].ofs + k] = 0;
            correcao_nova(ped, ini + (u64)fix[i].ofs, fix[i].tam, e, fix[i].pcrel,
                          fix[i].pcrel ? n - fix[i].ofs : 0, fix[i].assinado, fix[i].got,
                          fix[i].chamada);
        }
    }
    buf_poe(&ped->b, t, (size_t)n);
}

/* ---------------------------- Mnemônicos -------------------------------- */

static int cond_num(const char *s)
{
    static const struct { const char *n; int c; } sin[] = {
        {"o",0},{"no",1},{"b",2},{"c",2},{"nae",2},{"ae",3},{"nb",3},{"nc",3},
        {"e",4},{"z",4},{"ne",5},{"nz",5},{"be",6},{"na",6},{"a",7},{"nbe",7},
        {"s",8},{"ns",9},{"p",10},{"pe",10},{"np",11},{"po",11},{"l",12},{"nge",12},
        {"ge",13},{"nl",13},{"le",14},{"ng",14},{"g",15},{"nle",15},
    };
    for (size_t i = 0; i < sizeof sin / sizeof sin[0]; i++)
        if (strcmp(sin[i].n, s) == 0) return sin[i].c;
    return -1;
}

static int tam_sufixo(int c)
{
    switch (c) {
    case 'b': return 1;
    case 'w': return 2;
    case 'l': return 4;
    case 'q': return 8;
    }
    return 0;
}

/* O tamanho da operação: o do sufixo ou o do primeiro registrador comum. */
static int tam_operacao(int sufixo, operando_t *o, int n)
{
    if (sufixo) return sufixo;
    for (int i = n - 1; i >= 0; i--)
        if (o[i].tipo == O_REG && o[i].r->classe == R_GPR) return o[i].r->tam;
    erro("tamanho da operação desconhecido (use um sufixo: b, w, l ou q)");
    return 0;
}

/* Prefixos e REX.W conforme o tamanho da operação. */
static void cod_tam(cod_t *c, int tam)
{
    if (tam == 2) cod_pref(c, 0x66);
    else if (tam == 8) {
        if (modo32) erro("operação de 64 bits em .code32");
        c->w = 1;
    }
}

static void exige(int cond, const char *msg) { if (!cond) erro("%s", msg); }

static int eh_reg(const operando_t *o, int classe)
{
    return o->tipo == O_REG && o->r->classe == classe;
}

static int eh_rm(const operando_t *o)
{
    return o->tipo == O_MEM || eh_reg(o, R_GPR);
}

/* Reg do ModRM = registrador; rm = registrador ou memória. */
static void cod_modrm(cod_t *c, int campo, const operando_t *rm)
{
    c->tem_modrm = 1;
    c->campo_reg = campo;
    c->rm = rm;
    if (rm->tipo == O_REG) nota_reg(c, rm->r);
}

static void cod_imed(cod_t *c, const operando_t *o, int tam, int assinado)
{
    c->tam_imed = tam;
    c->imed = o->e;
    c->imed_assinado = assinado;
    if (expr_eh_cte(&o->e)) c->imed.c = normaliza(o->e.c, tam > 4 ? 8 : tam);
}

/* Imediato de 32 bits de uma operação de 64: sempre estendido com sinal. */
static int tam_imed_de(int tam) { return tam == 8 ? 4 : tam; }

/* add or adc sbb and sub xor cmp */
static void enc_alu(int n, int sufixo, operando_t *o, int no)
{
    exige(no == 2, "esperava 2 operandos");
    operando_t *src = &o[0], *dst = &o[1];
    int tam = tam_operacao(sufixo, o, 2);
    cod_t c;
    cod_inicia(&c);
    cod_tam(&c, tam);
    if (src->tipo == O_IMED) {
        exige(eh_rm(dst), "destino inválido");
        int cte = expr_eh_cte(&src->e);
        i64 v = cte ? normaliza(src->e.c, tam > 4 ? 4 : tam) : 0;
        if (tam == 1) {
            if (eh_reg(dst, R_GPR) && dst->r->num == 0) cod_op(&c, 0x04 + 8 * n, -1, -1);
            else { cod_op(&c, 0x80, -1, -1); cod_modrm(&c, n, dst); }
            cod_imed(&c, src, 1, 0);
        } else if (cte && cabe8(v)) {
            cod_op(&c, 0x83, -1, -1);
            cod_modrm(&c, n, dst);
            cod_imed(&c, src, 1, 1);
        } else {
            if (eh_reg(dst, R_GPR) && dst->r->num == 0) cod_op(&c, 0x05 + 8 * n, -1, -1);
            else { cod_op(&c, 0x81, -1, -1); cod_modrm(&c, n, dst); }
            cod_imed(&c, src, tam_imed_de(tam), tam == 8);
        }
    } else if (eh_reg(src, R_GPR)) {
        exige(eh_rm(dst), "destino inválido");
        nota_reg(&c, src->r);
        cod_op(&c, (tam == 1 ? 0x00 : 0x01) + 8 * n, -1, -1);
        cod_modrm(&c, src->r->num, dst);
    } else {
        exige(src->tipo == O_MEM && eh_reg(dst, R_GPR), "operandos inválidos");
        nota_reg(&c, dst->r);
        cod_op(&c, (tam == 1 ? 0x02 : 0x03) + 8 * n, -1, -1);
        cod_modrm(&c, dst->r->num, src);
        c.tipo_got = c.w ? R_X86_64_REX_GOTPCRELX : R_X86_64_GOTPCRELX;
    }
    emite(&c);
}

static void enc_test(int sufixo, operando_t *o, int no)
{
    exige(no == 2, "esperava 2 operandos");
    int tam = tam_operacao(sufixo, o, 2);
    cod_t c;
    cod_inicia(&c);
    cod_tam(&c, tam);
    if (o[0].tipo == O_IMED) {
        exige(eh_rm(&o[1]), "destino inválido");
        if (eh_reg(&o[1], R_GPR) && o[1].r->num == 0) cod_op(&c, tam == 1 ? 0xA8 : 0xA9, -1, -1);
        else { cod_op(&c, tam == 1 ? 0xF6 : 0xF7, -1, -1); cod_modrm(&c, 0, &o[1]); }
        cod_imed(&c, &o[0], tam_imed_de(tam), tam == 8);
    } else {
        const operando_t *r = eh_reg(&o[0], R_GPR) ? &o[0] : &o[1];
        const operando_t *m = r == &o[0] ? &o[1] : &o[0];
        exige(eh_reg(r, R_GPR) && eh_rm(m), "operandos inválidos");
        nota_reg(&c, r->r);
        cod_op(&c, tam == 1 ? 0x84 : 0x85, -1, -1);
        cod_modrm(&c, r->r->num, m);
        c.tipo_got = c.w ? R_X86_64_REX_GOTPCRELX : R_X86_64_GOTPCRELX;
    }
    emite(&c);
}

static void enc_mov(int sufixo, operando_t *o, int no, int movabs)
{
    exige(no == 2, "esperava 2 operandos");
    operando_t *src = &o[0], *dst = &o[1];
    cod_t c;
    cod_inicia(&c);

    /* registradores de controle e de segmento */
    if (eh_reg(src, R_CR) || eh_reg(dst, R_CR)) {
        const operando_t *cr = eh_reg(src, R_CR) ? src : dst, *g = cr == src ? dst : src;
        exige(eh_reg(g, R_GPR) && g->r->tam == (modo32 ? 4 : 8), "mov de controle: use um registrador inteiro");
        cod_op(&c, 0x0F, cr == src ? 0x20 : 0x22, -1);
        cod_modrm(&c, cr->r->num, g);
        emite(&c);
        return;
    }
    if (eh_reg(src, R_SEG) || eh_reg(dst, R_SEG)) {
        const operando_t *sg = eh_reg(src, R_SEG) ? src : dst, *g = sg == src ? dst : src;
        exige(eh_rm(g), "mov de segmento inválido");
        cod_op(&c, sg == dst ? 0x8E : 0x8C, -1, -1);
        cod_modrm(&c, sg->r->num, g);
        emite(&c);
        return;
    }

    int tam = tam_operacao(sufixo, o, 2);
    cod_tam(&c, tam);
    if (src->tipo == O_IMED) {
        int cte = expr_eh_cte(&src->e);
        if (eh_reg(dst, R_GPR)) {
            nota_reg(&c, dst->r);
            if (tam == 8 && !movabs && (!cte || (src->e.c >= INT32_MIN && src->e.c <= INT32_MAX))) {
                cod_op(&c, 0xC7, -1, -1);
                cod_modrm(&c, 0, dst);
                cod_imed(&c, src, 4, 1);
            } else {
                cod_op(&c, tam == 1 ? 0xB0 : 0xB8, -1, -1);
                c.reg_op = dst->r->num;
                cod_imed(&c, src, tam, 0);
            }
        } else {
            exige(dst->tipo == O_MEM, "destino inválido");
            cod_op(&c, tam == 1 ? 0xC6 : 0xC7, -1, -1);
            cod_modrm(&c, 0, dst);
            cod_imed(&c, src, tam_imed_de(tam), tam == 8);
        }
    } else if (eh_reg(src, R_GPR)) {
        nota_reg(&c, src->r);
        if (modo32 && src->r->num == 0 && dst->tipo == O_MEM && dst->base < 0 && dst->indice < 0 && !dst->rip) {
            cod_op(&c, tam == 1 ? 0xA2 : 0xA3, -1, -1);         /* moffs */
            c.tam_imed = 4; c.imed = dst->e; c.imed_assinado = 0;
        } else {
            exige(eh_rm(dst), "destino inválido");
            cod_op(&c, tam == 1 ? 0x88 : 0x89, -1, -1);
            cod_modrm(&c, src->r->num, dst);
        }
    } else {
        exige(src->tipo == O_MEM && eh_reg(dst, R_GPR), "operandos inválidos");
        nota_reg(&c, dst->r);
        if (modo32 && dst->r->num == 0 && src->base < 0 && src->indice < 0 && !src->rip) {
            cod_op(&c, tam == 1 ? 0xA0 : 0xA1, -1, -1);
            c.tam_imed = 4; c.imed = src->e; c.imed_assinado = 0;
        } else {
            cod_op(&c, tam == 1 ? 0x8A : 0x8B, -1, -1);
            cod_modrm(&c, dst->r->num, src);
            c.tipo_got = c.w ? R_X86_64_REX_GOTPCRELX : R_X86_64_GOTPCRELX;
        }
    }
    emite(&c);
}

/* movzb* movzw* movsb* movsw* movslq: a origem é menor que o destino. */
static void enc_movx(int sinal, int tam_src, int tam_dst, operando_t *o, int no)
{
    exige(no == 2 && eh_reg(&o[1], R_GPR) && eh_rm(&o[0]), "operandos inválidos");
    if (!tam_dst) tam_dst = o[1].r->tam;
    if (!tam_src) {
        exige(eh_reg(&o[0], R_GPR), "tamanho da origem desconhecido");
        tam_src = o[0].r->tam;
    }
    cod_t c;
    cod_inicia(&c);
    cod_tam(&c, tam_dst);
    nota_reg(&c, o[1].r);
    if (tam_src == 4) cod_op(&c, 0x63, -1, -1);
    else cod_op(&c, 0x0F, (sinal ? 0xBE : 0xB6) + (tam_src == 2), -1);
    cod_modrm(&c, o[1].r->num, &o[0]);
    emite(&c);
}

static void enc_lea(int sufixo, operando_t *o, int no)
{
    exige(no == 2 && o[0].tipo == O_MEM && eh_reg(&o[1], R_GPR), "lea: operandos inválidos");
    int tam = tam_operacao(sufixo, o, 2);
    cod_t c;
    cod_inicia(&c);
    cod_tam(&c, tam);
    cod_op(&c, 0x8D, -1, -1);
    cod_modrm(&c, o[1].r->num, &o[0]);
    emite(&c);
}

static void enc_xchg(int sufixo, operando_t *o, int no)
{
    exige(no == 2, "esperava 2 operandos");
    int tam = tam_operacao(sufixo, o, 2);
    cod_t c;
    cod_inicia(&c);
    cod_tam(&c, tam);
    if (tam != 1 && eh_reg(&o[0], R_GPR) && eh_reg(&o[1], R_GPR) &&
        (o[0].r->num == 0 || o[1].r->num == 0) &&
        !(tam == 4 && o[0].r->num == 0 && o[1].r->num == 0)) {
        cod_op(&c, 0x90, -1, -1);
        c.reg_op = o[0].r->num == 0 ? o[1].r->num : o[0].r->num;
    } else {
        const operando_t *r = eh_reg(&o[0], R_GPR) ? &o[0] : &o[1];
        const operando_t *m = r == &o[0] ? &o[1] : &o[0];
        exige(eh_reg(r, R_GPR) && eh_rm(m), "operandos inválidos");
        nota_reg(&c, r->r);
        cod_op(&c, tam == 1 ? 0x86 : 0x87, -1, -1);
        cod_modrm(&c, r->r->num, m);
    }
    emite(&c);
}

/* not neg mul imul div idiv (um operando): F6/F7 /n; inc dec: FE/FF /n */
static void enc_grupo3(int op8, int n, int sufixo, operando_t *o, int no)
{
    exige(no == 1 && eh_rm(&o[0]), "esperava 1 operando");
    int tam = tam_operacao(sufixo, o, 1);
    cod_t c;
    cod_inicia(&c);
    cod_tam(&c, tam);
    if (modo32 && op8 == 0xFE && tam != 1 && eh_reg(&o[0], R_GPR)) {
        cod_op(&c, n ? 0x48 : 0x40, -1, -1);     /* inc/dec curtos (só em 32 bits) */
        c.reg_op = o[0].r->num;
        emite(&c);
        return;
    }
    cod_op(&c, tam == 1 ? op8 : op8 + 1, -1, -1);
    cod_modrm(&c, n, &o[0]);
    emite(&c);
}

static void enc_imul(int sufixo, operando_t *o, int no)
{
    if (no == 1) { enc_grupo3(0xF6, 5, sufixo, o, no); return; }
    exige(no == 2 || no == 3, "imul: operandos inválidos");
    operando_t *dst = &o[no - 1];
    exige(eh_reg(dst, R_GPR), "imul: o destino deve ser um registrador");
    int tam = tam_operacao(sufixo, o, no);
    cod_t c;
    cod_inicia(&c);
    cod_tam(&c, tam);
    if (o[0].tipo == O_IMED) {
        const operando_t *src = no == 3 ? &o[1] : dst;
        int cte = expr_eh_cte(&o[0].e);
        if (cte && cabe8(normaliza(o[0].e.c, tam > 4 ? 4 : tam))) {
            cod_op(&c, 0x6B, -1, -1);
            cod_modrm(&c, dst->r->num, src);
            cod_imed(&c, &o[0], 1, 1);
        } else {
            cod_op(&c, 0x69, -1, -1);
            cod_modrm(&c, dst->r->num, src);
            cod_imed(&c, &o[0], tam_imed_de(tam), tam == 8);
        }
    } else {
        exige(no == 2 && eh_rm(&o[0]), "imul: operandos inválidos");
        cod_op(&c, 0x0F, 0xAF, -1);
        cod_modrm(&c, dst->r->num, &o[0]);
    }
    emite(&c);
}

/* rol ror rcl rcr shl/sal shr sar */
static void enc_desloc(int n, int sufixo, operando_t *o, int no)
{
    exige(no == 1 || no == 2, "deslocamento: operandos inválidos");
    operando_t *dst = &o[no - 1];
    exige(eh_rm(dst), "destino inválido");
    int tam = sufixo ? sufixo : (eh_reg(dst, R_GPR) ? dst->r->tam : 0);
    if (!tam) erro("tamanho da operação desconhecido");
    cod_t c;
    cod_inicia(&c);
    cod_tam(&c, tam);
    if (no == 1 || (o[0].tipo == O_IMED && expr_eh_cte(&o[0].e) && o[0].e.c == 1)) {
        cod_op(&c, tam == 1 ? 0xD0 : 0xD1, -1, -1);
    } else if (o[0].tipo == O_IMED) {
        cod_op(&c, tam == 1 ? 0xC0 : 0xC1, -1, -1);
        cod_imed(&c, &o[0], 1, 0);
    } else {
        exige(eh_reg(&o[0], R_GPR) && o[0].r->num == 1 && o[0].r->tam == 1, "o deslocamento usa $n ou %cl");
        cod_op(&c, tam == 1 ? 0xD2 : 0xD3, -1, -1);
    }
    cod_modrm(&c, n, dst);
    emite(&c);
}

static void enc_push_pop(int push, operando_t *o, int no)
{
    exige(no == 1, "esperava 1 operando");
    cod_t c;
    cod_inicia(&c);
    if (eh_reg(&o[0], R_GPR)) {
        exige(o[0].r->tam == (modo32 ? 4 : 8), "push/pop: registrador do tamanho da pilha");
        cod_op(&c, push ? 0x50 : 0x58, -1, -1);
        c.reg_op = o[0].r->num;
    } else if (o[0].tipo == O_IMED) {
        exige(push, "pop de um número?");
        if (expr_eh_cte(&o[0].e) && cabe8(o[0].e.c)) { cod_op(&c, 0x6A, -1, -1); cod_imed(&c, &o[0], 1, 1); }
        else { cod_op(&c, 0x68, -1, -1); cod_imed(&c, &o[0], 4, 1); }
    } else {
        exige(o[0].tipo == O_MEM, "operando inválido");
        cod_op(&c, push ? 0xFF : 0x8F, -1, -1);
        cod_modrm(&c, push ? 6 : 0, &o[0]);
    }
    emite(&c);
}

static void enc_setcc(int cc, operando_t *o, int no)
{
    exige(no == 1 && eh_rm(&o[0]), "set: operando inválido");
    exige(o[0].tipo != O_REG || o[0].r->tam == 1, "set: use um registrador de 1 byte");
    cod_t c;
    cod_inicia(&c);
    cod_op(&c, 0x0F, 0x90 + cc, -1);
    cod_modrm(&c, 0, &o[0]);
    emite(&c);
}

static void enc_cmov(int cc, int sufixo, operando_t *o, int no)
{
    exige(no == 2 && eh_reg(&o[1], R_GPR) && eh_rm(&o[0]), "cmov: operandos inválidos");
    int tam = tam_operacao(sufixo, o, 2);
    cod_t c;
    cod_inicia(&c);
    cod_tam(&c, tam);
    cod_op(&c, 0x0F, 0x40 + cc, -1);
    cod_modrm(&c, o[1].r->num, &o[0]);
    emite(&c);
}

/* call e jmp para um endereço em registrador ou memória */
static void enc_indireto(int n, operando_t *o)
{
    cod_t c;
    cod_inicia(&c);
    exige(eh_rm(&o[0]), "operando inválido");
    if (o[0].tipo == O_REG) exige(o[0].r->tam == (modo32 ? 4 : 8), "registrador do tamanho de um endereço");
    cod_op(&c, 0xFF, -1, -1);
    cod_modrm(&c, n, &o[0]);
    emite(&c);
}

static void enc_call(operando_t *o, int no)
{
    exige(no == 1, "call: esperava 1 operando");
    if (o[0].indireto || o[0].tipo == O_REG) { enc_indireto(2, o); return; }
    exige(o[0].tipo == O_MEM && o[0].base < 0 && o[0].indice < 0 && !o[0].rip, "call: destino inválido");
    pedaco_t *ped = dados_atual();
    buf_byte(&ped->b, 0xE8);
    correcao_nova(ped, ped->b.n, 4, o[0].e, 1, 4, 1, 0, 1);
    buf_le(&ped->b, 0, 4);
}

/* jmp e jcc para um rótulo: um pedaço próprio, porque o tamanho varia. */
static void enc_salto(int cond, operando_t *o, int no)
{
    exige(no == 1, "salto: esperava 1 operando");
    if (cond < 0 && (o[0].indireto || o[0].tipo == O_REG)) { enc_indireto(4, o); return; }
    exige(o[0].tipo == O_MEM && o[0].base < 0 && o[0].indice < 0 && !o[0].rip, "salto: destino inválido");
    pedaco_t *ped = pedaco_novo(P_SALTO);
    ped->cond = cond;
    ped->alvo = o[0].e;
}

static void enc_es(int entrada, int sufixo, operando_t *o, int no)
{
    /* in %dx, %al / in $n, %al / out %al, %dx / out %al, $n */
    exige(no == 2, "in/out: esperava 2 operandos");
    const operando_t *dado = entrada ? &o[1] : &o[0], *porta = entrada ? &o[0] : &o[1];
    exige(eh_reg(dado, R_GPR) && dado->r->num == 0, "in/out usam %al, %ax ou %eax");
    int tam = sufixo ? sufixo : dado->r->tam;
    cod_t c;
    cod_inicia(&c);
    if (tam == 2) cod_pref(&c, 0x66);
    int base = entrada ? 0xE4 : 0xE6;
    if (porta->tipo == O_IMED) {
        cod_op(&c, base + (tam != 1), -1, -1);
        cod_imed(&c, porta, 1, 0);
    } else {
        exige(eh_reg(porta, R_GPR) && porta->r->num == 2 && porta->r->tam == 2, "a porta é $n ou %dx");
        cod_op(&c, base + 8 + (tam != 1), -1, -1);
    }
    emite(&c);
}

/* Instruções SSE: prefixo, opcode, e a forma dos operandos. */
enum { S_XX, S_MOV, S_CVT2I, S_CVTI2, S_MOVQ, S_MOVD };
static const struct {
    const char *nome;
    int pref, op, op_st, forma;
} sse[] = {
    {"addss",0xF3,0x58,0,S_XX},{"addsd",0xF2,0x58,0,S_XX},
    {"subss",0xF3,0x5C,0,S_XX},{"subsd",0xF2,0x5C,0,S_XX},
    {"mulss",0xF3,0x59,0,S_XX},{"mulsd",0xF2,0x59,0,S_XX},
    {"divss",0xF3,0x5E,0,S_XX},{"divsd",0xF2,0x5E,0,S_XX},
    {"sqrtss",0xF3,0x51,0,S_XX},{"sqrtsd",0xF2,0x51,0,S_XX},
    {"minss",0xF3,0x5D,0,S_XX},{"minsd",0xF2,0x5D,0,S_XX},
    {"maxss",0xF3,0x5F,0,S_XX},{"maxsd",0xF2,0x5F,0,S_XX},
    {"ucomiss",0,0x2E,0,S_XX},{"ucomisd",0x66,0x2E,0,S_XX},
    {"comiss",0,0x2F,0,S_XX},{"comisd",0x66,0x2F,0,S_XX},
    {"cvtss2sd",0xF3,0x5A,0,S_XX},{"cvtsd2ss",0xF2,0x5A,0,S_XX},
    {"xorps",0,0x57,0,S_XX},{"xorpd",0x66,0x57,0,S_XX},
    {"andps",0,0x54,0,S_XX},{"andpd",0x66,0x54,0,S_XX},
    {"orps",0,0x56,0,S_XX},{"orpd",0x66,0x56,0,S_XX},
    {"pxor",0x66,0xEF,0,S_XX},
    {"movss",0xF3,0x10,0x11,S_MOV},{"movsd",0xF2,0x10,0x11,S_MOV},
    {"movaps",0,0x28,0x29,S_MOV},{"movapd",0x66,0x28,0x29,S_MOV},
    {"movups",0,0x10,0x11,S_MOV},{"movupd",0x66,0x10,0x11,S_MOV},
    {"movdqa",0x66,0x6F,0x7F,S_MOV},{"movdqu",0xF3,0x6F,0x7F,S_MOV},
    {"cvttss2si",0xF3,0x2C,0,S_CVT2I},{"cvttsd2si",0xF2,0x2C,0,S_CVT2I},
    {"cvtss2si",0xF3,0x2D,0,S_CVT2I},{"cvtsd2si",0xF2,0x2D,0,S_CVT2I},
    {"cvtsi2ss",0xF3,0x2A,0,S_CVTI2},{"cvtsi2sd",0xF2,0x2A,0,S_CVTI2},
    {"movq",0,0,0,S_MOVQ},{"movd",0,0,0,S_MOVD},
};

static int enc_sse(const char *mn, operando_t *o, int no)
{
    char base[32];
    int sufixo = 0;
    for (size_t i = 0; i < sizeof sse / sizeof sse[0]; i++) {
        size_t k = strlen(sse[i].nome);
        if (strncmp(mn, sse[i].nome, k) != 0) continue;
        if (mn[k] && !((sse[i].forma == S_CVT2I || sse[i].forma == S_CVTI2) &&
                       (mn[k] == 'l' || mn[k] == 'q') && !mn[k + 1]))
            continue;
        sufixo = mn[k] ? tam_sufixo(mn[k]) : 0;
        strcpy(base, sse[i].nome);
        int forma = sse[i].forma;
        if ((forma == S_MOVQ || forma == S_MOVD) &&
            !(no == 2 && (eh_reg(&o[0], R_XMM) || eh_reg(&o[1], R_XMM))))
            return 0;                         /* movq comum, de inteiros */
        exige(no == 2, "esperava 2 operandos");
        cod_t c;
        cod_inicia(&c);
        operando_t *src = &o[0], *dst = &o[1];
        switch (forma) {
        case S_XX:
            exige(eh_reg(dst, R_XMM) && (eh_reg(src, R_XMM) || src->tipo == O_MEM), "operandos inválidos");
            if (sse[i].pref) cod_pref(&c, sse[i].pref);
            cod_op(&c, 0x0F, sse[i].op, -1);
            cod_modrm(&c, dst->r->num, src);
            break;
        case S_MOV:
            if (sse[i].pref) cod_pref(&c, sse[i].pref);
            if (eh_reg(dst, R_XMM)) {
                exige(eh_reg(src, R_XMM) || src->tipo == O_MEM, "operandos inválidos");
                cod_op(&c, 0x0F, sse[i].op, -1);
                cod_modrm(&c, dst->r->num, src);
            } else {
                exige(eh_reg(src, R_XMM) && dst->tipo == O_MEM, "operandos inválidos");
                cod_op(&c, 0x0F, sse[i].op_st, -1);
                cod_modrm(&c, src->r->num, dst);
            }
            break;
        case S_CVT2I:
            exige(eh_reg(dst, R_GPR) && (eh_reg(src, R_XMM) || src->tipo == O_MEM), "operandos inválidos");
            cod_pref(&c, sse[i].pref);
            if ((sufixo ? sufixo : dst->r->tam) == 8) c.w = 1;
            cod_op(&c, 0x0F, sse[i].op, -1);
            cod_modrm(&c, dst->r->num, src);
            break;
        case S_CVTI2:
            exige(eh_reg(dst, R_XMM) && eh_rm(src), "operandos inválidos");
            cod_pref(&c, sse[i].pref);
            if ((sufixo ? sufixo : (src->tipo == O_REG ? src->r->tam : 4)) == 8) c.w = 1;
            cod_op(&c, 0x0F, sse[i].op, -1);
            cod_modrm(&c, dst->r->num, src);
            break;
        case S_MOVQ:
        case S_MOVD:
            if (eh_reg(dst, R_XMM) && eh_reg(src, R_GPR)) {
                cod_pref(&c, 0x66); c.w = forma == S_MOVQ;
                cod_op(&c, 0x0F, 0x6E, -1); cod_modrm(&c, dst->r->num, src);
            } else if (eh_reg(src, R_XMM) && eh_reg(dst, R_GPR)) {
                cod_pref(&c, 0x66); c.w = forma == S_MOVQ;
                cod_op(&c, 0x0F, 0x7E, -1); cod_modrm(&c, src->r->num, dst);
            } else if (forma == S_MOVQ && eh_reg(dst, R_XMM)) {
                exige(eh_reg(src, R_XMM) || src->tipo == O_MEM, "operandos inválidos");
                cod_pref(&c, 0xF3);
                cod_op(&c, 0x0F, 0x7E, -1); cod_modrm(&c, dst->r->num, src);
            } else if (forma == S_MOVQ && eh_reg(src, R_XMM) && dst->tipo == O_MEM) {
                cod_pref(&c, 0x66);
                cod_op(&c, 0x0F, 0xD6, -1); cod_modrm(&c, src->r->num, dst);
            } else if (forma == S_MOVD && eh_reg(dst, R_XMM)) {
                cod_pref(&c, 0x66);
                cod_op(&c, 0x0F, 0x6E, -1); cod_modrm(&c, dst->r->num, src);
            } else {
                exige(forma == S_MOVD && eh_reg(src, R_XMM), "operandos inválidos");
                cod_pref(&c, 0x66);
                cod_op(&c, 0x0F, 0x7E, -1); cod_modrm(&c, src->r->num, dst);
            }
            break;
        }
        emite(&c);
        return 1;
    }
    return 0;
}

/* Instruções sem operandos: os bytes, e o modo em que valem. */
static const struct {
    const char *nome;
    u8 b[4];
    int n;
    int so64;     /* 1: só no modo de 64 bits */
} simples[] = {
    {"ret",{0xC3},1,0},{"retq",{0xC3},1,0},{"leave",{0xC9},1,0},{"leaveq",{0xC9},1,0},
    {"nop",{0x90},1,0},{"hlt",{0xF4},1,0},{"cli",{0xFA},1,0},{"sti",{0xFB},1,0},
    {"cld",{0xFC},1,0},{"std",{0xFD},1,0},{"int3",{0xCC},1,0},
    {"cqto",{0x48,0x99},2,1},{"cqo",{0x48,0x99},2,1},{"cltq",{0x48,0x98},2,1},
    {"cltd",{0x99},1,0},{"cdq",{0x99},1,0},{"cwtl",{0x98},1,0},
    {"ud2",{0x0F,0x0B},2,0},{"endbr64",{0xF3,0x0F,0x1E,0xFA},4,0},
    {"cpuid",{0x0F,0xA2},2,0},{"rdmsr",{0x0F,0x32},2,0},{"wrmsr",{0x0F,0x30},2,0},
    {"rdtsc",{0x0F,0x31},2,0},{"iretq",{0x48,0xCF},2,1},{"iret",{0xCF},1,0},
    {"pushfq",{0x9C},1,1},{"popfq",{0x9D},1,1},{"pushfl",{0x9C},1,0},{"popfl",{0x9D},1,0},
    {"fninit",{0xDB,0xE3},2,0},{"pause",{0xF3,0x90},2,0},
    {"lodsb",{0xAC},1,0},{"stosb",{0xAA},1,0},{"stosw",{0x66,0xAB},2,0},
    {"stosl",{0xAB},1,0},{"stosq",{0x48,0xAB},2,1},{"movsb",{0xA4},1,0},
    {"movsq",{0x48,0xA5},2,1},
};

static int enc_simples(const char *mn)
{
    for (size_t i = 0; i < sizeof simples / sizeof simples[0]; i++) {
        if (strcmp(simples[i].nome, mn) != 0) continue;
        if (simples[i].so64 && modo32) erro("'%s' não existe em .code32", mn);
        buf_poe(&dados_atual()->b, simples[i].b, (size_t)simples[i].n);
        return 1;
    }
    return 0;
}

static const struct { const char *nome; int n; } alus[] = {
    {"add",0},{"or",1},{"adc",2},{"sbb",3},{"and",4},{"sub",5},{"xor",6},{"cmp",7},
};
static const struct { const char *nome; int n; } desls[] = {
    {"rol",0},{"ror",1},{"rcl",2},{"rcr",3},{"shl",4},{"sal",4},{"shr",5},{"sar",7},
};
static const struct { const char *nome; int op8, n; } grupo3[] = {
    {"not",0xF6,2},{"neg",0xF6,3},{"mul",0xF6,4},{"div",0xF6,6},{"idiv",0xF6,7},
    {"inc",0xFE,0},{"dec",0xFE,1},
};

/* Separa "addq" em "add" + 8; devolve 0 se não for desta família. */
static int com_sufixo(const char *mn, const char *base, int *sufixo)
{
    size_t k = strlen(base);
    if (strncmp(mn, base, k) != 0) return 0;
    if (!mn[k]) { *sufixo = 0; return 1; }
    if (mn[k + 1] || !tam_sufixo(mn[k])) return 0;
    *sufixo = tam_sufixo(mn[k]);
    return 1;
}

static void instrucao(char *mn, operando_t *o, int no)
{
    int s, cc;

    if (strcmp(mn, "rep") == 0 || strcmp(mn, "lock") == 0) {
        buf_byte(&dados_atual()->b, mn[0] == 'r' ? 0xF3 : 0xF0);
        return;
    }
    if (no == 0 && enc_simples(mn)) return;
    if (enc_sse(mn, o, no)) return;

    for (size_t i = 0; i < sizeof alus / sizeof alus[0]; i++)
        if (com_sufixo(mn, alus[i].nome, &s)) { enc_alu(alus[i].n, s, o, no); return; }
    for (size_t i = 0; i < sizeof desls / sizeof desls[0]; i++)
        if (com_sufixo(mn, desls[i].nome, &s)) { enc_desloc(desls[i].n, s, o, no); return; }
    for (size_t i = 0; i < sizeof grupo3 / sizeof grupo3[0]; i++)
        if (com_sufixo(mn, grupo3[i].nome, &s)) { enc_grupo3(grupo3[i].op8, grupo3[i].n, s, o, no); return; }
    if (com_sufixo(mn, "imul", &s)) { enc_imul(s, o, no); return; }
    if (com_sufixo(mn, "test", &s)) { enc_test(s, o, no); return; }
    if (com_sufixo(mn, "lea", &s))  { enc_lea(s, o, no); return; }
    if (com_sufixo(mn, "xchg", &s)) { enc_xchg(s, o, no); return; }
    if (strcmp(mn, "movabsq") == 0 || strcmp(mn, "movabs") == 0) { enc_mov(8, o, no, 1); return; }
    if (com_sufixo(mn, "mov", &s))  { enc_mov(s, o, no, 0); return; }
    if (strcmp(mn, "movslq") == 0 || strcmp(mn, "movsxd") == 0) { enc_movx(1, 4, 8, o, no); return; }
    if ((strncmp(mn, "movz", 4) == 0 || strncmp(mn, "movs", 4) == 0) &&
        (mn[4] == 'b' || mn[4] == 'w') && strlen(mn) <= 6) {
        int ts = mn[4] == 'b' ? 1 : 2, td = mn[5] ? tam_sufixo(mn[5]) : 0;
        if (!mn[5] || td > ts) { enc_movx(mn[3] == 's', ts, td, o, no); return; }
    }
    if (strcmp(mn, "push") == 0 || strcmp(mn, "pushq") == 0 || strcmp(mn, "pushl") == 0) { enc_push_pop(1, o, no); return; }
    if (strcmp(mn, "pop") == 0 || strcmp(mn, "popq") == 0 || strcmp(mn, "popl") == 0) { enc_push_pop(0, o, no); return; }
    if (strcmp(mn, "call") == 0 || strcmp(mn, "callq") == 0 || strcmp(mn, "calll") == 0) { enc_call(o, no); return; }
    if (strcmp(mn, "jmp") == 0 || strcmp(mn, "jmpq") == 0) { enc_salto(-1, o, no); return; }
    if (mn[0] == 'j' && (cc = cond_num(mn + 1)) >= 0) { enc_salto(cc, o, no); return; }
    if (strncmp(mn, "set", 3) == 0 && (cc = cond_num(mn + 3)) >= 0) { enc_setcc(cc, o, no); return; }
    if (strncmp(mn, "set", 3) == 0 && mn[3] && mn[strlen(mn) - 1] == 'b') {
        char t[16]; snprintf(t, sizeof t, "%s", mn + 3); t[strlen(t) - 1] = '\0';
        if ((cc = cond_num(t)) >= 0) { enc_setcc(cc, o, no); return; }
    }
    if (strncmp(mn, "cmov", 4) == 0) {
        if ((cc = cond_num(mn + 4)) >= 0) { enc_cmov(cc, 0, o, no); return; }
        char t[16]; snprintf(t, sizeof t, "%s", mn + 4);
        size_t k = strlen(t);
        if (k > 1 && tam_sufixo(t[k - 1]) > 1) {
            int sf = tam_sufixo(t[k - 1]);
            t[k - 1] = '\0';
            if ((cc = cond_num(t)) >= 0) { enc_cmov(cc, sf, o, no); return; }
        }
    }
    if (com_sufixo(mn, "in", &s))  { enc_es(1, s, o, no); return; }
    if (com_sufixo(mn, "out", &s)) { enc_es(0, s, o, no); return; }
    if (strcmp(mn, "int") == 0) {
        exige(no == 1 && o[0].tipo == O_IMED, "int $n");
        cod_t c; cod_inicia(&c); cod_op(&c, 0xCD, -1, -1); cod_imed(&c, &o[0], 1, 0); emite(&c);
        return;
    }
    if (strcmp(mn, "lgdt") == 0 || strcmp(mn, "lidt") == 0 || strcmp(mn, "lgdtl") == 0 ||
        strcmp(mn, "lidtl") == 0 || strcmp(mn, "lgdtq") == 0 || strcmp(mn, "lidtq") == 0) {
        exige(no == 1 && o[0].tipo == O_MEM, "lgdt/lidt: esperava um endereço");
        cod_t c; cod_inicia(&c); cod_op(&c, 0x0F, 0x01, -1);
        cod_modrm(&c, mn[1] == 'g' ? 2 : 3, &o[0]); emite(&c);
        return;
    }
    if (strcmp(mn, "ltr") == 0 || strcmp(mn, "ltrw") == 0) {
        exige(no == 1 && eh_rm(&o[0]), "ltr: operando inválido");
        cod_t c; cod_inicia(&c); cod_op(&c, 0x0F, 0x00, -1); cod_modrm(&c, 3, &o[0]); emite(&c);
        return;
    }
    if (strcmp(mn, "fxsave") == 0 || strcmp(mn, "fxrstor") == 0) {
        exige(no == 1 && o[0].tipo == O_MEM, "fxsave/fxrstor: esperava um endereço");
        cod_t c; cod_inicia(&c); cod_op(&c, 0x0F, 0xAE, -1);
        cod_modrm(&c, mn[2] == 's' ? 0 : 1, &o[0]); emite(&c);
        return;
    }
    if (strcmp(mn, "ljmp") == 0 || strcmp(mn, "ljmpl") == 0) {
        exige(modo32, "ljmp só em .code32");
        exige(no == 2 && o[0].tipo == O_IMED && o[1].tipo == O_IMED && expr_eh_cte(&o[0].e), "ljmp $seletor, $endereço");
        cod_t c; cod_inicia(&c); cod_op(&c, 0xEA, -1, -1);
        c.tam_imed = 4; c.imed = o[1].e; c.imed_assinado = 0;
        c.tam_imed2 = 2; c.imed2 = o[0].e.c;
        emite(&c);
        return;
    }
    erro("instrução desconhecida '%s'", mn);
}

/* ------------------------------ Diretivas ------------------------------- */

static secao_t *secao_procura(const char *nome)
{
    for (secao_t *s = secoes; s; s = s->prox)
        if (strcmp(s->nome, nome) == 0) return s;
    return 0;
}

static int comeca(const char *s, const char *pre) { return strncmp(s, pre, strlen(pre)) == 0; }

static secao_t *secao(const char *nome, const char *flags, const char *tipo)
{
    secao_t *s = secao_procura(nome);
    if (s) return s;
    s = xmalloc(sizeof *s);
    s->nome = xstrdup(nome);
    s->alinh = 1;
    /* sem flags explícitas: os nomes conhecidos dizem o que a seção é */
    s->tipo = SHT_PROGBITS;
    if (comeca(nome, ".text")) s->flags = SHF_ALLOC | SHF_EXEC;
    else if (comeca(nome, ".data") || comeca(nome, ".tdata")) s->flags = SHF_ALLOC | SHF_WRITE;
    else if (comeca(nome, ".bss")) { s->flags = SHF_ALLOC | SHF_WRITE; s->tipo = SHT_NOBITS; }
    else if (comeca(nome, ".rodata")) s->flags = SHF_ALLOC;
    else if (comeca(nome, ".note")) s->tipo = SHT_NOTE;
    if (flags) {
        s->flags = 0;
        for (const char *f = flags; *f; f++) {
            if (*f == 'a') s->flags |= SHF_ALLOC;
            else if (*f == 'w') s->flags |= SHF_WRITE;
            else if (*f == 'x') s->flags |= SHF_EXEC;
            else if (*f == 'M') s->flags |= SHF_MERGE;
            else if (*f == 'S') s->flags |= SHF_STRINGS;
            else erro("flag de seção '%c' não suportada", *f);
        }
    }
    if (tipo) {
        if (strcmp(tipo, "progbits") == 0) s->tipo = SHT_PROGBITS;
        else if (strcmp(tipo, "nobits") == 0) s->tipo = SHT_NOBITS;
        else if (strcmp(tipo, "note") == 0) s->tipo = SHT_NOTE;
        else erro("tipo de seção '@%s' não suportado", tipo);
    }
    if (secoes_ult) secoes_ult->prox = s; else secoes = s;
    secoes_ult = s;
    return s;
}

static void muda_secao(secao_t *s) { atual = s; }

static void le_string(buf_t *b)
{
    pula_espacos();
    if (*p != '"') erro("esperava uma string");
    p++;
    while (*p && *p != '"') {
        int c = (u8)*p++;
        if (c == '\\') {
            c = (u8)*p++;
            switch (c) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'a': c = 7; break;
            case 'b': c = 8; break;
            case 'f': c = 12; break;
            case 'v': c = 11; break;
            case 'x': {
                int v = 0;
                while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
                    int d = *p <= '9' ? *p - '0' : (*p | 32) - 'a' + 10;
                    v = v * 16 + d;
                    p++;
                }
                c = v & 0xFF;
                break;
            }
            default:
                if (c >= '0' && c <= '7') {
                    int v = c - '0';
                    for (int k = 0; k < 2 && *p >= '0' && *p <= '7'; k++) v = v * 8 + (*p++ - '0');
                    c = v & 0xFF;
                }
            }
        }
        buf_byte(b, (u8)c);
    }
    if (*p != '"') erro("string sem fim");
    p++;
}

static void exige_bytes(void)
{
    if (atual->tipo == SHT_NOBITS) erro("dados em seção sem conteúdo (%s)", atual->nome);
}

static void dados(int tam)
{
    do {
        expr_t e = le_expr();
        exige_bytes();
        pedaco_t *ped = dados_atual();
        if (expr_eh_cte(&e)) buf_le(&ped->b, (u64)e.c, tam);
        else {
            correcao_nova(ped, ped->b.n, tam, e, 0, 0, 0, 0, 0);
            buf_le(&ped->b, 0, tam);
        }
    } while (espera(','));
}

static void espaco(u64 n, u8 valor)
{
    pedaco_t *ped = dados_atual();
    if (atual->tipo == SHT_NOBITS) {
        if (valor) erro("valor diferente de zero em seção sem conteúdo");
        ped->espaco += n;
        return;
    }
    for (u64 i = 0; i < n; i++) buf_byte(&ped->b, valor);
}

static void alinha(u32 a, int tem_valor, u8 valor)
{
    if (!a || (a & (a - 1))) erro("alinhamento deve ser potência de 2");
    if (a > atual->alinh) atual->alinh = a;
    pedaco_t *ped = pedaco_novo(P_ALINHA);
    ped->alinh = a;
    ped->preenche = valor;
    ped->nops = (atual->flags & SHF_EXEC) && !tem_valor;
}

static i64 le_cte(void)
{
    expr_t e = le_expr();
    if (!expr_eh_cte(&e)) erro("esperava um número");
    return e.c;
}

static void le_nome_obrig(char *nome, int max)
{
    pula_espacos();
    if (!le_nome(nome, max)) erro("esperava um nome");
}

static void incbin(void)
{
    buf_t nome = {0};
    le_string(&nome);
    buf_byte(&nome, 0);
    FILE *f = fopen((char *)nome.d, "rb");
    if (!f) erro("não consegui abrir '%s'", (char *)nome.d);
    exige_bytes();
    pedaco_t *ped = dados_atual();
    u8 bloco[4096];
    size_t n;
    while ((n = fread(bloco, 1, sizeof bloco, f)) > 0) buf_poe(&ped->b, bloco, n);
    fclose(f);
    free(nome.d);
}

static void diretiva(const char *d)
{
    char nome[256];

    if (strcmp(d, ".text") == 0) muda_secao(secao(".text", 0, 0));
    else if (strcmp(d, ".data") == 0) muda_secao(secao(".data", 0, 0));
    else if (strcmp(d, ".bss") == 0) muda_secao(secao(".bss", 0, 0));
    else if (strcmp(d, ".section") == 0) {
        pula_espacos();
        int n = 0;
        while (*p && *p != ',' && *p != ' ' && *p != '\t') { if (n < 255) nome[n++] = *p; p++; }
        nome[n] = '\0';
        buf_t flags = {0};
        char tipo[32];
        int tem_flags = 0, tem_tipo = 0;
        if (espera(',')) {
            le_string(&flags);
            buf_byte(&flags, 0);
            tem_flags = 1;
            if (espera(',')) {
                pula_espacos();
                if (*p == '@' || *p == '%') p++;
                le_nome_obrig(tipo, sizeof tipo);
                tem_tipo = 1;
            }
        }
        muda_secao(secao(nome, tem_flags ? (char *)flags.d : 0, tem_tipo ? tipo : 0));
        free(flags.d);
    }
    else if (strcmp(d, ".globl") == 0 || strcmp(d, ".global") == 0 || strcmp(d, ".weak") == 0) {
        do {
            le_nome_obrig(nome, sizeof nome);
            simbolo_t *s = simbolo_registra(nome);
            s->ligacao = d[1] == 'w' ? STB_WEAK : STB_GLOBAL;
        } while (espera(','));
    }
    else if (strcmp(d, ".local") == 0 || strcmp(d, ".hidden") == 0 || strcmp(d, ".file") == 0 ||
             strcmp(d, ".ident") == 0 || strcmp(d, ".loc") == 0 || strcmp(d, ".intel_syntax") == 0) {
        if (d[1] == 'i' && d[2] == 'n') erro("só a sintaxe AT&T é suportada");
        *(char *)p = '\0';
    }
    else if (strcmp(d, ".type") == 0) {
        le_nome_obrig(nome, sizeof nome);
        simbolo_t *s = simbolo_registra(nome);
        if (!espera(',')) erro(".type nome, @tipo");
        pula_espacos();
        if (*p == '@' || *p == '%') p++;
        char t[32];
        le_nome_obrig(t, sizeof t);
        if (strcmp(t, "function") == 0) s->tipo = STT_FUNC;
        else if (strcmp(t, "object") == 0) s->tipo = STT_OBJECT;
    }
    else if (strcmp(d, ".size") == 0) {
        le_nome_obrig(nome, sizeof nome);
        simbolo_t *s = simbolo_registra(nome);
        if (!espera(',')) erro(".size nome, expressão");
        s->tam_expr = le_expr();
        s->tem_tam = 1;
    }
    else if (strcmp(d, ".byte") == 0) dados(1);
    else if (strcmp(d, ".short") == 0 || strcmp(d, ".word") == 0 || strcmp(d, ".value") == 0 ||
             strcmp(d, ".hword") == 0 || strcmp(d, ".2byte") == 0) dados(2);
    else if (strcmp(d, ".int") == 0 || strcmp(d, ".long") == 0 || strcmp(d, ".4byte") == 0) dados(4);
    else if (strcmp(d, ".quad") == 0 || strcmp(d, ".8byte") == 0) dados(8);
    else if (strcmp(d, ".ascii") == 0 || strcmp(d, ".asciz") == 0 || strcmp(d, ".string") == 0) {
        do {
            exige_bytes();
            pedaco_t *ped = dados_atual();
            le_string(&ped->b);
            if (d[5] == 'z' || d[2] == 't') buf_byte(&ped->b, 0);
        } while (espera(','));
    }
    else if (strcmp(d, ".fill") == 0) {
        i64 n = le_cte(), tam = 1, v = 0;
        if (espera(',')) { tam = le_cte(); if (espera(',')) v = le_cte(); }
        if (tam < 0 || tam > 8) erro(".fill: tamanho inválido");
        if (v == 0) espaco((u64)(n * tam), 0);
        else {
            exige_bytes();
            for (i64 i = 0; i < n; i++) buf_le(&dados_atual()->b, (u64)v, (int)tam);
        }
    }
    else if (strcmp(d, ".zero") == 0 || strcmp(d, ".skip") == 0 || strcmp(d, ".space") == 0) {
        i64 n = le_cte(), v = 0;
        if (espera(',')) v = le_cte();
        espaco((u64)n, (u8)v);
    }
    else if (strcmp(d, ".balign") == 0 || strcmp(d, ".align") == 0 || strcmp(d, ".p2align") == 0) {
        i64 a = le_cte(), v = 0;
        int tem_valor = 0;
        if (espera(',')) {
            pula_espacos();
            if (*p != ',') { v = le_cte(); tem_valor = 1; }
            if (espera(',')) erro("alinhamento com limite não é suportado");
        }
        if (d[1] == 'p') a = (i64)1 << a;
        alinha((u32)a, tem_valor, (u8)v);
    }
    else if (strcmp(d, ".comm") == 0) {
        le_nome_obrig(nome, sizeof nome);
        simbolo_t *s = simbolo_registra(nome);
        if (!espera(',')) erro(".comm nome, tamanho[, alinhamento]");
        s->tam_comum = (u64)le_cte();
        s->alinh_comum = espera(',') ? (u64)le_cte() : 1;
        s->comum = 1;
        s->ligacao = STB_GLOBAL;
        s->tipo = STT_OBJECT;
    }
    else if (strcmp(d, ".set") == 0 || strcmp(d, ".equ") == 0) {
        le_nome_obrig(nome, sizeof nome);
        if (!espera(',')) erro(".set nome, valor");
        simbolo_t *s = simbolo_registra(nome);
        expr_t e = le_expr();
        if (!expr_eh_cte(&e)) erro(".set: só valores numéricos");
        s->definido = 1; s->sec = 0; s->valor = e.c;
    }
    else if (strcmp(d, ".code32") == 0) modo32 = 1;
    else if (strcmp(d, ".code64") == 0) modo32 = 0;
    else if (strcmp(d, ".incbin") == 0) incbin();
    else erro("diretiva desconhecida '%s'", d);
}

/* ------------------------------ Uma linha ------------------------------- */

static int em_comentario;   /* dentro de um comentário de várias linhas */

/* Tira os comentários (# e barra-asterisco) e devolve a linha limpa. */
static void limpa_linha(char *l)
{
    char *w = l;
    int aspas = 0;
    for (char *r = l; *r; r++) {
        if (em_comentario) {
            if (r[0] == '*' && r[1] == '/') { em_comentario = 0; r++; }
            continue;
        }
        if (aspas) {
            *w++ = *r;
            if (*r == '\\' && r[1]) { *w++ = *++r; continue; }
            if (*r == '"') aspas = 0;
            continue;
        }
        if (*r == '"') { aspas = 1; *w++ = *r; continue; }
        if (*r == '#') break;
        if (r[0] == '/' && r[1] == '*') { em_comentario = 1; r++; *w++ = ' '; continue; }
        *w++ = *r;
    }
    *w = '\0';
}

/* Uma instrução ou diretiva (as linhas podem ter várias, separadas por ';'). */
static void comando(char *txt)
{
    char nome[256];
    p = txt;

    for (;;) {                                    /* rótulos */
        pula_espacos();
        const char *ini = p;
        if (!le_nome(nome, sizeof nome)) break;
        pula_espacos();
        if (*p == ':') { p++; define_rotulo(nome); continue; }
        if (*p == '=') {                          /* nome = valor */
            p++;
            simbolo_t *s = simbolo_registra(nome);
            expr_t e = le_expr();
            if (!expr_eh_cte(&e)) erro("'=': só valores numéricos");
            s->definido = 1; s->sec = 0; s->valor = e.c;
            pula_espacos();
            if (*p) erro("lixo no fim da linha: '%s'", p);
            return;
        }
        p = ini;
        break;
    }
    pula_espacos();
    if (!*p) return;
    if (!le_nome(nome, sizeof nome)) erro("linha inválida: '%s'", p);

    if (nome[0] == '.') diretiva(nome);
    else {
        operando_t o[3];
        int no = 0;
        pula_espacos();
        if (strcmp(nome, "rep") == 0 || strcmp(nome, "lock") == 0) {
            instrucao(nome, o, 0);
            if (*p) comando((char *)p);
            return;
        }
        if (*p) {
            do {
                if (no == 3) erro("operandos demais");
                le_operando(&o[no++]);
            } while (espera(','));
        }
        for (char *c = nome; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
        instrucao(nome, o, no);
    }
    pula_espacos();
    if (*p) erro("lixo no fim da linha: '%s'", p);
}

static void linha(char *l)
{
    limpa_linha(l);
    /* separa por ';' fora de strings */
    char *ini = l;
    int aspas = 0;
    for (char *r = l;; r++) {
        if (*r == '"' && (r == l || r[-1] != '\\')) aspas = !aspas;
        if ((*r == ';' && !aspas) || !*r) {
            int fim = !*r;
            *r = '\0';
            comando(ini);
            if (fim) break;
            ini = r + 1;
        }
    }
}

/* ----------------------------- Disposição ------------------------------- */

/* NOPs como os do GNU as: blocos de 11 bytes e o resto. */
static const u8 nops[12][11] = {
    {0},
    {0x90},
    {0x66,0x90},
    {0x0F,0x1F,0x00},
    {0x0F,0x1F,0x40,0x00},
    {0x0F,0x1F,0x44,0x00,0x00},
    {0x66,0x0F,0x1F,0x44,0x00,0x00},
    {0x0F,0x1F,0x80,0x00,0x00,0x00,0x00},
    {0x0F,0x1F,0x84,0x00,0x00,0x00,0x00,0x00},
    {0x66,0x0F,0x1F,0x84,0x00,0x00,0x00,0x00,0x00},
    {0x66,0x2E,0x0F,0x1F,0x84,0x00,0x00,0x00,0x00,0x00},
    {0x66,0x66,0x2E,0x0F,0x1F,0x84,0x00,0x00,0x00,0x00,0x00},
};

static u64 simb_ender(const simbolo_t *s) { return s->ped->ender + s->ofs; }

/* A referência pode ser resolvida aqui mesmo (destino nesta seção)? Como
 * no GNU as: um jmp/jcc resolve também um símbolo global; um call ou um
 * endereço relativo, só um local (o global pode ser trocado na ligação). */
static int alvo_local(const secao_t *sec, const expr_t *e, int salto)
{
    const simbolo_t *s = e->s;
    if (!s || e->menos || e->mod == MOD_GOTPCREL || !s->definido || s->sec != sec) return 0;
    return salto ? s->ligacao != STB_WEAK : s->ligacao == STB_LOCAL;
}

static u64 tam_pedaco(const secao_t *sec, const pedaco_t *ped, u64 end)
{
    if (ped->tipo == P_DADOS) return sec->tipo == SHT_NOBITS ? ped->espaco : ped->b.n;
    if (ped->tipo == P_ALINHA) return (ped->alinh - (end & (ped->alinh - 1))) & (ped->alinh - 1);
    return ped->longo ? (ped->cond < 0 ? 5 : 6) : 2;
}

/* Todo salto começa curto. A cada passada, os pedaços são percorridos em
 * ordem, como no GNU as: quem já passou nesta passada está no endereço
 * novo; quem ainda não passou, no antigo mais o quanto o código já
 * cresceu ("estica"). Um salto que não alcança vira longo; um
 * alinhamento pode encolher. Repete até nada mudar. */
static void dispor(secao_t *sec)
{
    u64 end = 0;
    int passada = 0, regiao = 0;
    for (pedaco_t *ped = sec->prim; ped; ped = ped->prox) {
        ped->regiao = regiao;
        if (ped->tipo == P_ALINHA) regiao++;
        ped->ender = end;
        ped->tam = tam_pedaco(sec, ped, end);
        end += ped->tam;
    }
    sec->tam = end;

    int mudou;
    do {
        mudou = 0;
        passada++;
        i64 estica = 0;
        for (pedaco_t *ped = sec->prim; ped; ped = ped->prox) {
            ped->ender = (u64)((i64)ped->ender + estica);
            ped->passada = passada;
            i64 cresce = 0;
            if (ped->tipo == P_ALINHA) {
                cresce = (i64)tam_pedaco(sec, ped, ped->ender) - (i64)ped->tam;
            } else if (ped->tipo == P_SALTO && !ped->longo) {
                if (!alvo_local(sec, &ped->alvo, 1)) {
                    ped->longo = 1;
                } else {
                    /* Destino à frente, ainda não visto: anda junto com o
                     * código, a menos que um alinhamento no meio absorva
                     * o crescimento (a regra do GNU as). */
                    const simbolo_t *a = ped->alvo.s;
                    i64 alvo = (i64)(a->ped->ender + a->ofs) + ped->alvo.c;
                    int espera_proxima = 0;
                    if (estica && a->ped->passada != passada) {
                        if (estica < 0 || a->ped->regiao == ped->regiao) alvo += estica;
                        else if (alvo < (i64)ped->ender + 1) espera_proxima = 1;
                    }
                    if (!espera_proxima && !cabe8(alvo - (i64)(ped->ender + 2))) ped->longo = 1;
                }
                if (ped->longo) cresce = ped->cond < 0 ? 3 : 4;
            }
            if (cresce) mudou = 1;
            ped->tam = (u64)((i64)ped->tam + cresce);
            estica += cresce;
        }
        sec->tam = (u64)((i64)sec->tam + estica);
    } while (mudou);

    /* os bytes finais */
    if (sec->tipo == SHT_NOBITS) return;
    for (pedaco_t *ped = sec->prim; ped; ped = ped->prox) {
        if (ped->tipo == P_DADOS) buf_poe(&sec->final, ped->b.d, ped->b.n);
        else if (ped->tipo == P_ALINHA) {
            u64 n = ped->tam;
            if (ped->nops) {
                while (n > 11) { buf_poe(&sec->final, nops[11], 11); n -= 11; }
                buf_poe(&sec->final, nops[n], (size_t)n);
            } else for (u64 i = 0; i < n; i++) buf_byte(&sec->final, ped->preenche);
        } else {
            /* o salto: o deslocamento entra na fase de correções */
            int cond = ped->cond;
            if (!ped->longo) {
                buf_byte(&sec->final, cond < 0 ? 0xEB : (u8)(0x70 + cond));
                i64 d = (i64)simb_ender(ped->alvo.s) + ped->alvo.c - (i64)(ped->ender + 2);
                buf_byte(&sec->final, (u8)d);
            } else {
                if (cond < 0) buf_byte(&sec->final, 0xE9);
                else { buf_byte(&sec->final, 0x0F); buf_byte(&sec->final, (u8)(0x80 + cond)); }
                /* a correção é criada depois, com o endereço já conhecido */
                buf_le(&sec->final, 0, 4);
            }
        }
    }
}

/* Saltos longos viram correções, como um call. */
static void corrige_saltos(secao_t *sec)
{
    for (pedaco_t *ped = sec->prim; ped; ped = ped->prox) {
        if (ped->tipo != P_SALTO || !ped->longo) continue;
        secao_t *ant = atual;
        atual = sec;
        correcao_nova(ped, ped->cond < 0 ? 1 : 2, 4, ped->alvo, 1, 4, 1, 0, 1);
        sec->corr->salto = 1;
        atual = ant;
    }
}

/* ----------------------------- Correções -------------------------------- */

static void escreve_campo(secao_t *sec, u64 ender, u64 v, int tam)
{
    for (int i = 0; i < tam; i++) sec->final.d[ender + (u64)i] = (u8)(v >> (8 * i));
}

static void confere_alcance(i64 v, int tam, int assinado, int linha_c)
{
    int ok = 1;
    if (tam == 1) ok = assinado ? (v >= -128 && v <= 127) : (v >= -128 && v <= 255);
    else if (tam == 2) ok = v >= -32768 && v <= 65535;
    else if (tam == 4) ok = assinado ? (v >= INT32_MIN && v <= INT32_MAX) : (v >= INT32_MIN && v <= (i64)UINT32_MAX);
    if (!ok) { num_linha = linha_c; erro("valor fora do alcance do campo"); }
}

static void rel_nova(secao_t *sec, u64 ofs, u32 sym, int tipo, i64 adendo)
{
    buf_le(&sec->rel, ofs, 8);
    buf_le(&sec->rel, (u64)sym << 32 | (u32)tipo, 8);
    buf_le(&sec->rel, (u64)adendo, 8);
    sec->nrel++;
}

static void resolve(secao_t *sec)
{
    /* As correções estão em ordem inversa; a lista invertida sai em ordem. */
    correcao_t *lista = 0;
    for (correcao_t *c = sec->corr, *prox; c; c = prox) { prox = c->prox; c->prox = lista; lista = c; }
    sec->corr = lista;

    for (correcao_t *c = sec->corr; c; c = c->prox) {
        u64 ender = c->ped->ender + c->ofs;
        expr_t e = c->e;
        expr_simplifica(&e);
        num_linha = c->linha;

        /* a - b na mesma seção: um número */
        if (e.s && e.menos && e.s->definido && e.menos->definido && e.s->sec && e.s->sec == e.menos->sec) {
            e.c += (i64)simb_ender(e.s) - (i64)simb_ender(e.menos);
            e.s = e.menos = 0;
        }
        if (e.menos) erro("expressão com subtração de símbolos que o montador não resolve");
        if (sec->tipo == SHT_NOBITS) erro("referência em seção sem conteúdo");

        if (!e.s) {
            i64 v = c->pcrel ? e.c - (i64)c->ajuste : e.c;
            if (c->pcrel) erro("endereço relativo a um número");
            confere_alcance(v, c->tam, c->assinado, c->linha);
            escreve_campo(sec, ender, (u64)v, c->tam);
            continue;
        }
        if (c->pcrel && alvo_local(sec, &e, c->salto)) {
            i64 v = (i64)simb_ender(e.s) + e.c - (i64)(ender + (u64)c->ajuste);
            confere_alcance(v, c->tam, 1, c->linha);
            escreve_campo(sec, ender, (u64)v, c->tam);
            continue;
        }

        /* relocação */
        int tipo;
        if (c->pcrel) {
            if (c->tam != 4) erro("salto curto para fora da seção");
            if (e.mod == MOD_GOTPCREL) tipo = c->tipo_got ? c->tipo_got : R_X86_64_GOTPCREL;
            else if (e.mod == MOD_PLT || (c->chamada && e.s->ligacao != STB_LOCAL) ||
                     (c->chamada && !e.s->definido)) tipo = R_X86_64_PLT32;
            else tipo = R_X86_64_PC32;
        } else {
            if (e.mod) erro("@plt/@gotpcrel só em endereços relativos");
            tipo = c->tam == 8 ? R_X86_64_64 : c->tam == 4 ? (c->assinado ? R_X86_64_32S : R_X86_64_32)
                 : c->tam == 2 ? R_X86_64_16 : R_X86_64_8;
        }
        i64 adendo = e.c - (c->pcrel ? c->ajuste : 0);
        u32 sym;
        if (e.s->definido && e.s->sec && e.s->ligacao == STB_LOCAL && e.mod != MOD_GOTPCREL) {
            sym = e.s->sec->sym_secao;              /* local: seção + deslocamento */
            adendo += (i64)simb_ender(e.s);
        } else {
            sym = e.s->indice;
        }
        rel_nova(sec, ender, sym, tipo, adendo);
    }
}

/* ------------------------------ Saída ELF ------------------------------- */

static buf_t strtab, shstrtab, symtab;

static u32 str_poe(buf_t *b, const char *s)
{
    u32 i = (u32)b->n;
    buf_poe(b, s, strlen(s) + 1);
    return i;
}

static void sym_poe(u32 nome, int info, u16 shndx, u64 valor, u64 tam)
{
    buf_le(&symtab, nome, 4);
    buf_byte(&symtab, (u8)info);
    buf_byte(&symtab, 0);
    buf_le(&symtab, shndx, 2);
    buf_le(&symtab, valor, 8);
    buf_le(&symtab, tam, 8);
}

static u64 tamanho_simb(simbolo_t *s)
{
    if (!s->tem_tam) return s->comum ? s->tam_comum : 0;
    expr_t e = s->tam_expr;
    expr_simplifica(&e);
    if (e.s && e.menos && e.s->definido && e.menos->definido && e.s->sec == e.menos->sec && e.s->sec) {
        e.c += (i64)simb_ender(e.s) - (i64)simb_ender(e.menos);
        e.s = e.menos = 0;
    }
    if (!expr_eh_cte(&e)) erro(".size de '%s' não é um número", s->nome);
    return (u64)e.c;
}

/* Os índices: símbolo nulo, seções, locais, e por último os globais. */
static u32 monta_simbolos(void)
{
    u32 n = 1;
    str_poe(&strtab, "");
    sym_poe(0, 0, 0, 0, 0);
    for (secao_t *s = secoes; s; s = s->prox) {
        s->sym_secao = n++;
        sym_poe(0, STB_LOCAL << 4 | STT_SECTION, (u16)s->indice, 0, 0);
    }
    for (no_simb_t *o = ordem; o; o = o->prox) {
        simbolo_t *s = o->s;
        if (s->ligacao != STB_LOCAL || !s->definido || eh_local_L(s) || s->comum) continue;
        if (strncmp(s->nome, ".L.ponto", 8) == 0) continue;
        s->indice = n++;
        u16 shndx = s->sec ? (u16)s->sec->indice : 0xFFF1;      /* SHN_ABS */
        u64 v = s->sec ? simb_ender(s) : (u64)s->valor;
        sym_poe(str_poe(&strtab, s->nome), STB_LOCAL << 4 | s->tipo, shndx, v, tamanho_simb(s));
    }
    u32 primeiro_global = n;
    for (no_simb_t *o = ordem; o; o = o->prox) {
        simbolo_t *s = o->s;
        int global = s->ligacao != STB_LOCAL || s->comum;
        int indefinido = !s->definido && !s->comum;
        if (!global && !(indefinido && s->usado)) continue;
        if (indefinido && eh_local_L(s)) { num_linha = 0; erro("rótulo '%s' usado mas não definido", s->nome); }
        s->indice = n++;
        int lig = s->ligacao == STB_LOCAL ? STB_GLOBAL : s->ligacao;
        u16 shndx = s->comum ? 0xFFF2 : indefinido ? 0 : s->sec ? (u16)s->sec->indice : 0xFFF1;
        u64 v = s->comum ? s->alinh_comum : indefinido ? 0 : s->sec ? simb_ender(s) : (u64)s->valor;
        sym_poe(str_poe(&strtab, s->nome), lig << 4 | s->tipo, shndx, v, tamanho_simb(s));
    }
    return primeiro_global;
}

static void cab_secao(buf_t *b, u32 nome, u32 tipo, u64 flags, u64 off, u64 tam,
                      u32 link, u32 info, u64 alinh, u64 entsize)
{
    buf_le(b, nome, 4); buf_le(b, tipo, 4); buf_le(b, flags, 8); buf_le(b, 0, 8);
    buf_le(b, off, 8); buf_le(b, tam, 8); buf_le(b, link, 4); buf_le(b, info, 4);
    buf_le(b, alinh, 8); buf_le(b, entsize, 8);
}

static void alinha_buf(buf_t *b, u64 a)
{
    while (b->n % a) buf_byte(b, 0);
}

static void grava(const char *saida)
{
    /* índices das seções: as do programa, depois .rela.*, .symtab... */
    u32 n = 1;
    for (secao_t *s = secoes; s; s = s->prox) s->indice = n++;
    u32 primeiro_global = monta_simbolos();
    for (secao_t *s = secoes; s; s = s->prox) resolve(s);
    u32 nrela = 0;
    for (secao_t *s = secoes; s; s = s->prox) if (s->nrel) nrela++;
    u32 i_symtab = n + nrela, i_strtab = i_symtab + 1, i_shstr = i_strtab + 1;

    buf_t arq = {0}, cab = {0};
    buf_le(&arq, 0, 64);                         /* o cabeçalho vem no fim */
    str_poe(&shstrtab, "");
    u64 *offs = xmalloc(sizeof(u64) * (n + nrela + 4));
    u32 *nomes = xmalloc(sizeof(u32) * (n + nrela + 4));

    for (secao_t *s = secoes; s; s = s->prox) {
        nomes[s->indice] = str_poe(&shstrtab, s->nome);
        if (s->tipo != SHT_NOBITS) {
            alinha_buf(&arq, s->alinh);
            offs[s->indice] = arq.n;
            buf_poe(&arq, s->final.d, s->final.n);
        } else offs[s->indice] = arq.n;
    }
    u32 k = n;
    for (secao_t *s = secoes; s; s = s->prox) {
        if (!s->nrel) continue;
        char nome[300];
        snprintf(nome, sizeof nome, ".rela%s", s->nome);
        nomes[k] = str_poe(&shstrtab, nome);
        alinha_buf(&arq, 8);
        offs[k] = arq.n;
        buf_poe(&arq, s->rel.d, s->rel.n);
        k++;
    }
    nomes[i_symtab] = str_poe(&shstrtab, ".symtab");
    alinha_buf(&arq, 8);
    offs[i_symtab] = arq.n;
    buf_poe(&arq, symtab.d, symtab.n);
    nomes[i_strtab] = str_poe(&shstrtab, ".strtab");
    offs[i_strtab] = arq.n;
    buf_poe(&arq, strtab.d, strtab.n);
    nomes[i_shstr] = str_poe(&shstrtab, ".shstrtab");
    offs[i_shstr] = arq.n;
    buf_poe(&arq, shstrtab.d, shstrtab.n);
    alinha_buf(&arq, 8);
    u64 shoff = arq.n;

    cab_secao(&cab, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    for (secao_t *s = secoes; s; s = s->prox)
        cab_secao(&cab, nomes[s->indice], s->tipo, s->flags, offs[s->indice],
                  s->tipo == SHT_NOBITS ? s->tam : s->final.n, 0, 0, s->alinh, 0);
    k = n;
    for (secao_t *s = secoes; s; s = s->prox) {
        if (!s->nrel) continue;
        cab_secao(&cab, nomes[k], SHT_RELA, SHF_INFO_LINK, offs[k], s->rel.n, i_symtab, s->indice, 8, 24);
        k++;
    }
    cab_secao(&cab, nomes[i_symtab], SHT_SYMTAB, 0, offs[i_symtab], symtab.n, i_strtab, primeiro_global, 8, 24);
    cab_secao(&cab, nomes[i_strtab], SHT_STRTAB, 0, offs[i_strtab], strtab.n, 0, 0, 1, 0);
    cab_secao(&cab, nomes[i_shstr], SHT_STRTAB, 0, offs[i_shstr], shstrtab.n, 0, 0, 1, 0);
    buf_poe(&arq, cab.d, cab.n);

    /* o cabeçalho ELF */
    u8 *h = arq.d;
    memset(h, 0, 64);
    h[0] = 0x7F; h[1] = 'E'; h[2] = 'L'; h[3] = 'F';
    h[4] = 2; h[5] = 1; h[6] = 1;                /* 64 bits, little-endian, versão 1 */
    h[16] = 1;                                   /* ET_REL */
    h[18] = 62;                                  /* EM_X86_64 */
    h[20] = 1;
    for (int i = 0; i < 8; i++) h[40 + i] = (u8)(shoff >> (8 * i));
    h[52] = 64;                                  /* e_ehsize */
    h[58] = 64;                                  /* e_shentsize */
    u32 nsec = i_shstr + 1;
    h[60] = (u8)nsec; h[61] = (u8)(nsec >> 8);
    h[62] = (u8)i_shstr; h[63] = (u8)(i_shstr >> 8);

    FILE *f = fopen(saida, "wb");
    if (!f) { fprintf(stderr, "nefs-as: não consegui criar '%s'\n", saida); exit(1); }
    if (fwrite(arq.d, 1, arq.n, f) != arq.n || fclose(f) != 0) {
        fprintf(stderr, "nefs-as: erro ao gravar '%s'\n", saida);
        exit(1);
    }
}

/* -------------------------------- main ---------------------------------- */

static char *le_arquivo(const char *nome, size_t *tam)
{
    FILE *f = strcmp(nome, "-") == 0 ? stdin : fopen(nome, "rb");
    if (!f) { fprintf(stderr, "nefs-as: não consegui abrir '%s'\n", nome); exit(1); }
    buf_t b = {0};
    char bloco[4096];
    size_t n;
    while ((n = fread(bloco, 1, sizeof bloco, f)) > 0) buf_poe(&b, bloco, n);
    if (f != stdin) fclose(f);
    buf_byte(&b, 0);
    *tam = b.n - 1;
    return (char *)b.d;
}

int main(int argc, char **argv)
{
    const char *saida = 0, *entrada = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) saida = argv[++i];
        else if (strcmp(argv[i], "--64") == 0) continue;
        else if (argv[i][0] == '-' && argv[i][1]) {
            fprintf(stderr, "uso: nefs-as [-o saída.o] entrada.s\n");
            return 1;
        } else entrada = argv[i];
    }
    if (!entrada) { fprintf(stderr, "uso: nefs-as [-o saída.o] entrada.s\n"); return 1; }
    char padrao[1024];
    if (!saida) {
        snprintf(padrao, sizeof padrao, "%s", entrada);
        char *ponto = strrchr(padrao, '.');
        if (ponto && strchr(ponto, '/') == 0) *ponto = '\0';
        strncat(padrao, ".o", sizeof padrao - strlen(padrao) - 1);
        saida = padrao;
    }
    arquivo = entrada;

    size_t tam;
    char *texto = le_arquivo(entrada, &tam);
    muda_secao(secao(".text", 0, 0));
    secao(".data", 0, 0);
    secao(".bss", 0, 0);
    char *l = texto;
    while (*l) {
        char *fim = strchr(l, '\n');
        if (fim) *fim = '\0';
        num_linha++;
        linha(l);
        if (!fim) break;
        l = fim + 1;
    }

    for (secao_t *s = secoes; s; s = s->prox) dispor(s);
    for (secao_t *s = secoes; s; s = s->prox) corrige_saltos(s);
    grava(saida);
    return 0;
}
