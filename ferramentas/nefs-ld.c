/*
 * ===========================================================================
 *  NEFS - ferramentas/nefs-ld.c
 *  O ligador (linker) do NEFS: junta objetos ELF64 (.o) e bibliotecas (.a)
 *  num programa do NovaEra, ou no próprio núcleo.
 *
 *    nefs-ld [opções] arquivos.o biblioteca.a ... [-lnome]
 *
 *      -o saída        o arquivo gerado (padrão: a.out)
 *      -base endereço  onde o programa começa na memória (padrão: 0x40000000,
 *                      USUARIO_BASE; o núcleo usa 0x100000)
 *      -binario        grava a memória pura, sem cabeçalho ELF (o kernel.bin)
 *      -inicio seção   põe esta seção bem no começo (o .multiboot do núcleo)
 *      -e símbolo      o ponto de entrada (padrão: _start)
 *      -L pasta        onde procurar as bibliotecas de -lnome
 *      -lnome          a biblioteca libnome.a
 *      -mapa           mostra onde ficou cada seção e cada símbolo
 *
 *  A memória fica assim (a mesma ordem de linker.ld e usuario/usuario.ld):
 *
 *      base   [-inicio] .text ...      código
 *      4 KiB  .rodata ...              constantes
 *      4 KiB  .data ... .got           variáveis com valor inicial
 *             _carga_fim               (até aqui vem do arquivo)
 *      4 KiB  .bss ...                 variáveis zeradas
 *             _kernel_fim, _fim        (o fim de tudo)
 *
 *  Das bibliotecas, só entram os objetos que definem algum símbolo que
 *  falta, como no ld do Unix. As seções que não vão para a memória
 *  (comentários, .note, .eh_frame) ficam de fora.
 *
 *  Escrito em C padrão: compila no Linux e dentro do próprio NovaEra.
 * ===========================================================================
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t  i64;

static void erro(const char *msg, const char *detalhe)
{
    fprintf(stderr, "nefs-ld: %s%s%s\n", msg, detalhe ? ": " : "", detalhe ? detalhe : "");
    exit(1);
}

static void *xmalloc(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    if (!p) erro("sem memória", 0);
    return p;
}

static char *xstrdup(const char *s)
{
    char *d = xmalloc(strlen(s) + 1);
    strcpy(d, s);
    return d;
}

static u64 le16(const u8 *p) { return (u64)p[0] | (u64)p[1] << 8; }
static u64 le32(const u8 *p) { return le16(p) | le16(p + 2) << 16; }
static u64 le64(const u8 *p) { return le32(p) | le32(p + 4) << 32; }

static void poe(u8 *p, u64 v, int tam)
{
    for (int i = 0; i < tam; i++) p[i] = (u8)(v >> (8 * i));
}

static u8 *le_arquivo(const char *nome, size_t *tam)
{
    FILE *f = fopen(nome, "rb");
    if (!f) erro("não consegui abrir", nome);
    size_t cap = 65536, n = 0, k;
    u8 *d = xmalloc(cap);
    while ((k = fread(d + n, 1, cap - n, f)) > 0) {
        n += k;
        if (n == cap) {
            cap *= 2;
            d = realloc(d, cap);
            if (!d) erro("sem memória", 0);
        }
    }
    fclose(f);
    *tam = n;
    return d;
}

/* ------------------------------ Objetos --------------------------------- */

#define SHT_PROGBITS 1u
#define SHT_SYMTAB   2u
#define SHT_RELA     4u
#define SHT_NOBITS   8u
#define SHF_WRITE    0x1u
#define SHF_ALLOC    0x2u
#define SHF_EXEC     0x4u
#define SHF_TLS      0x400u
#define SHN_UNDEF    0u
#define SHN_ABS      0xFFF1u
#define SHN_COMMON   0xFFF2u
#define STB_LOCAL    0
#define STB_WEAK     2
#define STT_SECTION  3

/* As classes de seção, na ordem em que vão para a memória. */
enum { C_FORA, C_INICIO, C_TEXTO, C_RODATA, C_DADOS, C_BSS, NCLASSES };

