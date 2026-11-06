/*
 * ===========================================================================
 *  NEFS - ferramentas/nefs-cpp.c
 *  O pré-processador C do NEFS: trata #include, #define, #if... e expande
 *  as macros, como manda o padrão C11 (seção 6.10). A saída vai para o
 *  cproc, com marcadores de linha ("# 12 \"arquivo.c\"") para os erros
 *  apontarem o lugar certo.
 *
 *    nefs-cpp [opções] [entrada.c | -]
 *
 *      -o saída            grava aqui (padrão: a saída padrão)
 *      -I pasta            procura cabeçalhos aqui
 *      -iquote/-isystem/-idirafter pasta   idem, como no cpp do GCC
 *      -D nome[=valor]     define uma macro;  -U nome  apaga
 *      -include arquivo    processa antes da entrada
 *      -nostdinc           não procura em /usr/include
 *      -undef              não define as macros do sistema (__x86_64__...)
 *      -std=c99|c11|c17    o valor de __STDC_VERSION__
 *      -P                  sem marcadores de linha
 *
 *  Como funciona: cada arquivo vira uma lista de "tokens" (nomes, números,
 *  strings, sinais). A lista é percorrida uma vez; as diretivas (#...) são
 *  executadas, e cada nome de macro é trocado pela expansão, que volta
 *  para o começo da lista e é lida de novo. Para uma macro não se expandir
 *  dentro dela mesma, cada token carrega o conjunto das macros que o
 *  produziram (o "hideset", do algoritmo de Dave Prosser, o mesmo que os
 *  compiladores usam).
 *
 *  Escrito em C padrão: compila no Linux e dentro do próprio NovaEra.
 * ===========================================================================
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t u64;
typedef int64_t  i64;

/* ------------------------------ Utilidades ------------------------------- */

static void *xmalloc(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    if (!p) { fprintf(stderr, "nefs-cpp: sem memória\n"); exit(1); }
    return p;
}

static char *xstrndup(const char *s, size_t n)
{
    char *d = xmalloc(n + 1);
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

static char *xstrdup(const char *s) { return xstrndup(s, strlen(s)); }

/* Texto que cresce. */
typedef struct {
    char  *d;
    size_t n, cap;
} texto_t;

static void tx_poe(texto_t *t, const char *s, size_t n)
{
    if (t->n + n + 1 > t->cap) {
        t->cap = (t->n + n + 1) * 2;
        char *d = realloc(t->d, t->cap);
        if (!d) { fprintf(stderr, "nefs-cpp: sem memória\n"); exit(1); }
        t->d = d;
    }
    memcpy(t->d + t->n, s, n);
    t->n += n;
    t->d[t->n] = '\0';
}

static void tx_c(texto_t *t, char c) { tx_poe(t, &c, 1); }
static void tx_s(texto_t *t, const char *s) { tx_poe(t, s, strlen(s)); }

/* Nomes únicos: o mesmo texto vira sempre o mesmo ponteiro. Assim comparar
 * nomes de macros é comparar ponteiros. */
#define NTAB 4096u
typedef struct nome { char *s; struct nome *prox; } nome_t;
static nome_t *nomes[NTAB];

static unsigned hash(const char *s, size_t n)
{
    unsigned h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char)s[i]) * 16777619u;
    return h % NTAB;
}

static char *interno(const char *s, size_t n)
{
    unsigned h = hash(s, n);
    for (nome_t *x = nomes[h]; x; x = x->prox)
        if (strncmp(x->s, s, n) == 0 && x->s[n] == '\0') return x->s;
    nome_t *x = xmalloc(sizeof *x);
    x->s = xstrndup(s, n);
    x->prox = nomes[h];
    nomes[h] = x;
    return x->s;
}

/* ------------------------------- Tokens ---------------------------------- */

enum { T_NOME, T_NUM, T_CAR, T_STR, T_SINAL, T_OUTRO, T_MARCA, T_FIM };

typedef struct arquivo {
    char *nome;
    char *texto;
    int   uma_vez;          /* #pragma once */
} arquivo_t;

typedef struct hideset {
    const char *nome;
    struct hideset *prox;
} hideset_t;

typedef struct tok {
    int        tipo;
    char      *s;           /* o texto; em T_NOME, o nome único */
    int        espaco;      /* tem espaço antes */
    int        inicio;      /* é o primeiro da linha */
    int        expandido;   /* veio de uma macro (nunca começa diretiva) */
    arquivo_t *arq;
    const char *nome_arq;   /* o nome mostrado (#line pode mudar) */
    int        linha;
    hideset_t *hs;
    struct tok *prox;
} tok_t;

static const char *loc_nome = "nefs-cpp";
static int loc_linha;

static void erro_em(const tok_t *t, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (t) fprintf(stderr, "%s:%d: erro: ", t->nome_arq, t->linha);
    else   fprintf(stderr, "%s:%d: erro: ", loc_nome, loc_linha);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static void aviso_em(const tok_t *t, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:%d: aviso: ", t->nome_arq, t->linha);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static tok_t *tok_novo(int tipo, const char *s, const tok_t *modelo)
{
    tok_t *t = xmalloc(sizeof *t);
    t->tipo = tipo;
    t->s = (char *)s;
    if (modelo) {
        t->arq = modelo->arq;
        t->nome_arq = modelo->nome_arq;
        t->linha = modelo->linha;
    }
    return t;
}

static tok_t *tok_copia(const tok_t *t)
{
    tok_t *c = xmalloc(sizeof *c);
    *c = *t;
    c->prox = NULL;
    return c;
}

static tok_t *tok_fim(const tok_t *modelo)
{
    tok_t *t = tok_novo(T_FIM, "", modelo);
    return t;
}

static int eh(const tok_t *t, const char *s) { return t->tipo != T_FIM && t->tipo != T_MARCA && strcmp(t->s, s) == 0; }

/* -------------------------------- Leitura -------------------------------- */

static int letra(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$'; }
static int digito(int c) { return c >= '0' && c <= '9'; }

/* Junta as linhas terminadas em '\': as quebras tiradas voltam depois da
 * próxima quebra de verdade, para os números de linha não mudarem. */
static void junta_linhas(char *p)
{
    char *w = p;
    int pendentes = 0;
    while (*p) {
        if (p[0] == '\\' && p[1] == '\n') { p += 2; pendentes++; }
        else if (p[0] == '\\' && p[1] == '\r' && p[2] == '\n') { p += 3; pendentes++; }
        else if (*p == '\n') {
            *w++ = *p++;
            for (; pendentes > 0; pendentes--) *w++ = '\n';
        } else *w++ = *p++;
    }
    for (; pendentes > 0; pendentes--) *w++ = '\n';
    *w = '\0';
}

static const char *const sinais[] = {
    "<<=", ">>=", "...", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=",
    "&&", "||", "*=", "/=", "%=", "+=", "-=", "&=", "^=", "|=", "##",
    "::",                                           /* C23: [[gnu::packed]] */
};

static size_t tam_sinal(const char *p)
{
    for (size_t i = 0; i < sizeof sinais / sizeof sinais[0]; i++) {
        size_t n = strlen(sinais[i]);
        if (strncmp(p, sinais[i], n) == 0) return n;
    }
    return strchr("+-*/%&|^~!=<>?:;,.()[]{}#", *p) && *p ? 1 : 0;
}

/* Uma string ou caractere a partir do apóstrofo/aspas em p. */
static const char *fim_literal(const char *p, char fecha, const arquivo_t *a, int linha)
{
    p++;
    while (*p != fecha) {
        if (*p == '\n' || *p == '\0') {
            loc_nome = a->nome; loc_linha = linha;
            erro_em(NULL, "%s sem fim", fecha == '"' ? "string" : "caractere");
        }
        if (*p == '\\' && p[1]) p++;
        p++;
    }
    return p + 1;
}

static tok_t *tokenizar(arquivo_t *a)
{
    tok_t cabeca = {0}, *cur = &cabeca;
    char *p = a->texto;
    int linha = 1, inicio = 1, espaco = 0;

    while (*p) {
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
            espaco = 1;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            char *q = strstr(p + 2, "*/");
            if (!q) { loc_nome = a->nome; loc_linha = linha; erro_em(NULL, "comentário sem fim"); }
            for (; p < q; p++) if (*p == '\n') linha++;
            p = q + 2;
            espaco = 1;
            continue;
        }
        if (*p == '\n') { p++; linha++; inicio = 1; espaco = 0; continue; }
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\f' || *p == '\v') { p++; espaco = 1; continue; }

        const char *ini = p;
        int tipo;
        if (digito(*p) || (*p == '.' && digito(p[1]))) {
            p++;
            for (;;) {
                if ((p[0] == 'e' || p[0] == 'E' || p[0] == 'p' || p[0] == 'P') && (p[1] == '+' || p[1] == '-')) p += 2;
                else if (letra(*p) || digito(*p) || *p == '.') p++;
                else if (*p == '\'' && (letra(p[1]) || digito(p[1]))) p += 2;   /* C23: 1'000 */
                else break;
            }
            tipo = T_NUM;
        } else if ((p[0] == 'u' && p[1] == '8' && (p[2] == '"' || p[2] == '\'')) ) {
            p = (char *)fim_literal(p + 2, p[2], a, linha);
            tipo = ini[2] == '"' ? T_STR : T_CAR;
        } else if ((p[0] == 'L' || p[0] == 'u' || p[0] == 'U') && (p[1] == '"' || p[1] == '\'')) {
            p = (char *)fim_literal(p + 1, p[1], a, linha);
            tipo = ini[1] == '"' ? T_STR : T_CAR;
        } else if (*p == '"' || *p == '\'') {
            p = (char *)fim_literal(p, *p, a, linha);
            tipo = *ini == '"' ? T_STR : T_CAR;
        } else if (letra(*p)) {
            while (letra(*p) || digito(*p)) p++;
            tipo = T_NOME;
        } else {
            size_t n = tam_sinal(p);
            if (n) { p += n; tipo = T_SINAL; }
            else { p++; tipo = T_OUTRO; }
        }
        tok_t *t = xmalloc(sizeof *t);
        t->tipo = tipo;
        t->s = tipo == T_NOME ? interno(ini, (size_t)(p - ini)) : xstrndup(ini, (size_t)(p - ini));
        t->espaco = espaco;
        t->inicio = inicio;
        t->arq = a;
        t->nome_arq = a->nome;
        t->linha = linha;
        cur = cur->prox = t;
        inicio = espaco = 0;
    }
    tok_t *f = xmalloc(sizeof *f);
    f->tipo = T_FIM;
    f->s = "";
    f->arq = a;
    f->nome_arq = a->nome;
    f->linha = linha;
    f->inicio = 1;
    cur->prox = f;
    return cabeca.prox;
}

static char *le_arquivo(const char *nome)
{
    FILE *f = strcmp(nome, "-") == 0 ? stdin : fopen(nome, "rb");
    if (!f) return NULL;
    texto_t t = {0};
    char bloco[4096];
    size_t n;
    while ((n = fread(bloco, 1, sizeof bloco, f)) > 0) tx_poe(&t, bloco, n);
    if (f != stdin) fclose(f);
    if (!t.d) tx_s(&t, "");
    if (t.n && t.d[t.n - 1] != '\n') tx_c(&t, '\n');
    return t.d;
}

#define MAX_ARQS 4096
static arquivo_t *arquivos[MAX_ARQS];
static int narquivos;

static arquivo_t *arquivo_de(const char *nome, char *texto)
{
    arquivo_t *a = xmalloc(sizeof *a);
    a->nome = xstrdup(nome);
    junta_linhas(texto);
    a->texto = texto;
    if (narquivos < MAX_ARQS) arquivos[narquivos++] = a;
    return a;
}

/* -------------------------------- Hidesets ------------------------------- */

static int hs_tem(const hideset_t *h, const char *nome)
{
    for (; h; h = h->prox) if (h->nome == nome) return 1;
    return 0;
}

static hideset_t *hs_mais(hideset_t *h, const char *nome)
{
    if (hs_tem(h, nome)) return h;
    hideset_t *n = xmalloc(sizeof *n);
    n->nome = nome;
    n->prox = h;
    return n;
}

static hideset_t *hs_uniao(hideset_t *a, hideset_t *b)
{
    for (; b; b = b->prox) a = hs_mais(a, b->nome);
    return a;
}

static hideset_t *hs_intersecao(hideset_t *a, hideset_t *b)
{
    hideset_t *r = NULL;
    for (; a; a = a->prox) if (hs_tem(b, a->nome)) r = hs_mais(r, a->nome);
    return r;
}

/* -------------------------------- Macros --------------------------------- */

typedef tok_t *(*embutida_t)(const tok_t *);

typedef struct macro {
    const char *nome;
    int         funcao;      /* tipo função: NOME(a, b) */
    const char **params;
    int         nparams;
    const char *va;          /* o nome do parâmetro variável (__VA_ARGS__) */
    tok_t      *corpo;
    embutida_t  embutida;    /* __FILE__, __LINE__... */
    struct macro *prox;
} macro_t;

static macro_t *macros[NTAB];

static unsigned hash_ptr(const char *p) { return (unsigned)(((uintptr_t)p >> 3) % NTAB); }

static macro_t *macro_acha(const char *nome)
{
    for (macro_t *m = macros[hash_ptr(nome)]; m; m = m->prox)
        if (m->nome == nome) return m;
    return NULL;
}

static void macro_apaga(const char *nome)
{
    macro_t **pm = &macros[hash_ptr(nome)];
    for (; *pm; pm = &(*pm)->prox)
        if ((*pm)->nome == nome) { *pm = (*pm)->prox; return; }
}

static macro_t *macro_nova(const char *nome)
{
    macro_apaga(nome);
    macro_t *m = xmalloc(sizeof *m);
    m->nome = nome;
    unsigned h = hash_ptr(nome);
    m->prox = macros[h];
    macros[h] = m;
    return m;
}

/* -------------------------- Listas de tokens ----------------------------- */

typedef struct { tok_t cabeca; tok_t *ult; } lista_t;

static void lista_inicia(lista_t *l) { memset(&l->cabeca, 0, sizeof l->cabeca); l->ult = &l->cabeca; }
static void lista_poe(lista_t *l, tok_t *t) { l->ult = l->ult->prox = t; t->prox = NULL; }

/* Os tokens até o fim da linha da diretiva, terminados por um T_FIM. */
static tok_t *resto_da_linha(tok_t **rest, tok_t *t)
{
    lista_t l;
    lista_inicia(&l);
    while (!t->inicio) {
        lista_poe(&l, tok_copia(t));
        t = t->prox;
    }
    lista_poe(&l, tok_fim(t));
    *rest = t;
    return l.cabeca.prox;
}

static tok_t *pula_linha(tok_t *t, int avisa)
{
    if (t->inicio) return t;
    if (avisa) aviso_em(t, "lixo no fim da diretiva: '%s'", t->s);
    while (!t->inicio) t = t->prox;
    return t;
}

/* ------------------------- # e ## (texto e colagem) ---------------------- */

static void escapa(texto_t *t, const char *s)
{
    for (; *s; s++) {
        if (*s == '\\' || *s == '"') tx_c(t, '\\');
        tx_c(t, *s);
    }
}

/* #param: os tokens do argumento viram uma string. */
static tok_t *texto_de(const tok_t *arg, const tok_t *modelo)
{
    texto_t t = {0};
    tx_c(&t, '"');
    for (const tok_t *a = arg; a->tipo != T_FIM; a = a->prox) {
        if (a->tipo == T_MARCA) continue;
        if (a != arg && a->espaco && t.n > 1) tx_c(&t, ' ');
        if (a->tipo == T_STR || a->tipo == T_CAR) escapa(&t, a->s);
        else tx_s(&t, a->s);
    }
    tx_c(&t, '"');
    tok_t *r = tok_novo(T_STR, t.d, modelo);
    return r;
}

/* a ## b: o texto dos dois juntos tem que ser um token só. */
static tok_t *cola(const tok_t *a, const tok_t *b)
{
    texto_t t = {0};
    tx_s(&t, a->s);
    tx_s(&t, b->s);
    arquivo_t tmp = { (char *)a->nome_arq, t.d, 0 };
    tx_c(&t, '\n');
    tmp.texto = t.d;
    tok_t *r = tokenizar(&tmp);
    if (r->tipo == T_FIM || r->prox->tipo != T_FIM)
        erro_em(a, "'%s' ## '%s' não forma um token válido", a->s, b->s);
    r->arq = a->arq;
    r->nome_arq = a->nome_arq;
    r->linha = a->linha;
    r->espaco = a->espaco;
    r->inicio = 0;
    r->prox = NULL;
    return r;
}

/* ------------------------------- Expansão -------------------------------- */

typedef struct {
    tok_t *bruto;          /* como veio (para # e ##) */
    tok_t *expandido;      /* com as macros expandidas (calculado sob demanda) */
} arg_t;

static int expande(tok_t **rest, tok_t *t);

/* Expande todas as macros de uma lista (um argumento, uma linha de #if). */
static tok_t *expande_lista(tok_t *t)
{
    lista_t l;
    lista_inicia(&l);
    while (t->tipo != T_FIM) {
        if (expande(&t, t)) continue;
        lista_poe(&l, tok_copia(t));
        t = t->prox;
    }
    lista_poe(&l, tok_copia(t));
    return l.cabeca.prox;
}

static int param_de(const macro_t *m, const tok_t *t)
{
    if (t->tipo != T_NOME || !m->funcao) return -1;
    for (int i = 0; i < m->nparams; i++) if (m->params[i] == t->s) return i;
    if (m->va && t->s == m->va) return m->nparams;
    return -1;
}

/* Um argumento: até a vírgula (ou o ')') do nível de fora. */
static tok_t *le_um_arg(tok_t **rest, tok_t *t, int ate_o_fim)
{
    lista_t l;
    lista_inicia(&l);
    int nivel = 0;
    for (;;) {
        if (t->tipo == T_FIM) erro_em(t, "chamada de macro sem ')'");
        if (nivel == 0 && (eh(t, ")") || (!ate_o_fim && eh(t, ",")))) break;
        if (eh(t, "(")) nivel++;
        else if (eh(t, ")")) nivel--;
        tok_t *c = tok_copia(t);
        c->inicio = 0;
        lista_poe(&l, c);
        t = t->prox;
    }
    lista_poe(&l, tok_fim(t));
    *rest = t;
    return l.cabeca.prox;
}

static arg_t *le_args(tok_t **rest, tok_t *t, const macro_t *m, tok_t **fecha)
{
    int n = m->nparams + (m->va ? 1 : 0);
    arg_t *args = xmalloc(sizeof(arg_t) * (size_t)(n + 1));
    t = t->prox;                               /* depois do '(' */
    for (int i = 0; i < m->nparams; i++) {
        if (i > 0) {
            if (!eh(t, ",")) erro_em(t, "poucos argumentos para a macro '%s'", m->nome);
            t = t->prox;
        }
        args[i].bruto = le_um_arg(&t, t, 0);
    }
    if (m->va) {
        if (eh(t, ")")) args[m->nparams].bruto = tok_fim(t);
        else {
            if (m->nparams > 0) {
                if (!eh(t, ",")) erro_em(t, "poucos argumentos para a macro '%s'", m->nome);
                t = t->prox;
            }
            args[m->nparams].bruto = le_um_arg(&t, t, 1);
        }
    }
    if (!eh(t, ")")) erro_em(t, "argumentos demais para a macro '%s'", m->nome);
    *fecha = t;
    *rest = t->prox;
    return args;
}

static tok_t *arg_expandido(arg_t *a)
{
    if (!a->expandido) a->expandido = expande_lista(a->bruto);
    return a->expandido;
}

static tok_t *marca(const tok_t *modelo)
{
    tok_t *t = tok_novo(T_MARCA, "", modelo);
    return t;
}

/* Põe os tokens de 'a' (até o T_FIM) no fim da lista; vazio vira marca. */
static void poe_arg(lista_t *l, const tok_t *a, const tok_t *modelo, int com_marca)
{
    if (a->tipo == T_FIM) { if (com_marca) lista_poe(l, marca(modelo)); return; }
    for (; a->tipo != T_FIM; a = a->prox) lista_poe(l, tok_copia(a));
}

/* Troca os parâmetros do corpo pelos argumentos (C11 6.10.3.1 a 6.10.3.3). */
static tok_t *substitui(const macro_t *m, arg_t *args)
{
    lista_t l;
    lista_inicia(&l);
    for (const tok_t *t = m->corpo; t->tipo != T_FIM; t = t->prox) {
        int p;
        /* #param */
        if (eh(t, "#") && (p = param_de(m, t->prox)) >= 0) {
            tok_t *s = texto_de(args[p].bruto, t);
            s->espaco = t->espaco;
            lista_poe(&l, s);
            t = t->prox;
            continue;
        }
        /* , ## __VA_ARGS__ (extensão do GCC): sem argumentos, a vírgula
         * some; com eles, nada se cola */
        if (eh(t, ",") && eh(t->prox, "##") && m->va && t->prox->prox->tipo == T_NOME &&
            t->prox->prox->s == m->va) {
            const tok_t *a = args[m->nparams].bruto;
            if (a->tipo != T_FIM) {
                lista_poe(&l, tok_copia(t));
                for (; a->tipo != T_FIM; a = a->prox) lista_poe(&l, tok_copia(a));
            }
            t = t->prox->prox;
            continue;
        }
        /* a ## b */
        if (eh(t, "##")) {
            if (l.ult == &l.cabeca) erro_em(t, "'##' no começo de uma macro");
            const tok_t *dir = t->prox;
            if (dir->tipo == T_FIM) erro_em(t, "'##' no fim de uma macro");
            lista_t d;
            lista_inicia(&d);
            if ((p = param_de(m, dir)) >= 0) poe_arg(&d, args[p].bruto, dir, 1);
            else if (eh(dir, "#") && (p = param_de(m, dir->prox)) >= 0) {
                lista_poe(&d, texto_de(args[p].bruto, dir));
                dir = dir->prox;
            } else lista_poe(&d, tok_copia(dir));
            /* troca o último da lista pelo resultado da colagem */
            tok_t *esq = l.ult, *prim = d.cabeca.prox;
            if (esq->tipo == T_MARCA) {
                *esq = *prim;
                esq->prox = NULL;
            } else if (prim->tipo != T_MARCA) {
                tok_t *c = cola(esq, prim);
                c->hs = esq->hs;
                *esq = *c;
                esq->prox = NULL;
            }
            for (tok_t *r = prim->prox; r; r = r->prox) lista_poe(&l, tok_copia(r));
            t = dir;
            continue;
        }
        /* param ## ... : o argumento entra sem expandir */
        if ((p = param_de(m, t)) >= 0 && eh(t->prox, "##")) {
            const tok_t *a = args[p].bruto;
            if (a->tipo == T_FIM) lista_poe(&l, marca(t));
            else {
                int primeiro = 1;
                for (; a->tipo != T_FIM; a = a->prox) {
                    tok_t *c = tok_copia(a);
                    if (primeiro) c->espaco = t->espaco;
                    primeiro = 0;
                    lista_poe(&l, c);
                }
            }
            continue;
        }
        /* param: o argumento já expandido */
        if (p >= 0) {
            const tok_t *a = arg_expandido(&args[p]);
            int primeiro = 1;
            for (; a->tipo != T_FIM; a = a->prox) {
                tok_t *c = tok_copia(a);
                if (primeiro) c->espaco = t->espaco;
                primeiro = 0;
                lista_poe(&l, c);
            }
            continue;
        }
        lista_poe(&l, tok_copia(t));
    }
    /* tira as marcas */
    lista_t r;
    lista_inicia(&r);
    for (tok_t *t = l.cabeca.prox, *prox; t; t = prox) {
        prox = t->prox;
        if (t->tipo != T_MARCA) lista_poe(&r, t);
    }
    lista_poe(&r, tok_fim(m->corpo));
    return r.cabeca.prox;
}

/* Os tokens da expansão ficam no lugar (arquivo e linha) de quem chamou. */
static tok_t *ajusta(tok_t *corpo, const tok_t *chamada, hideset_t *hs, tok_t *depois)
{
    tok_t *ult = NULL;
    for (tok_t *t = corpo; t->tipo != T_FIM; t = t->prox) {
        t->hs = hs_uniao(t->hs, hs);
        t->arq = chamada->arq;
        t->nome_arq = chamada->nome_arq;
        t->linha = chamada->linha;
        t->inicio = 0;
        t->expandido = 1;
        if (t == corpo) { t->espaco = chamada->espaco; t->inicio = chamada->inicio; }
        ult = t;
    }
    if (!ult) {
        /* expansão vazia: o próximo token herda o começo de linha */
        if (chamada->inicio && depois && depois->tipo != T_FIM && !depois->inicio) {
            tok_t *c = tok_copia(depois);
            c->prox = depois->prox;
            c->inicio = 1;
            c->arq = chamada->arq;
            c->nome_arq = chamada->nome_arq;
            c->linha = chamada->linha;
            return c;
        }
        return depois;
    }
    ult->prox = depois;
    return corpo;
}

/* Se t é uma macro (e pode ser expandida), troca pela expansão: *rest
 * recebe a lista com a expansão na frente. Devolve 1 se expandiu. */
static int expande(tok_t **rest, tok_t *t)
{
    if (t->tipo != T_NOME || hs_tem(t->hs, t->s)) return 0;
    macro_t *m = macro_acha(t->s);
    if (!m) return 0;

    if (m->embutida) {
        tok_t *r = m->embutida(t);
        r->espaco = t->espaco;
        r->inicio = t->inicio;
        r->arq = t->arq;
        r->nome_arq = t->nome_arq;
        r->linha = t->linha;
        r->prox = t->prox;
        *rest = r;
        return 1;
    }
    if (!m->funcao) {
        /* sem parâmetros, mas o ## ainda cola tokens */
        hideset_t *hs = hs_mais(t->hs, m->nome);
        *rest = ajusta(substitui(m, NULL), t, hs, t->prox);
        return 1;
    }
    /* tipo função: só se o próximo token for '(' */
    if (!eh(t->prox, "(")) return 0;
    tok_t *fecha;
    arg_t *args = le_args(rest, t->prox, m, &fecha);
    hideset_t *hs = hs_mais(hs_intersecao(t->hs, fecha->hs), m->nome);
    tok_t *corpo = substitui(m, args);
    *rest = ajusta(corpo, t, hs, *rest);
    return 1;
}

/* ------------------------------- #define --------------------------------- */

static void define(tok_t **rest, tok_t *t)
{
    if (t->tipo != T_NOME) erro_em(t, "#define precisa de um nome");
    macro_t *m = macro_nova(t->s);
    tok_t *nome = t;
    t = t->prox;
    if (!t->inicio && eh(t, "(") && !t->espaco) {
        /* tipo função: os parâmetros */
        m->funcao = 1;
        const char *ps[256];
        int n = 0;
        t = t->prox;
        while (!eh(t, ")")) {
            if (t->inicio) erro_em(nome, "parâmetros de '%s' sem ')'", nome->s);
            if (n > 0) {
                if (!eh(t, ",")) erro_em(t, "esperava ',' entre os parâmetros");
                t = t->prox;
            }
            if (eh(t, "...")) {
                m->va = interno("__VA_ARGS__", 11);
                t = t->prox;
                if (!eh(t, ")")) erro_em(t, "'...' tem que ser o último parâmetro");
                break;
            }
            if (t->tipo != T_NOME) erro_em(t, "parâmetro inválido");
            if (eh(t->prox, "...")) {                /* nome... (extensão do GCC) */
                m->va = t->s;
                t = t->prox->prox;
                if (!eh(t, ")")) erro_em(t, "'...' tem que ser o último parâmetro");
                break;
            }
            if (n == 256) erro_em(t, "parâmetros demais");
            ps[n++] = t->s;
            t = t->prox;
        }
        t = t->prox;
        m->nparams = n;
        m->params = xmalloc(sizeof(char *) * (size_t)(n ? n : 1));
        memcpy(m->params, ps, sizeof(char *) * (size_t)n);
    }
    m->corpo = resto_da_linha(rest, t);
    if (m->corpo->tipo != T_FIM) m->corpo->espaco = 0;
}

/* ---------------------------- #if: expressões ---------------------------- */

typedef struct { u64 v; int sem_sinal; } valor_t;

static const tok_t *pe;          /* o token da expressão sendo lido */
static int nao_avalia;           /* dentro do lado que não conta de || && ?: */

static valor_t ex_cond(void);

static int car_escape(const char **pp)
{
    const char *p = *pp;
    int c = (unsigned char)*p++;
    if (c != '\\') { *pp = p; return c; }
    c = (unsigned char)*p++;
    switch (c) {
    case 'n': c = '\n'; break;
    case 't': c = '\t'; break;
    case 'r': c = '\r'; break;
    case 'a': c = 7; break;
    case 'b': c = 8; break;
    case 'f': c = 12; break;
    case 'v': c = 11; break;
    case 'e': c = 27; break;
    case 'x': {
        int v = 0;
        while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
            v = v * 16 + (*p <= '9' ? *p - '0' : (*p | 32) - 'a' + 10);
            p++;
        }
        c = v;
        break;
    }
    default:
        if (c >= '0' && c <= '7') {
            int v = c - '0';
            for (int k = 0; k < 2 && *p >= '0' && *p <= '7'; k++) v = v * 8 + (*p++ - '0');
            c = v;
        }
    }
    *pp = p;
    return c;
}

static valor_t ex_numero(const tok_t *t)
{
    valor_t r = {0, 0};
    const char *p = t->s;
    if (t->tipo == T_CAR) {
        int largo = *p != '\'';
        while (*p != '\'') p++;
        p++;
        i64 v = 0;
        int n = 0;
        while (*p != '\'') { v = (v << 8) | (car_escape(&p) & 0xFF); n++; }
        if (n == 1 && !largo) v = (signed char)v;       /* char tem sinal no x86 */
        r.v = (u64)v;
        return r;
    }
    int base = 10;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
    else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) { base = 2; p += 2; }
    else if (p[0] == '0') base = 8;
    u64 v = 0;
    for (;; p++) {
        int d;
        if (*p == '\'') continue;                     /* C23: 1'000 */
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (u64)base + (u64)d;
    }
    for (; *p; p++) {
        if (*p == 'u' || *p == 'U') r.sem_sinal = 1;
        else if (*p == 'l' || *p == 'L') continue;
        else erro_em(t, "número inválido em #if: '%s'", t->s);
    }
    if (v > (u64)INT64_MAX) r.sem_sinal = 1;
    r.v = v;
    return r;
}