typedef struct {
    const char *nome;
    u32  tipo;
    u64  flags, alinh, tam;
    const u8 *dados;
    int  classe;
    u64  ender;               /* na memória, depois da disposição */
    const u8 *rela;           /* as relocações desta seção */
    u64  nrela;
} sec_t;

typedef struct {
    const char *nome;
    int  ligacao, tipo;
    u32  shndx;
    u64  valor, tam;
    struct global *g;         /* símbolos globais: a definição escolhida */
} sym_t;

typedef struct obj {
    char  *nome;
    const u8 *d;
    size_t tam;
    u32    nsec;
    sec_t *sec;
    u32    nsym;
    sym_t *sym;
    int    incluido;
    struct obj *prox;
} obj_t;

typedef struct global {
    const char *nome;
    obj_t *obj;               /* quem define (0 = ninguém ainda) */
    u32    idx;
    int    fraco;             /* a definição é fraca (weak) */
    int    pedido;            /* alguém precisa dele (referência forte) */
    u64    comum_tam, comum_alinh;   /* COMMON sem definição */
    u64    comum_ender;
    int    interno;           /* definido pelo ligador (_fim...) */
    u64    valor_interno;
    int    got;               /* tem posição na GOT; 1 + índice */
    struct global *prox;
} global_t;

#define NTAB 4096u
static global_t *tabela[NTAB];
static obj_t    *objs, *objs_ult;

static u32 hash(const char *s)
{
    u32 h = 2166136261u;
    while (*s) h = (h ^ (u8)*s++) * 16777619u;
    return h % NTAB;
}

typedef struct no_g { global_t *g; struct no_g *prox; } no_g_t;
static no_g_t *ordem, *ordem_ult;

static global_t *global(const char *nome)
{
    u32 h = hash(nome);
    for (global_t *g = tabela[h]; g; g = g->prox)
        if (strcmp(g->nome, nome) == 0) return g;
    global_t *g = xmalloc(sizeof *g);
    g->nome = nome;
    g->prox = tabela[h];
    tabela[h] = g;
    no_g_t *n = xmalloc(sizeof *n);
    n->g = g;
    if (ordem_ult) ordem_ult->prox = n; else ordem = n;
    ordem_ult = n;
    return g;
}

/* Lê os cabeçalhos de um objeto ELF64 relocável. */
static obj_t *obj_le(char *nome, const u8 *d, size_t tam)
{
    if (tam < 64 || memcmp(d, "\177ELF", 4) != 0 || d[4] != 2 || d[5] != 1)
        erro("não é um objeto ELF de 64 bits", nome);
    if (le16(d + 16) != 1) erro("não é um objeto relocável (.o)", nome);
    if (le16(d + 18) != 62) erro("não é x86-64", nome);
    obj_t *o = xmalloc(sizeof *o);
    o->nome = nome;
    o->d = d;
    o->tam = tam;
    u64 shoff = le64(d + 40);
    o->nsec = (u32)le16(d + 60);
    u32 shstr = (u32)le16(d + 62);
    if (shoff + (u64)o->nsec * 64 > tam) erro("objeto truncado", nome);
    o->sec = xmalloc(sizeof(sec_t) * o->nsec);
    const u8 *sh_str = d + le64(d + shoff + (u64)shstr * 64 + 24);

    u32 symtab = 0;
    for (u32 i = 0; i < o->nsec; i++) {
        const u8 *h = d + shoff + (u64)i * 64;
        sec_t *s = &o->sec[i];
        s->nome  = (const char *)sh_str + le32(h);
        s->tipo  = (u32)le32(h + 4);
        s->flags = le64(h + 8);
        s->tam   = le64(h + 32);
        s->alinh = le64(h + 48) ? le64(h + 48) : 1;
        s->dados = d + le64(h + 24);
        if (s->tipo == SHT_SYMTAB) symtab = i;
        if (s->tipo != SHT_NOBITS && le64(h + 24) + s->tam > tam) erro("seção fora do arquivo", nome);
    }
    /* as relocações de cada seção */
    for (u32 i = 0; i < o->nsec; i++) {
        const u8 *h = d + shoff + (u64)i * 64;
        if (o->sec[i].tipo != SHT_RELA) continue;
        u32 alvo = (u32)le32(h + 44);
        if (alvo < o->nsec) {
            o->sec[alvo].rela = o->sec[i].dados;
            o->sec[alvo].nrela = o->sec[i].tam / 24;
        }
    }
    /* os símbolos */
    if (symtab) {
        const u8 *h = d + shoff + (u64)symtab * 64;
        u32 link = (u32)le32(h + 40);
        const u8 *strs = o->sec[link].dados;
        o->nsym = (u32)(o->sec[symtab].tam / 24);
        o->sym = xmalloc(sizeof(sym_t) * (o->nsym ? o->nsym : 1));
        for (u32 i = 0; i < o->nsym; i++) {
            const u8 *e = o->sec[symtab].dados + (u64)i * 24;
            sym_t *s = &o->sym[i];
            s->nome    = (const char *)strs + le32(e);
            s->ligacao = e[4] >> 4;
            s->tipo    = e[4] & 15;
            s->shndx   = (u32)le16(e + 6);
            s->valor   = le64(e + 8);
            s->tam     = le64(e + 16);
        }
    }
    return o;
}

/* Este objeto define algum símbolo que alguém pediu e ninguém tem? */
static int obj_resolve_algo(obj_t *o)
{
    for (u32 i = 1; i < o->nsym; i++) {
        sym_t *s = &o->sym[i];
        if (s->ligacao == STB_LOCAL || s->shndx == SHN_UNDEF || s->shndx == SHN_COMMON) continue;
        u32 h = hash(s->nome);
        for (global_t *g = tabela[h]; g; g = g->prox)
            if (strcmp(g->nome, s->nome) == 0) {
                if (g->pedido && !g->obj && !g->interno) return 1;
                break;
            }
    }
    return 0;
}

/* Inclui o objeto: os símbolos globais dele entram na tabela. */
static void obj_inclui(obj_t *o)
{
    o->incluido = 1;
    if (objs_ult) objs_ult->prox = o; else objs = o;
    objs_ult = o;
    for (u32 i = 1; i < o->nsym; i++) {
        sym_t *s = &o->sym[i];
        if (s->ligacao == STB_LOCAL) continue;
        global_t *g = global(s->nome);
        s->g = g;
        if (s->shndx == SHN_UNDEF) {
            if (s->ligacao != STB_WEAK) g->pedido = 1;
        } else if (s->shndx == SHN_COMMON) {
            if (s->tam > g->comum_tam) g->comum_tam = s->tam;
            if (s->valor > g->comum_alinh) g->comum_alinh = s->valor;
        } else if (!g->obj || (g->fraco && s->ligacao != STB_WEAK)) {
            g->obj = o;
            g->idx = i;
            g->fraco = s->ligacao == STB_WEAK;
        } else if (!g->fraco && s->ligacao != STB_WEAK) {
            fprintf(stderr, "nefs-ld: '%s' definido em %s e em %s\n", s->nome, g->obj->nome, o->nome);
            exit(1);
        }
    }
}

/* ----------------------------- Bibliotecas ------------------------------ */

typedef struct biblio {
    obj_t **membros;
    u32     n;
    struct biblio *prox;
} biblio_t;

static biblio_t *biblios, *biblios_ult;

/* Formato "ar" do Unix: "!<arch>\n" e, para cada membro, um cabeçalho de
 * 60 bytes (nome, data, dono, modo, tamanho) e os dados. Os nomes longos
 * ficam no membro "//"; o índice de símbolos ("/") não é necessário. */