static void ex_espera(const char *s)
{
    if (!eh(pe, s)) erro_em(pe, "esperava '%s' na expressão do #if", s);
    pe = pe->prox;
}

static valor_t ex_unario(void)
{
    valor_t r = {0, 0};
    if (eh(pe, "(")) { pe = pe->prox; r = ex_cond(); ex_espera(")"); return r; }
    if (eh(pe, "+")) { pe = pe->prox; return ex_unario(); }
    if (eh(pe, "-")) { pe = pe->prox; r = ex_unario(); r.v = (u64)0 - r.v; return r; }
    if (eh(pe, "~")) { pe = pe->prox; r = ex_unario(); r.v = ~r.v; return r; }
    if (eh(pe, "!")) { pe = pe->prox; r = ex_unario(); r.v = !r.v; r.sem_sinal = 0; return r; }
    if (pe->tipo == T_NUM || pe->tipo == T_CAR) { r = ex_numero(pe); pe = pe->prox; return r; }
    if (pe->tipo == T_FIM) erro_em(pe, "expressão do #if incompleta");
    erro_em(pe, "'%s' inválido na expressão do #if", pe->s);
    return r;
}

/* Os operadores binários, do mais fraco ao mais forte (sem ?: , && e ||). */
static const struct { const char *op; int nivel; } ops[] = {
    {"|",1},{"^",2},{"&",3},{"==",4},{"!=",4},{"<",5},{">",5},{"<=",5},{">=",5},
    {"<<",6},{">>",6},{"+",7},{"-",7},{"*",8},{"/",8},{"%",8},
};

static int nivel_de(const tok_t *t)
{
    if (t->tipo != T_SINAL) return 0;
    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++) if (strcmp(t->s, ops[i].op) == 0) return ops[i].nivel;
    return 0;
}

static valor_t ex_bin(int nivel)
{
    valor_t a = nivel > 8 ? ex_unario() : ex_bin(nivel + 1);
    if (nivel > 8) return a;
    while (nivel_de(pe) == nivel) {
        const tok_t *op = pe;
        pe = pe->prox;
        valor_t b = ex_bin(nivel + 1);
        int u = a.sem_sinal || b.sem_sinal;
        i64 x = (i64)a.v, y = (i64)b.v;
        const char *s = op->s;
        valor_t r = { 0, u };
        if (!strcmp(s, "*")) r.v = a.v * b.v;
        else if (!strcmp(s, "/") || !strcmp(s, "%")) {
            if (!b.v) {
                if (!nao_avalia) erro_em(op, "divisão por zero no #if");
                a = r;
                continue;
            }
            if (u) r.v = s[0] == '/' ? a.v / b.v : a.v % b.v;
            else if (y == -1) r.v = s[0] == '/' ? (u64)0 - a.v : 0;
            else r.v = (u64)(s[0] == '/' ? x / y : x % y);
        }
        else if (!strcmp(s, "+")) r.v = a.v + b.v;
        else if (!strcmp(s, "-")) r.v = a.v - b.v;
        else if (!strcmp(s, "<<")) { r.v = b.v >= 64 ? 0 : a.v << b.v; r.sem_sinal = a.sem_sinal; }
        else if (!strcmp(s, ">>")) {
            r.sem_sinal = a.sem_sinal;
            if (b.v >= 64) r.v = a.sem_sinal || x >= 0 ? 0 : (u64)-1;
            else r.v = a.sem_sinal ? a.v >> b.v : (u64)(x >> b.v);
        }
        else {
            int c;
            if (!strcmp(s, "<")) c = u ? a.v < b.v : x < y;
            else if (!strcmp(s, ">")) c = u ? a.v > b.v : x > y;
            else if (!strcmp(s, "<=")) c = u ? a.v <= b.v : x <= y;
            else if (!strcmp(s, ">=")) c = u ? a.v >= b.v : x >= y;
            else if (!strcmp(s, "==")) c = a.v == b.v;
            else if (!strcmp(s, "!=")) c = a.v != b.v;
            else if (!strcmp(s, "&")) { r.v = a.v & b.v; a = r; continue; }
            else if (!strcmp(s, "^")) { r.v = a.v ^ b.v; a = r; continue; }
            else { r.v = a.v | b.v; a = r; continue; }
            r.v = (u64)c;
            r.sem_sinal = 0;
        }
        a = r;
    }
    return a;
}