static void biblio_le(char *nome, const u8 *d, size_t tam)
{
    biblio_t *b = xmalloc(sizeof *b);
    u32 cap = 16;
    b->membros = xmalloc(sizeof(obj_t *) * cap);
    const char *nomes_longos = 0;
    size_t pos = 8;
    while (pos + 60 <= tam) {
        const u8 *h = d + pos;
        char tam_txt[11];
        memcpy(tam_txt, h + 48, 10);
        tam_txt[10] = '\0';
        size_t n = (size_t)strtoul(tam_txt, 0, 10);
        const u8 *dados = h + 60;
        if (pos + 60 + n > tam) erro("biblioteca truncada", nome);
        char membro[256];
        if (h[0] == '/' && h[1] == '/') nomes_longos = (const char *)dados;
        else if (h[0] == '/' && (h[1] == ' ' || h[1] == '\0')) { /* índice de símbolos */ }
        else {
            int k = 0;
            if (h[0] == '/' && nomes_longos) {
                const char *l = nomes_longos + strtoul((const char *)h + 1, 0, 10);
                while (*l && *l != '/' && *l != '\n' && k < 255) membro[k++] = *l++;
            } else {
                while (k < 16 && h[k] != '/' && h[k] != ' ') { membro[k] = (char)h[k]; k++; }
            }
            membro[k] = '\0';
            char *completo = xmalloc(strlen(nome) + strlen(membro) + 3);
            sprintf(completo, "%s(%s)", nome, membro);
            if (b->n == cap) {
                cap *= 2;
                obj_t **m = xmalloc(sizeof(obj_t *) * cap);
                memcpy(m, b->membros, sizeof(obj_t *) * b->n);
                b->membros = m;
            }
            b->membros[b->n++] = obj_le(completo, dados, n);
        }
        pos += 60 + n + (n & 1);
    }
    if (biblios_ult) biblios_ult->prox = b; else biblios = b;
    biblios_ult = b;
}

/* Puxa das bibliotecas o que falta, até nada mais mudar. */
static void biblios_resolve(void)
{
    int mudou;
    do {
        mudou = 0;
        for (biblio_t *b = biblios; b; b = b->prox)
            for (u32 i = 0; i < b->n; i++)
                if (!b->membros[i]->incluido && obj_resolve_algo(b->membros[i])) {
                    obj_inclui(b->membros[i]);
                    mudou = 1;
                }
    } while (mudou);
}

/* ----------------------------- Disposição ------------------------------- */

static const char *secao_inicio;
static u64 base = 0x40000000u;
static u64 ender_classe[NCLASSES], fim_classe[NCLASSES];
static u64 got_ender, ngot, carga_fim, bss_ini, fim;

static int comeca(const char *s, const char *pre) { return strncmp(s, pre, strlen(pre)) == 0; }

static int classe_de(const sec_t *s)
{
    if (!(s->flags & SHF_ALLOC)) return C_FORA;
    if (comeca(s->nome, ".note") || comeca(s->nome, ".eh_frame") || strcmp(s->nome, ".comment") == 0)
        return C_FORA;
    if (s->flags & SHF_TLS) erro("variáveis por thread (TLS) não são suportadas", s->nome);
    if (secao_inicio && strcmp(s->nome, secao_inicio) == 0) return C_INICIO;
    if (s->tipo == SHT_NOBITS) return C_BSS;
    if (s->flags & SHF_EXEC) return C_TEXTO;
    if (s->flags & SHF_WRITE) return C_DADOS;
    return C_RODATA;
}

static u64 alinha(u64 v, u64 a) { return (v + a - 1) & ~(a - 1); }

static void dispor(void)
{
    u64 end = base;
    for (int c = C_INICIO; c < NCLASSES; c++) {
        if (c == C_BSS) carga_fim = end;         /* até aqui vem do arquivo */
        if (c == C_RODATA || c == C_DADOS || c == C_BSS) end = alinha(end, 4096);
        ender_classe[c] = end;
        if (c == C_BSS) bss_ini = end;
        for (obj_t *o = objs; o; o = o->prox)
            for (u32 i = 1; i < o->nsec; i++) {
                sec_t *s = &o->sec[i];
                if (s->classe != c) continue;
                end = alinha(end, s->alinh);
                s->ender = end;
                end += s->tam;
            }
        if (c == C_DADOS && ngot) {              /* a GOT, no fim dos dados */
            end = alinha(end, 8);
            got_ender = end;
            end += 8 * ngot;
        }
        if (c == C_BSS) {                        /* os símbolos COMMON */
            for (no_g_t *n = ordem; n; n = n->prox) {
                global_t *g = n->g;
                if (g->obj || g->interno || !g->comum_tam) continue;
                end = alinha(end, g->comum_alinh ? g->comum_alinh : 1);
                g->comum_ender = end;
                end += g->comum_tam;
            }
        }
        fim_classe[c] = end;
    }
    fim = end;
}

/* Os símbolos que o próprio ligador define. */
static const char *const internos[] = {
    "_carga_fim", "_bss_ini", "_kernel_fim", "_fim", "_GLOBAL_OFFSET_TABLE_",
};

static u64 valor_interno(const char *nome)
{
    if (strcmp(nome, "_carga_fim") == 0) return carga_fim;
    if (strcmp(nome, "_bss_ini") == 0) return bss_ini;
    if (strcmp(nome, "_GLOBAL_OFFSET_TABLE_") == 0) return got_ender;
    return fim;                                  /* _kernel_fim, _fim */
}

/* ----------------------------- Relocações ------------------------------- */

#define R_X86_64_NONE           0
#define R_X86_64_64             1
#define R_X86_64_PC32           2
#define R_X86_64_PLT32          4
#define R_X86_64_GOTPCREL       9
#define R_X86_64_32            10
#define R_X86_64_32S           11
#define R_X86_64_16            12
#define R_X86_64_PC16          13
#define R_X86_64_8             14
#define R_X86_64_PC8           15
#define R_X86_64_PC64          24
#define R_X86_64_GOTPCRELX     41
#define R_X86_64_REX_GOTPCRELX 42

static int eh_got(u32 tipo)
{
    return tipo == R_X86_64_GOTPCREL || tipo == R_X86_64_GOTPCRELX || tipo == R_X86_64_REX_GOTPCRELX;
}

/* O endereço final de um símbolo de um objeto. */
static u64 sym_valor(obj_t *o, u32 idx)
{
    sym_t *s = &o->sym[idx];
    if (s->ligacao != STB_LOCAL) {
        global_t *g = s->g;
        if (g->interno) return valor_interno(g->nome);
        if (g->obj) {
            sym_t *d = &g->obj->sym[g->idx];
            if (d->shndx == SHN_ABS) return d->valor;
            return g->obj->sec[d->shndx].ender + d->valor;
        }
        if (g->comum_tam) return g->comum_ender;
        return 0;                                 /* fraco e indefinido */
    }
    if (s->shndx == SHN_ABS) return s->valor;
    if (s->shndx == SHN_UNDEF || s->shndx >= o->nsec) return 0;
    return o->sec[s->shndx].ender + (s->tipo == STT_SECTION ? 0 : s->valor);
}

/* Toda referência pela GOT ganha uma posição nela. */
static void conta_got(void)
{
    for (obj_t *o = objs; o; o = o->prox)
        for (u32 i = 1; i < o->nsec; i++) {
            sec_t *s = &o->sec[i];
            if (s->classe == C_FORA) continue;
            for (u64 k = 0; k < s->nrela; k++) {
                const u8 *r = s->rela + 24 * k;
                u32 tipo = (u32)le32(r + 8), idx = (u32)le32(r + 12);
                if (!eh_got(tipo)) continue;
                if (idx >= o->nsym) erro("relocação com símbolo inválido", o->nome);
                sym_t *sym = &o->sym[idx];
                if (sym->ligacao == STB_LOCAL) erro("GOT para símbolo local não suportada", o->nome);
                if (!sym->g->got) sym->g->got = (int)++ngot;
            }
        }
}

static u8 *imagem;   /* a memória do programa, de base até carga_fim */