static valor_t ex_e(void)
{
    valor_t a = ex_bin(1);
    while (eh(pe, "&&")) {
        pe = pe->prox;
        if (!a.v) nao_avalia++;
        valor_t b = ex_bin(1);
        if (!a.v) nao_avalia--;
        a.v = a.v && b.v;
        a.sem_sinal = 0;
    }
    return a;
}

static valor_t ex_ou(void)
{
    valor_t a = ex_e();
    while (eh(pe, "||")) {
        pe = pe->prox;
        if (a.v) nao_avalia++;
        valor_t b = ex_e();
        if (a.v) nao_avalia--;
        a.v = a.v || b.v;
        a.sem_sinal = 0;
    }
    return a;
}

static valor_t ex_cond(void)
{
    valor_t c = ex_ou();
    if (!eh(pe, "?")) return c;
    pe = pe->prox;
    if (!c.v) nao_avalia++;
    valor_t a = ex_cond();
    if (!c.v) nao_avalia--;
    ex_espera(":");
    if (c.v) nao_avalia++;
    valor_t b = ex_cond();
    if (c.v) nao_avalia--;
    valor_t r = c.v ? a : b;
    r.sem_sinal = a.sem_sinal || b.sem_sinal;
    return r;
}

/* ------------------------------- #include -------------------------------- */

#define MAX_PASTAS 64
static const char *pastas_aspas[MAX_PASTAS];   /* -iquote */
static const char *pastas_I[MAX_PASTAS];       /* -I */
static const char *pastas_sis[MAX_PASTAS];     /* -isystem e /usr/include */
static const char *pastas_depois[MAX_PASTAS];  /* -idirafter */
static int naspas, nI, nsis, ndepois;

static int existe(const char *caminho)
{
    FILE *f = fopen(caminho, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static char *junta_caminho(const char *pasta, const char *nome)
{
    texto_t t = {0};
    if (pasta && *pasta) {
        tx_s(&t, pasta);
        if (t.d[t.n - 1] != '/') tx_c(&t, '/');
    }
    tx_s(&t, nome);
    return t.d;
}

static char *procura(const char *nome, int aspas, const tok_t *onde)
{
    if (nome[0] == '/') return existe(nome) ? xstrdup(nome) : NULL;
    if (aspas) {
        /* a pasta do arquivo que inclui */
        const char *de = onde->arq->nome, *barra = strrchr(de, '/');
        char *c = barra ? junta_caminho(xstrndup(de, (size_t)(barra - de)), nome) : xstrdup(nome);
        if (existe(c)) return c;
        for (int i = 0; i < naspas; i++) { c = junta_caminho(pastas_aspas[i], nome); if (existe(c)) return c; }
    }
    const char **listas[3] = { pastas_I, pastas_sis, pastas_depois };
    int ns[3] = { nI, nsis, ndepois };
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < ns[k]; i++) {
            char *c = junta_caminho(listas[k][i], nome);
            if (existe(c)) return c;
        }
    return NULL;
}

/* O nome do cabeçalho: "x.h", <x.h> ou uma macro que vira um dos dois. */
static char *nome_cabecalho(tok_t **rest, tok_t *t, int *aspas, tok_t **onde)
{
    *onde = t;
    if (t->tipo == T_STR && t->s[0] == '"') {
        *aspas = 1;
        *rest = pula_linha(t->prox, 1);
        return xstrndup(t->s + 1, strlen(t->s) - 2);
    }
    tok_t *linha = resto_da_linha(rest, t);
    if (!eh(linha, "<")) linha = expande_lista(linha);
    if (linha->tipo == T_STR && linha->s[0] == '"') {
        *aspas = 1;
        return xstrndup(linha->s + 1, strlen(linha->s) - 2);
    }
    if (!eh(linha, "<")) erro_em(t, "esperava \"arquivo\" ou <arquivo> no #include");
    texto_t n = {0};
    for (tok_t *u = linha->prox; !eh(u, ">"); u = u->prox) {
        if (u->tipo == T_FIM) erro_em(t, "<arquivo> sem '>'");
        if (u != linha->prox && u->espaco) tx_c(&n, ' ');
        tx_s(&n, u->s);
    }
    *aspas = 0;
    return n.d ? n.d : xstrdup("");
}

static int profundidade;

static tok_t *inclui(const char *caminho, tok_t *depois, const tok_t *onde)
{
    for (int i = 0; i < narquivos; i++)
        if (arquivos[i]->uma_vez && strcmp(arquivos[i]->nome, caminho) == 0) return depois;
    char *texto = le_arquivo(caminho);
    if (!texto) erro_em(onde, "não consegui ler '%s'", caminho);
    if (++profundidade > 200) erro_em(onde, "#include aninhado demais (laço?)");
    arquivo_t *a = arquivo_de(caminho, texto);
    tok_t *t = tokenizar(a);
    /* o T_FIM do arquivo fica: ele marca a volta ao arquivo de fora */
    tok_t *u = t;
    while (u->tipo != T_FIM) u = u->prox;
    u->tipo = T_MARCA;                /* o fim de um #include */
    u->prox = depois;
    return t;
}

/* ----------------------------- Condicionais ------------------------------ */

typedef struct cond {
    int ja_valeu;         /* algum ramo deste #if já foi usado */
    int em_else;
    const tok_t *onde;
    struct cond *prox;
} cond_t;

static cond_t *conds;

static void cond_empilha(const tok_t *onde, int valeu)
{
    cond_t *c = xmalloc(sizeof *c);
    c->ja_valeu = valeu;
    c->onde = onde;
    c->prox = conds;
    conds = c;
}

static int eh_diretiva(const tok_t *t) { return t->inicio && !t->expandido && eh(t, "#"); }

/* Pula um bloco que não vale: até o #elif/#else/#endif deste nível. */
static tok_t *pula_bloco(tok_t *t)
{
    int nivel = 0;
    while (t->tipo != T_FIM) {
        if (eh_diretiva(t)) {
            const tok_t *d = t->prox;
            if (!d->inicio && (eh(d, "if") || eh(d, "ifdef") || eh(d, "ifndef"))) nivel++;
            else if (!d->inicio && eh(d, "endif")) {
                if (nivel == 0) return t;
                nivel--;
            } else if (!d->inicio && nivel == 0 && (eh(d, "elif") || eh(d, "else"))) return t;
        }
        t = t->prox;
    }
    return t;
}

static int contador;

/* defined X, defined(X) e __has_include(...) viram 1 ou 0, antes de expandir. */
static tok_t *le_condicao(tok_t **rest, tok_t *t)
{
    tok_t *linha = resto_da_linha(rest, t);
    lista_t l;
    lista_inicia(&l);
    for (tok_t *u = linha; u->tipo != T_FIM; u = u->prox) {
        if (eh(u, "defined")) {
            tok_t *inicio = u;
            int par = eh(u->prox, "(");
            u = par ? u->prox->prox : u->prox;
            if (u->tipo != T_NOME) erro_em(inicio, "defined precisa de um nome");
            int def = macro_acha(u->s) != NULL;
            if (par) { u = u->prox; if (!eh(u, ")")) erro_em(inicio, "defined( sem ')'"); }
            lista_poe(&l, tok_novo(T_NUM, def ? "1" : "0", inicio));
            continue;
        }
        if (eh(u, "__has_include")) {
            tok_t *inicio = u;
            if (!eh(u->prox, "(")) erro_em(u, "__has_include precisa de '('");
            u = u->prox->prox;
            int aspas;
            texto_t n = {0};
            if (u->tipo == T_STR) { aspas = 1; tx_poe(&n, u->s + 1, strlen(u->s) - 2); u = u->prox; }
            else {
                if (!eh(u, "<")) erro_em(inicio, "__has_include(\"x\") ou __has_include(<x>)");
                aspas = 0;
                for (u = u->prox; !eh(u, ">"); u = u->prox) {
                    if (u->tipo == T_FIM) erro_em(inicio, "<arquivo> sem '>'");
                    tx_s(&n, u->s);
                }
                u = u->prox;
            }
            if (!eh(u, ")")) erro_em(inicio, "__has_include sem ')'");
            int tem = n.d && procura(n.d, aspas, inicio) != NULL;
            lista_poe(&l, tok_novo(T_NUM, tem ? "1" : "0", inicio));
            continue;
        }
        lista_poe(&l, tok_copia(u));
    }
    lista_poe(&l, tok_fim(t));
    tok_t *e = expande_lista(l.cabeca.prox);
    /* os nomes que sobraram valem 0 */
    for (tok_t *u = e; u->tipo != T_FIM; u = u->prox)
        if (u->tipo == T_NOME) { u->tipo = T_NUM; u->s = "0"; }
    return e;
}

static int avalia(tok_t **rest, tok_t *t)
{
    const tok_t *inicio = t;
    tok_t *e = le_condicao(rest, t);
    if (e->tipo == T_FIM) erro_em(inicio, "#if sem expressão");
    pe = e;
    valor_t v = ex_cond();
    if (pe->tipo != T_FIM) erro_em(pe, "lixo no fim da expressão do #if: '%s'", pe->s);
    return v.v != 0;
}

/* ------------------------------ Diretivas -------------------------------- */

static tok_t *diretiva(tok_t *hash, tok_t **pragma)
{
    tok_t *t = hash->prox;
    *pragma = NULL;
    if (t->inicio) return t;                         /* # sozinho */
    tok_t *rest;

    if (eh(t, "include")) {
        int aspas;
        tok_t *onde;
        char *nome = nome_cabecalho(&rest, t->prox, &aspas, &onde);
        char *c = procura(nome, aspas, hash);
        if (!c) erro_em(onde, "não achei o cabeçalho '%s'", nome);
        return inclui(c, rest, hash);
    }
    if (eh(t, "define")) { define(&rest, t->prox); return rest; }
    if (eh(t, "undef")) {
        t = t->prox;
        if (t->tipo != T_NOME) erro_em(t, "#undef precisa de um nome");
        macro_apaga(t->s);
        return pula_linha(t->prox, 1);
    }
    if (eh(t, "if")) {
        int v = avalia(&rest, t->prox);
        cond_empilha(hash, v);
        return v ? rest : pula_bloco(rest);
    }
    if (eh(t, "ifdef") || eh(t, "ifndef")) {
        tok_t *n = t->prox;
        if (n->tipo != T_NOME) erro_em(t, "#%s precisa de um nome", t->s);
        int v = (macro_acha(n->s) != NULL) == eh(t, "ifdef");
        cond_empilha(hash, v);
        rest = pula_linha(n->prox, 1);
        return v ? rest : pula_bloco(rest);
    }
    if (eh(t, "elif")) {
        if (!conds || conds->em_else) erro_em(t, "#elif sem #if");
        if (conds->ja_valeu) { resto_da_linha(&rest, t->prox); return pula_bloco(rest); }
        int v = avalia(&rest, t->prox);
        conds->ja_valeu = v;
        return v ? rest : pula_bloco(rest);
    }
    if (eh(t, "else")) {
        if (!conds || conds->em_else) erro_em(t, "#else sem #if");
        conds->em_else = 1;
        rest = pula_linha(t->prox, 1);
        if (conds->ja_valeu) return pula_bloco(rest);
        conds->ja_valeu = 1;
        return rest;
    }
    if (eh(t, "endif")) {
        if (!conds) erro_em(t, "#endif sem #if");
        conds = conds->prox;
        return pula_linha(t->prox, 1);
    }
    if (eh(t, "line")) {
        tok_t *linha = expande_lista(resto_da_linha(&rest, t->prox));
        if (linha->tipo != T_NUM) erro_em(t, "#line precisa de um número");
        int nova = atoi(linha->s);
        const char *nome = NULL;
        if (linha->prox->tipo == T_STR) nome = xstrndup(linha->prox->s + 1, strlen(linha->prox->s) - 2);
        /* os tokens seguintes deste arquivo passam a contar daqui */
        int delta = nova - (rest->linha);
        for (tok_t *u = rest; u->tipo != T_FIM && u->tipo != T_MARCA && u->arq == hash->arq; u = u->prox) {
            u->linha += delta;
            if (nome) u->nome_arq = nome;
        }
        return rest;
    }
    if (eh(t, "error")) {
        texto_t m = {0};
        for (tok_t *u = t->prox; !u->inicio; u = u->prox) {
            if (m.n) tx_c(&m, ' ');
            tx_s(&m, u->s);
        }
        erro_em(t, "#error %s", m.d ? m.d : "");
    }
    if (eh(t, "warning")) {
        texto_t m = {0};
        for (tok_t *u = t->prox; !u->inicio; u = u->prox) {
            if (m.n) tx_c(&m, ' ');
            tx_s(&m, u->s);
        }
        aviso_em(t, "#warning %s", m.d ? m.d : "");
        return pula_linha(t->prox, 0);
    }
    if (eh(t, "pragma")) {
        if (eh(t->prox, "once") && t->prox->prox->inicio) {
            hash->arq->uma_vez = 1;
            return t->prox->prox;
        }
        *pragma = hash;
        return pula_linha(t->prox, 0);
    }
    if (t->tipo == T_NUM) {                          /* # 12 "arquivo" (saída de um cpp) */
        return pula_linha(t, 0);
    }
    erro_em(t, "diretiva desconhecida: #%s", t->s);
    return NULL;
}

/* ------------------------------- A saída --------------------------------- */

static FILE *saida;
static int marcadores = 1;
static const char *saida_arq;
static int saida_linha;
static int linha_vazia = 1;

static void nova_linha(const tok_t *t)
{
    int salto = t->linha - saida_linha;
    if (marcadores && (t->nome_arq != saida_arq || salto < 0 || salto > 8)) {
        if (!linha_vazia) fputc('\n', saida);
        fprintf(saida, "# %d \"%s\"\n", t->linha, t->nome_arq);
        saida_arq = t->nome_arq;
    } else if (!linha_vazia || salto > 0) {
        if (salto <= 0) salto = 1;
        for (int i = 0; i < salto; i++) fputc('\n', saida);
    }
    saida_arq = t->nome_arq;
    saida_linha = t->linha;
    linha_vazia = 1;
}

static void escreve(const tok_t *t)
{
    if (t->inicio || saida_arq == NULL) nova_linha(t);
    /* Entre tokens vai sempre um espaço: assim dois tokens vizinhos nunca
     * se colam por engano (como "-" "-" virando "--"). */
    if (!linha_vazia) fputc(' ', saida);
    fputs(t->s, saida);
    linha_vazia = 0;
}

static void escreve_pragma(const tok_t *hash)
{
    nova_linha(hash);
    fputc('#', saida);
    for (const tok_t *u = hash->prox; !u->inicio; u = u->prox) {
        fputc(' ', saida);
        fputs(u->s, saida);
    }
    linha_vazia = 0;
}

/* Percorre a lista: diretivas, expansão de macros e saída. */
static void processa(tok_t *t)
{
    while (t->tipo != T_FIM) {
        if (t->tipo == T_MARCA) {              /* fim de um #include */
            profundidade--;
            t = t->prox;
            continue;
        }
        if (eh_diretiva(t)) {
            tok_t *pragma;
            t = diretiva(t, &pragma);
            if (pragma) escreve_pragma(pragma);
            continue;
        }
        if (expande(&t, t)) continue;
        escreve(t);
        t = t->prox;
    }
    if (conds) erro_em(conds->onde, "#if sem #endif");
}

/* ----------------------------- Embutidas --------------------------------- */

static tok_t *m_file(const tok_t *t)
{
    texto_t s = {0};
    tx_c(&s, '"');
    escapa(&s, t->nome_arq);
    tx_c(&s, '"');
    return tok_novo(T_STR, s.d, t);
}

static tok_t *m_line(const tok_t *t)
{
    char b[32];
    snprintf(b, sizeof b, "%d", t->linha);
    return tok_novo(T_NUM, xstrdup(b), t);
}

static tok_t *m_counter(const tok_t *t)
{
    char b[32];
    snprintf(b, sizeof b, "%d", contador++);
    return tok_novo(T_NUM, xstrdup(b), t);
}

static void embutida(const char *nome, embutida_t f)
{
    macro_t *m = macro_nova(interno(nome, strlen(nome)));
    m->embutida = f;
}

/* Uma definição em texto, como se viesse de um arquivo. */
static void define_texto(const char *nome, const char *def)
{
    texto_t t = {0};
    tx_s(&t, def);
    tx_c(&t, '\n');
    arquivo_t *a = arquivo_de(nome, t.d);
    processa(tokenizar(a));
}

/* -D nome  ->  #define nome 1;   -D nome=valor  ->  #define nome valor */
static void opcao_D(const char *arg)
{
    texto_t t = {0};
    tx_s(&t, "#define ");
    const char *igual = strchr(arg, '=');
    if (igual) {
        tx_poe(&t, arg, (size_t)(igual - arg));
        tx_c(&t, ' ');
        tx_s(&t, igual + 1);
    } else {
        tx_s(&t, arg);
        tx_s(&t, " 1");
    }
    define_texto("<linha de comando>", t.d);
}

static void opcao_U(const char *arg)
{
    macro_apaga(interno(arg, strlen(arg)));
}

/* --------------------------------- main ---------------------------------- */

static void uso(void)
{
    fprintf(stderr, "uso: nefs-cpp [-o saída] [-I pasta] [-D nome[=valor]] [-U nome] [-P]\n"
                    "                [-nostdinc] [-undef] [-std=c11] [-include arquivo] [entrada.c]\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *entrada = "-", *nome_saida = NULL;
    const char *std = "c11";
    int nostdinc = 0, undef = 0;

    /* 1ª volta: opções que não dependem da ordem */
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
#define PASTA(lista, n) do { if (n < MAX_PASTAS) lista[n++] = v; i++; } while (0)
        if (!strcmp(a, "-o") && v) { nome_saida = v; i++; }
        else if (!strcmp(a, "-I") && v) PASTA(pastas_I, nI);
        else if (!strncmp(a, "-I", 2)) { if (nI < MAX_PASTAS) pastas_I[nI++] = a + 2; }
        else if (!strcmp(a, "-iquote") && v) PASTA(pastas_aspas, naspas);
        else if (!strcmp(a, "-isystem") && v) PASTA(pastas_sis, nsis);
        else if (!strcmp(a, "-idirafter") && v) PASTA(pastas_depois, ndepois);
        else if (!strcmp(a, "-nostdinc")) nostdinc = 1;
        else if (!strcmp(a, "-undef")) undef = 1;
        else if (!strncmp(a, "-std=", 5)) std = a + 5;
        else if (!strcmp(a, "-P")) marcadores = 0;
        else if ((!strcmp(a, "-D") || !strcmp(a, "-U") || !strcmp(a, "-include")) && v) i++;
        else if (!strncmp(a, "-D", 2) || !strncmp(a, "-U", 2)) continue;
        else if (!strncmp(a, "-M", 2)) { fprintf(stderr, "nefs-cpp: %s não é suportado\n", a); return 2; }
        else if (a[0] == '-' && a[1]) uso();
        else entrada = a;
#undef PASTA
    }
    if (!nostdinc && nsis < MAX_PASTAS) pastas_sis[nsis++] = "/usr/include";

    saida = stdout;
    if (nome_saida && strcmp(nome_saida, "-") != 0) {
        saida = fopen(nome_saida, "w");
        if (!saida) { fprintf(stderr, "nefs-cpp: não consegui criar '%s'\n", nome_saida); return 1; }
    }

    /* As macros do padrão e as do sistema. */
    embutida("__FILE__", m_file);
    embutida("__LINE__", m_line);
    embutida("__COUNTER__", m_counter);
    long versao = 201112L;
    if (!strcmp(std, "c99") || !strcmp(std, "gnu99")) versao = 199901L;
    else if (!strcmp(std, "c17") || !strcmp(std, "c18") || !strcmp(std, "gnu17")) versao = 201710L;
    else if (!strcmp(std, "c2x") || !strcmp(std, "c23")) versao = 202311L;
    char def[128];
    snprintf(def, sizeof def, "#define __STDC_VERSION__ %ldL", versao);
    define_texto("<embutida>", "#define __STDC__ 1");
    define_texto("<embutida>", "#define __STDC_HOSTED__ 1");
    define_texto("<embutida>", def);
    define_texto("<embutida>", "#define __DATE__ \"Jan  1 1970\"");   /* fixos: o mesmo */
    define_texto("<embutida>", "#define __TIME__ \"00:00:00\"");      /* código, a mesma saída */
    if (!undef) {
        static const char *const sistema[] = {
            "__x86_64__", "__amd64__", "__LP64__", "__novaera__", "__NovaEra__",
        };
        for (size_t i = 0; i < sizeof sistema / sizeof sistema[0]; i++) {
            snprintf(def, sizeof def, "#define %s 1", sistema[i]);
            define_texto("<embutida>", def);
        }
    }

    /* 2ª volta: -D, -U e -include, na ordem em que vieram */
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((!strcmp(a, "-o") || !strcmp(a, "-I") || !strcmp(a, "-iquote") || !strcmp(a, "-isystem") ||
             !strcmp(a, "-idirafter")) && i + 1 < argc) { i++; continue; }
        if (!strcmp(a, "-D") && i + 1 < argc) opcao_D(argv[++i]);
        else if (!strncmp(a, "-D", 2)) opcao_D(a + 2);
        else if (!strcmp(a, "-U") && i + 1 < argc) opcao_U(argv[++i]);
        else if (!strncmp(a, "-U", 2) && a[2]) opcao_U(a + 2);
        else if (!strcmp(a, "-include") && i + 1 < argc) {
            const char *n = argv[++i];
            char *texto = le_arquivo(n);
            if (!texto) { fprintf(stderr, "nefs-cpp: não consegui ler '%s'\n", n); return 1; }
            processa(tokenizar(arquivo_de(n, texto)));
        }
    }

    char *texto = le_arquivo(entrada);
    if (!texto) { fprintf(stderr, "nefs-cpp: não consegui ler '%s'\n", entrada); return 1; }
    arquivo_t *a = arquivo_de(strcmp(entrada, "-") == 0 ? "<stdin>" : entrada, texto);
    saida_arq = NULL;
    processa(tokenizar(a));
    if (!linha_vazia) fputc('\n', saida);
    if (fflush(saida) != 0 || ferror(saida)) { fprintf(stderr, "nefs-cpp: erro ao gravar\n"); return 1; }
    if (saida != stdout) fclose(saida);
    return 0;
}