static void confere(i64 v, i64 min, i64 max, const char *obj, const char *sym)
{
    if (v < min || v > max) {
        fprintf(stderr, "nefs-ld: %s: o endereço de '%s' não cabe no campo da relocação\n", obj, sym);
        exit(1);
    }
}

static void reloca(void)
{
    for (obj_t *o = objs; o; o = o->prox)
        for (u32 i = 1; i < o->nsec; i++) {
            sec_t *s = &o->sec[i];
            if (s->classe == C_FORA || s->classe == C_BSS) continue;
            u8 *dest = imagem + (s->ender - base);
            memcpy(dest, s->dados, (size_t)s->tam);
            for (u64 k = 0; k < s->nrela; k++) {
                const u8 *r = s->rela + 24 * k;
                u64 ofs = le64(r);
                u32 tipo = (u32)le32(r + 8), idx = (u32)le32(r + 12);
                i64 A = (i64)le64(r + 16);
                if (ofs >= s->tam) erro("relocação fora da seção", o->nome);
                if (tipo == R_X86_64_NONE) continue;
                if (idx >= o->nsym) erro("relocação com símbolo inválido", o->nome);
                const char *nome = o->sym[idx].nome;
                i64 S = (i64)sym_valor(o, idx);
                i64 P = (i64)(s->ender + ofs);
                u8 *campo = dest + ofs;
                i64 v;
                switch (tipo) {
                case R_X86_64_64:
                    poe(campo, (u64)(S + A), 8);
                    break;
                case R_X86_64_PC64:
                    poe(campo, (u64)(S + A - P), 8);
                    break;
                case R_X86_64_PC32:
                case R_X86_64_PLT32:
                    v = S + A - P;
                    confere(v, INT32_MIN, INT32_MAX, o->nome, nome);
                    poe(campo, (u64)v, 4);
                    break;
                case R_X86_64_GOTPCREL:
                case R_X86_64_GOTPCRELX:
                case R_X86_64_REX_GOTPCRELX:
                    v = (i64)(got_ender + 8 * (u64)(o->sym[idx].g->got - 1)) + A - P;
                    confere(v, INT32_MIN, INT32_MAX, o->nome, nome);
                    poe(campo, (u64)v, 4);
                    break;
                case R_X86_64_32:
                    v = S + A;
                    confere(v, 0, (i64)UINT32_MAX, o->nome, nome);
                    poe(campo, (u64)v, 4);
                    break;
                case R_X86_64_32S:
                    v = S + A;
                    confere(v, INT32_MIN, INT32_MAX, o->nome, nome);
                    poe(campo, (u64)v, 4);
                    break;
                case R_X86_64_16:
                    v = S + A;
                    confere(v, 0, 0xFFFF, o->nome, nome);
                    poe(campo, (u64)v, 2);
                    break;
                case R_X86_64_PC16:
                    v = S + A - P;
                    confere(v, INT16_MIN, INT16_MAX, o->nome, nome);
                    poe(campo, (u64)v, 2);
                    break;
                case R_X86_64_8:
                    v = S + A;
                    confere(v, 0, 0xFF, o->nome, nome);
                    poe(campo, (u64)v, 1);
                    break;
                case R_X86_64_PC8:
                    v = S + A - P;
                    confere(v, INT8_MIN, INT8_MAX, o->nome, nome);
                    poe(campo, (u64)v, 1);
                    break;
                default: {
                    char t[32];
                    snprintf(t, sizeof t, "%u", tipo);
                    fprintf(stderr, "nefs-ld: %s: ", o->nome);
                    erro("tipo de relocação não suportado", t);
                }
                }
            }
        }
    /* a GOT: o endereço de cada símbolo */
    for (no_g_t *n = ordem; n; n = n->prox) {
        global_t *g = n->g;
        if (!g->got) continue;
        u64 v;
        if (g->interno) v = valor_interno(g->nome);
        else if (g->obj) v = sym_valor(g->obj, g->idx);
        else v = g->comum_tam ? g->comum_ender : 0;
        poe(imagem + (got_ender - base) + 8 * (u64)(g->got - 1), v, 8);
    }
}

/* ------------------------------- Saída ---------------------------------- */

static void grava(const char *saida, int binario, u64 entrada)
{
    FILE *f = fopen(saida, "wb");
    if (!f) erro("não consegui criar", saida);
    size_t tam = (size_t)(carga_fim - base);
    if (!binario) {
        /* Cabeçalho ELF + 3 segmentos (código, constantes, dados+bss). O
         * arquivo espelha a memória a partir do byte 4096. */
        u8 cab[4096];
        memset(cab, 0, sizeof cab);
        memcpy(cab, "\177ELF", 4);
        cab[4] = 2; cab[5] = 1; cab[6] = 1;
        poe(cab + 16, 2, 2);                     /* ET_EXEC */
        poe(cab + 18, 62, 2);                    /* x86-64 */
        poe(cab + 20, 1, 4);
        poe(cab + 24, entrada, 8);
        poe(cab + 32, 64, 8);                    /* e_phoff */
        poe(cab + 52, 64, 2);                    /* e_ehsize */
        poe(cab + 54, 56, 2);                    /* e_phentsize */
        struct { u64 ini, fim_arq, fim_mem; u32 flags; } seg[3] = {
            { ender_classe[C_INICIO], ender_classe[C_RODATA], 0, 5 },   /* R X */
            { ender_classe[C_RODATA], ender_classe[C_DADOS], 0, 4 },    /* R   */
            { ender_classe[C_DADOS],  carga_fim,             fim, 6 },  /* R W */
        };
        if (seg[0].fim_arq > fim_classe[C_TEXTO]) seg[0].fim_arq = fim_classe[C_TEXTO];
        if (seg[1].fim_arq > fim_classe[C_RODATA]) seg[1].fim_arq = fim_classe[C_RODATA];
        int n = 0;
        for (int i = 0; i < 3; i++) {
            u64 fim_mem = seg[i].fim_mem ? seg[i].fim_mem : seg[i].fim_arq;
            if (fim_mem <= seg[i].ini) continue;
            u8 *ph = cab + 64 + 56 * n++;
            poe(ph, 1, 4);                       /* PT_LOAD */
            poe(ph + 4, seg[i].flags, 4);
            poe(ph + 8, 4096 + (seg[i].ini - base), 8);
            poe(ph + 16, seg[i].ini, 8);
            poe(ph + 24, seg[i].ini, 8);
            poe(ph + 32, seg[i].fim_arq > seg[i].ini ? seg[i].fim_arq - seg[i].ini : 0, 8);
            poe(ph + 40, fim_mem - seg[i].ini, 8);
            poe(ph + 48, 4096, 8);
        }
        poe(cab + 56, (u64)n, 2);                /* e_phnum */
        if (fwrite(cab, 1, sizeof cab, f) != sizeof cab) erro("erro ao gravar", saida);
    }
    if (fwrite(imagem, 1, tam, f) != tam || fclose(f) != 0) erro("erro ao gravar", saida);
}

static void mapa(void)
{
    static const char *const nomes[] = { "", "início", "código", "constantes", "dados", "bss" };
    for (int c = C_INICIO; c < NCLASSES; c++)
        printf("%-11s 0x%08llx - 0x%08llx\n", nomes[c],
               (unsigned long long)ender_classe[c], (unsigned long long)fim_classe[c]);
    if (ngot) printf("%-11s 0x%08llx (%llu posições)\n", "GOT", (unsigned long long)got_ender,
                     (unsigned long long)ngot);
    for (no_g_t *n = ordem; n; n = n->prox) {
        global_t *g = n->g;
        if (!g->obj && !g->comum_tam) continue;
        u64 v = g->obj ? sym_valor(g->obj, g->idx) : g->comum_ender;
        printf("  0x%08llx  %s\n", (unsigned long long)v, g->nome);
    }
}

/* -------------------------------- main ---------------------------------- */

static void uso(void)
{
    fprintf(stderr, "uso: nefs-ld [-o saída] [-base endereço] [-binario] [-inicio seção]\n"
                    "             [-e símbolo] [-L pasta] [-mapa] arquivos.o biblioteca.a -lnome\n");
    exit(1);
}

static void carrega(char *nome)
{
    size_t tam;
    u8 *d = le_arquivo(nome, &tam);
    if (tam >= 8 && memcmp(d, "!<arch>\n", 8) == 0) biblio_le(nome, d, tam);
    else obj_inclui(obj_le(nome, d, tam));
}

int main(int argc, char **argv)
{
    const char *saida = "a.out", *entrada = "_start";
    const char *pastas[16];
    int npastas = 0, binario = 0, mostra_mapa = 0;

    /* 1ª volta: opções e pastas de bibliotecas */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-L") == 0 && i + 1 < argc) { if (npastas < 15) pastas[npastas++] = argv[++i]; }
        else if (strncmp(argv[i], "-L", 2) == 0 && argv[i][2]) { if (npastas < 15) pastas[npastas++] = argv[i] + 2; }
    }
    pastas[npastas++] = "/usr/lib/nefs";

    /* O ponto de entrada é pedido desde o começo: assim o crt0 de uma
     * biblioteca entra sozinho. */
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) entrada = argv[i + 1];
    global(entrada)->pedido = 1;
    for (size_t i = 0; i < sizeof internos / sizeof internos[0]; i++) global(internos[i])->interno = 1;

    for (int i = 1; i < argc; i++) {
        char *a = argv[i];
        if (strcmp(a, "-o") == 0 && i + 1 < argc) saida = argv[++i];
        else if (strcmp(a, "-base") == 0 && i + 1 < argc) base = strtoull(argv[++i], 0, 0);
        else if (strcmp(a, "-inicio") == 0 && i + 1 < argc) secao_inicio = argv[++i];
        else if (strcmp(a, "-e") == 0 && i + 1 < argc) i++;
        else if (strcmp(a, "-L") == 0 && i + 1 < argc) i++;
        else if (strncmp(a, "-L", 2) == 0) continue;
        else if (strcmp(a, "-binario") == 0) binario = 1;
        else if (strcmp(a, "-mapa") == 0) mostra_mapa = 1;
        else if (strncmp(a, "-l", 2) == 0 && a[2]) {
            char *achou = 0;
            for (int k = 0; k < npastas && !achou; k++) {
                char *c = xmalloc(strlen(pastas[k]) + strlen(a) + 8);
                sprintf(c, "%s/lib%s.a", pastas[k], a + 2);
                FILE *f = fopen(c, "rb");
                if (f) { fclose(f); achou = c; }
            }
            if (!achou) erro("biblioteca não encontrada", a);
            carrega(achou);
        }
        else if (a[0] == '-') uso();
        else carrega(xstrdup(a));
    }
    if (base & 0xFFF) erro("a base deve ser múltipla de 4096", 0);
    biblios_resolve();
    if (!objs) uso();

    /* símbolos que faltam */
    int faltam = 0;
    for (no_g_t *n = ordem; n; n = n->prox) {
        global_t *g = n->g;
        if (g->pedido && !g->obj && !g->interno && !g->comum_tam) {
            fprintf(stderr, "nefs-ld: símbolo indefinido: %s\n", g->nome);
            faltam = 1;
        }
    }
    if (faltam) return 1;

    for (obj_t *o = objs; o; o = o->prox)
        for (u32 i = 1; i < o->nsec; i++) o->sec[i].classe = classe_de(&o->sec[i]);
    conta_got();
    dispor();
    imagem = xmalloc((size_t)(carga_fim - base));
    reloca();

    global_t *e = global(entrada);
    u64 ender_entrada = e->obj ? sym_valor(e->obj, e->idx) : 0;
    if (!binario && !e->obj) erro("ponto de entrada não definido", entrada);
    grava(saida, binario, ender_entrada);
    if (mostra_mapa) mapa();
    return 0;
}
