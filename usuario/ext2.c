/*
 * ===========================================================================
 *  NovaEra - usuario/ext2.c   (PID 9, anel 3)
 *  Os discos ext2: o sistema de arquivos clássico do Linux. Diferente do
 *  FAT32, ele guarda, para cada arquivo, o DONO, o GRUPO e as PERMISSÕES
 *  (rwx), e por isso é onde o sistema vai morar (Etapa 5). O Linux lê e
 *  grava o que o NovaEra grava, e vice-versa.
 *
 *  Fala o mesmo protocolo dos outros servidores de arquivos (ARQ_*,
 *  MSG_LER, MSG_ESCREVER) e cuida de até 4 volumes (SA_MONTAR).
 *
 *  Como o ext2 guarda as coisas:
 *    - o disco é dividido em BLOCOS (1, 2 ou 4 KiB) e os blocos em GRUPOS
 *      (8 × tamanho do bloco blocos por grupo: 8192 blocos de 1 KiB);
 *    - o SUPERBLOCO (no byte 1024) diz o tamanho de tudo; a tabela de
 *      DESCRITORES diz onde fica cada parte de cada grupo;
 *    - cada grupo tem um mapa de bits dos blocos livres, um dos inodes
 *      livres e a sua parte da tabela de INODES;
 *    - um INODE é um arquivo sem nome: tipo e permissões, dono, tamanho e
 *      os números dos blocos (12 diretos, depois um bloco de números, um
 *      bloco de blocos de números, e mais um nível);
 *    - uma PASTA é um arquivo com registros (inode, tamanho do registro,
 *      tamanho do nome, tipo, nome). O nome mora na pasta, não no inode:
 *      por isso um arquivo pode ter vários nomes (links).
 *  A raiz é o inode 2.
 * ===========================================================================
 */
#include "ulib.h"

#define SETOR        512u
#define MAX_VOLUMES  4u
#define MAX_CANAIS   32u
#define NCACHE       16u
#define BLOCO_MAX    4096u
#define GDT_MAX      32768u          /* descritores: até 1024 grupos */
#define RAIZ         2u

/* superbloco: posições (bytes) */
#define SB_INODES       0
#define SB_BLOCOS       4
#define SB_LIVRES_B     12
#define SB_LIVRES_I     16
#define SB_PRIMEIRO     20
#define SB_LOG_BLOCO    24
#define SB_BPG          32
#define SB_IPG          40
#define SB_MNT_COUNT    52
#define SB_MAGICO       56
#define SB_ESTADO       58
#define SB_REV          76
#define SB_PRIM_INO     84
#define SB_TAM_INODE    88
#define SB_COMPAT       92
#define SB_INCOMPAT     96
#define SB_RO_COMPAT    100

#define INCOMPAT_TIPO   0x0002u      /* o tipo do arquivo vai na pasta */
#define RO_SPARSE       0x0001u
#define RO_GRANDE       0x0002u

/* inode: posições */
#define I_MODO     0
#define I_DONO     2
#define I_TAM      4
#define I_DTIME    20
#define I_GRUPO    24
#define I_LINKS    26
#define I_SETORES  28
#define I_FLAGS    32
#define I_BLOCO    40
#define I_ACL      104
#define I_LEN      128               /* o que o NovaEra lê e grava de cada inode */
#define FL_INDICE  0x1000u           /* pasta com índice (htree): o NovaEra não mantém */

#define S_PASTA    0x4000u
#define S_ARQUIVO  0x8000u
#define S_TIPO     0xF000u

static u32  le16(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8; }
static u32  le32(const u8 *p) { return le16(p) | le16(p + 2) << 16; }
static void poe16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void poe32(u8 *p, u32 v) { poe16(p, v); poe16(p + 2, v >> 16); }

/* ------------------------------ Os volumes ------------------------------- */

typedef struct {
    u32 bloco;                /* 0 = vazio (o bloco 0 nunca vem para o cache) */
    int sujo;
    u32 uso;
    u8  d[BLOCO_MAX];
} cache_t;

typedef struct {
    int  usado;
    u32  dev;
    char ponto[CAMINHO_MAX];
    u32  ponto_tam;
    int  so_ler;              /* recursos que o NovaEra não conhece: só leitura */
    u8   sb[1024];            /* o superbloco */
    u8   gdt[GDT_MAX];        /* os descritores dos grupos (32 bytes cada) */
    u32  bs, ngrupos, ipg, bpg, nblocos, ninodes, primeiro, tam_inode, gdt_bloco, gdt_blocos;
    int  com_tipo;            /* INCOMPAT_TIPO */
    int  meta_suja;           /* superbloco/descritores mudaram */
    cache_t cache[NCACHE];
} vol_t;

static vol_t  volumes[MAX_VOLUMES];
static vol_t *V = &volumes[0];
static u32    relogio;

static int disco(u32 tipo, u32 lba, u32 n, void *buf)
{
    u8 *b = buf;
    while (n) {
        u32 k = n > 128 ? 128 : n;
        msg_t m;
        msg_zera(&m);
        m.tipo = tipo;
        m.arg[0] = lba;
        m.arg[1] = k;
        m.arg[2] = (u32)(u64)b;
        m.arg[3] = V->dev;
        if (sys_chamar(PID_DISCO, &m) < 0 || m.tipo == MSG_ERRO) return -1;
        lba += k;
        n -= k;
        b += k * SETOR;
    }
    return 0;
}

static int bloco_es(u32 tipo, u32 b, void *buf) { return disco(tipo, b * (V->bs / SETOR), V->bs / SETOR, buf); }

/* --------------------------- Cache de blocos ----------------------------- */

static int cache_descarregar(cache_t *c)
{
    if (c->bloco && c->sujo) {
        if (bloco_es(DISCO_ESCREVER, c->bloco, c->d) < 0) return -1;
        c->sujo = 0;
    }
    return 0;
}

/* O bloco b no cache (lido do disco se 'ler'; senão, zerado). Atenção: o
 * ponteiro só vale até o próximo bloco()/novo(): o cache é pequeno. */
static cache_t *pegar(u32 b, int ler)
{
    cache_t *livre = &V->cache[0];
    for (u32 i = 0; i < NCACHE; i++) {
        if (V->cache[i].bloco == b) { V->cache[i].uso = ++relogio; return &V->cache[i]; }
        if (V->cache[i].uso < livre->uso) livre = &V->cache[i];
    }
    if (cache_descarregar(livre) < 0) return 0;
    livre->bloco = 0;
    if (ler) {
        if (bloco_es(DISCO_LER, b, livre->d) < 0) return 0;
    } else {
        mem_zera(livre->d, V->bs);
    }
    livre->bloco = b;
    livre->sujo  = !ler;
    livre->uso   = ++relogio;
    return livre;
}

static cache_t *bloco(u32 b) { return b && b < V->nblocos ? pegar(b, 1) : 0; }
static cache_t *novo(u32 b)  { return pegar(b, 0); }

/* Grava tudo: blocos sujos, descritores e o superbloco. */
static int descarregar(void)
{
    int r = 0;
    for (u32 i = 0; i < NCACHE; i++) r |= cache_descarregar(&V->cache[i]);
    if (V->meta_suja) {
        for (u32 k = 0; k < V->gdt_blocos; k++)
            r |= bloco_es(DISCO_ESCREVER, V->gdt_bloco + k, V->gdt + k * V->bs);
        r |= disco(DISCO_ESCREVER, 2, 2, V->sb);
        V->meta_suja = 0;
    }
    return r;
}

/* ------------------------------ Grupos ----------------------------------- */

static u8 *gd(u32 g) { return V->gdt + 32 * g; }
#define GD_MAPA_B   0
#define GD_MAPA_I   4
#define GD_TABELA   8
#define GD_LIVRES_B 12
#define GD_LIVRES_I 14
#define GD_PASTAS   16

static void conta(u32 g, u32 campo, int delta, u32 campo_sb)
{
    poe16(gd(g) + campo, (u32)((int)le16(gd(g) + campo) + delta));
    if (campo_sb) poe32(V->sb + campo_sb, (u32)((int)le32(V->sb + campo_sb) + delta));
    V->meta_suja = 1;
}

/* Um bloco livre (0 = disco cheio), de preferência no grupo 'g'. */
static u32 alocar_bloco(u32 g)
{
    for (u32 k = 0; k < V->ngrupos; k++, g = (g + 1) % V->ngrupos) {
        if (!le16(gd(g) + GD_LIVRES_B)) continue;
        u32 ini = V->primeiro + g * V->bpg;
        u32 n = V->nblocos - ini < V->bpg ? V->nblocos - ini : V->bpg;
        cache_t *c = bloco(le32(gd(g) + GD_MAPA_B));
        if (!c) return 0;
        for (u32 i = 0; i < n; i++) {
            if (c->d[i / 8] & (1u << (i % 8))) continue;
            c->d[i / 8] |= (u8)(1u << (i % 8));
            c->sujo = 1;
            conta(g, GD_LIVRES_B, -1, SB_LIVRES_B);
            return ini + i;
        }
    }
    return 0;
}

static void liberar_bloco(u32 b)
{
    if (b < V->primeiro || b >= V->nblocos) return;
    u32 g = (b - V->primeiro) / V->bpg, i = (b - V->primeiro) % V->bpg;
    cache_t *c = bloco(le32(gd(g) + GD_MAPA_B));
    if (!c || !(c->d[i / 8] & (1u << (i % 8)))) return;
    c->d[i / 8] &= (u8)~(1u << (i % 8));
    c->sujo = 1;
    conta(g, GD_LIVRES_B, 1, SB_LIVRES_B);
}

/* --------------------------------- Inodes -------------------------------- */

/* Onde o inode n mora: o bloco e a posição dentro dele. */
static int inode_onde(u32 n, u32 *b, u32 *off)
{
    if (n < 1 || n > V->ninodes) return -1;
    u32 g = (n - 1) / V->ipg, i = (n - 1) % V->ipg;
    u32 byte = i * V->tam_inode;
    *b = le32(gd(g) + GD_TABELA) + byte / V->bs;
    *off = byte % V->bs;
    return 0;
}

static int inode_ler(u32 n, u8 *ino)
{
    u32 b, off;
    if (inode_onde(n, &b, &off) < 0) return -1;
    cache_t *c = bloco(b);
    if (!c) return -1;
    mem_copia(ino, c->d + off, I_LEN);
    return 0;
}

static int inode_gravar(u32 n, const u8 *ino)
{
    u32 b, off;
    if (inode_onde(n, &b, &off) < 0) return -1;
    cache_t *c = bloco(b);
    if (!c) return -1;
    mem_copia(c->d + off, ino, I_LEN);
    c->sujo = 1;
    return 0;
}

/* Um inode livre, perto do grupo 'g' (0 = acabaram). O inode volta zerado. */
static u32 alocar_inode(u32 g, int pasta)
{
    for (u32 k = 0; k < V->ngrupos; k++, g = (g + 1) % V->ngrupos) {
        if (!le16(gd(g) + GD_LIVRES_I)) continue;
        cache_t *c = bloco(le32(gd(g) + GD_MAPA_I));
        if (!c) return 0;
        for (u32 i = 0; i < V->ipg; i++) {
            u32 n = g * V->ipg + i + 1;
            if ((c->d[i / 8] & (1u << (i % 8))) || n < le32(V->sb + SB_PRIM_INO)) continue;
            c->d[i / 8] |= (u8)(1u << (i % 8));
            c->sujo = 1;
            conta(g, GD_LIVRES_I, -1, SB_LIVRES_I);
            if (pasta) conta(g, GD_PASTAS, 1, 0);
            u32 b, off;
            inode_onde(n, &b, &off);
            cache_t *t = bloco(b);
            if (!t) return 0;
            mem_zera(t->d + off, V->tam_inode);
            t->sujo = 1;
            return n;
        }
    }
    return 0;
}

static void liberar_inode(u32 n, int pasta)
{
    u32 g = (n - 1) / V->ipg, i = (n - 1) % V->ipg;
    u8 ino[I_LEN];
    mem_zera(ino, sizeof ino);
    /* "apagado em": o NovaEra ainda não tem relógio, então uma data fixa
     * (2026). Um número pequeno aqui o fsck entende como a lista de órfãos. */
    poe32(ino + I_DTIME, 1767225600u);
    inode_gravar(n, ino);
    cache_t *c = bloco(le32(gd(g) + GD_MAPA_I));
    if (!c) return;
    c->d[i / 8] &= (u8)~(1u << (i % 8));
    c->sujo = 1;
    conta(g, GD_LIVRES_I, 1, SB_LIVRES_I);
    if (pasta) conta(g, GD_PASTAS, -1, 0);
}

static u32 grupo_de(u32 n) { return (n - 1) / V->ipg; }

/* O bloco número 'n' do arquivo (0 = buraco). Com 'criar', aloca o que
 * faltar (blocos novos vêm zerados) e atualiza ino (o chamador grava). */
static u32 bloco_de(u8 *ino, u32 dono, u32 n, int criar)
{
    u32 por = V->bs / 4, niveis, slot;
    if (n < 12) { slot = I_BLOCO + 4 * n; niveis = 0; }
    else {
        n -= 12;
        if (n < por) { slot = I_BLOCO + 48; niveis = 1; }
        else {
            n -= por;
            if (n < por * por) { slot = I_BLOCO + 52; niveis = 2; }
            else { n -= por * por; slot = I_BLOCO + 56; niveis = 3; }
        }
    }
    u32 g = grupo_de(dono);
    u32 b = le32(ino + slot);
    if (!b) {
        if (!criar || !(b = alocar_bloco(g))) return 0;
        if (!novo(b)) return 0;
        poe32(ino + slot, b);
        poe32(ino + I_SETORES, le32(ino + I_SETORES) + V->bs / SETOR);
    }
    for (u32 l = niveis; l > 0; l--) {
        u32 div = 1;
        for (u32 k = 1; k < l; k++) div *= por;
        u32 i = n / div;
        n %= div;
        cache_t *c = bloco(b);
        if (!c) return 0;
        u32 x = le32(c->d + 4 * i);
        if (!x) {
            if (!criar || !(x = alocar_bloco(g)) || !novo(x)) return 0;
            c = bloco(b);                      /* o cache pode ter girado */
            if (!c) return 0;
            poe32(c->d + 4 * i, x);
            c->sujo = 1;
            poe32(ino + I_SETORES, le32(ino + I_SETORES) + V->bs / SETOR);
        }
        b = x;
    }
    return b;
}

/* Libera um bloco de números (e o que ele aponta), 'nivel' níveis abaixo. */
static void liberar_arvore(u32 b, u32 nivel)
{
    if (!b) return;
    if (nivel) {
        u32 por = V->bs / 4;
        for (u32 i = 0; i < por; i++) {
            cache_t *c = bloco(b);
            if (!c) break;
            u32 x = le32(c->d + 4 * i);
            if (x) liberar_arvore(x, nivel - 1);
        }
    }
    liberar_bloco(b);
}

/* Apaga o conteúdo: todos os blocos voltam a ser livres. */
static void truncar(u8 *ino)
{
    for (u32 k = 0; k < 15; k++) {
        liberar_arvore(le32(ino + I_BLOCO + 4 * k), k < 12 ? 0 : k - 11);
        poe32(ino + I_BLOCO + 4 * k, 0);
    }
    poe32(ino + I_SETORES, 0);
    poe32(ino + I_TAM, 0);
}

/* -------------------------------- Pastas --------------------------------- */

typedef struct {
    u32  ino;
    u32  tipo;                 /* 1 = arquivo, 2 = pasta (o file_type do ext2) */
    char nome[256];
    u32  nome_tam;
    u32  bloco_log, pos;       /* onde está: bloco do arquivo e posição */
    u32  ant;                  /* posição do registro anterior no bloco (ou -1) */
} ent_t;

static u32 nome_tam(const u8 *r) { return V->com_tipo ? r[6] : le16(r + 6); }

/* O próximo registro em uso da pasta, a partir de (*bl, *pos). */
static int proximo(const u8 *dir, u32 *bl, u32 *pos, ent_t *e)
{
    u32 tam = le32(dir + I_TAM);
    u8 ino[I_LEN];
    mem_copia(ino, dir, I_LEN);
    while (*bl * V->bs < tam) {
        u32 b = bloco_de(ino, RAIZ, *bl, 0);
        cache_t *c = b ? bloco(b) : 0;
        if (!c) { (*bl)++; *pos = 0; continue; }
        u32 ant = 0xFFFFFFFFu;
        u32 p = 0;
        while (p + 8 <= V->bs) {               /* acha o registro em *pos (e o anterior) */
            u32 rl = le16(c->d + p + 4);
            if (rl < 8 || p + rl > V->bs) break;
            if (p >= *pos) {
                const u8 *r = c->d + p;
                *pos = p + rl;
                if (le32(r) && nome_tam(r)) {
                    e->ino = le32(r);
                    e->nome_tam = nome_tam(r);
                    for (u32 k = 0; k < e->nome_tam; k++) e->nome[k] = (char)r[8 + k];
                    e->nome[e->nome_tam] = '\0';
                    e->tipo = V->com_tipo ? r[7] : 0;
                    e->bloco_log = *bl;
                    e->pos = p;
                    e->ant = ant;
                    return 1;
                }
            }
            ant = p;
            p += rl;
        }
        (*bl)++;
        *pos = 0;
    }
    return 0;
}

static int procura(u32 dir_n, const char *nome, u32 tam, ent_t *e)
{
    u8 dir[I_LEN];
    if (inode_ler(dir_n, dir) < 0) return 0;
    u32 bl = 0, pos = 0;
    while (proximo(dir, &bl, &pos, e))
        if (e->nome_tam == tam && mem_igual(e->nome, nome, tam)) return 1;
    return 0;
}

static u32 alinha4(u32 x) { return (x + 3) & ~3u; }

static void poe_registro(u8 *r, u32 ino, u32 rl, const char *nome, u32 tam, u32 tipo)
{
    poe32(r, ino);
    poe16(r + 4, rl);
    if (V->com_tipo) { r[6] = (u8)tam; r[7] = (u8)tipo; }
    else poe16(r + 6, tam);
    mem_copia(r + 8, nome, tam);
}

/* Põe o nome na pasta: num espaço que sobra, ou num bloco novo no fim. */
static long adicionar(u32 dir_n, const char *nome, u32 tam, u32 ino, u32 tipo)
{
    u8 dir[I_LEN];
    if (inode_ler(dir_n, dir) < 0) return ERRO_DISCO;
    u32 precisa = 8 + alinha4(tam);
    u32 nb = le32(dir + I_TAM) / V->bs;
    for (u32 bl = 0; bl < nb; bl++) {
        u32 b = bloco_de(dir, dir_n, bl, 0);
        cache_t *c = b ? bloco(b) : 0;
        if (!c) continue;
        for (u32 p = 0; p + 8 <= V->bs; ) {
            u8 *r = c->d + p;
            u32 rl = le16(r + 4);
            if (rl < 8 || p + rl > V->bs) break;
            u32 usado = le32(r) ? 8 + alinha4(nome_tam(r)) : 0;
            if (rl - usado >= precisa) {
                if (usado) {                     /* divide o registro em dois */
                    poe16(r + 4, usado);
                    r += usado;
                    rl -= usado;
                }
                poe_registro(r, ino, rl, nome, tam, tipo);
                c->sujo = 1;
                goto feito;
            }
            p += rl;
        }
    }
    {   /* sem espaço: um bloco novo */
        u32 b = bloco_de(dir, dir_n, nb, 1);
        cache_t *c = b ? bloco(b) : 0;
        if (!c) return ERRO_SEM_ESPACO;
        poe_registro(c->d, ino, V->bs, nome, tam, tipo);
        c->sujo = 1;
        poe32(dir + I_TAM, (nb + 1) * V->bs);
    }
feito:
    poe32(dir + I_FLAGS, le32(dir + I_FLAGS) & ~FL_INDICE);   /* o índice ficou velho */
    inode_gravar(dir_n, dir);
    return 0;
}

/* Tira o registro e da pasta (junta com o anterior, ou zera o inode). */
static void remover(u32 dir_n, const ent_t *e)
{
    u8 dir[I_LEN];
    if (inode_ler(dir_n, dir) < 0) return;
    u32 b = bloco_de(dir, dir_n, e->bloco_log, 0);
    cache_t *c = b ? bloco(b) : 0;
    if (!c) return;
    u8 *r = c->d + e->pos;
    if (e->ant != 0xFFFFFFFFu) {
        u8 *a = c->d + e->ant;
        poe16(a + 4, le16(a + 4) + le16(r + 4));
    } else {
        poe32(r, 0);
    }
    c->sujo = 1;
    poe32(dir + I_FLAGS, le32(dir + I_FLAGS) & ~FL_INDICE);
    inode_gravar(dir_n, dir);
}

static int pasta_vazia(u32 n)
{
    u8 dir[I_LEN];
    if (inode_ler(n, dir) < 0) return 0;
    ent_t e;
    u32 bl = 0, pos = 0;
    while (proximo(dir, &bl, &pos, &e))
        if (!(e.nome_tam == 1 && e.nome[0] == '.') &&
            !(e.nome_tam == 2 && e.nome[0] == '.' && e.nome[1] == '.')) return 0;
    return 1;
}

/* "/mnt/a/b": o inode de b. Com 'pai', para antes do último pedaço. */
static long resolver(const char *caminho, u32 *n, u32 *pai, const char **ultimo)
{
    const char *p = caminho + V->ponto_tam;
    u32 atual = RAIZ;
    for (;;) {
        while (*p == '/') p++;
        if (!*p) { if (pai) return ERRO_INVALIDO; *n = atual; return 0; }
        const char *ini = p;
        while (*p && *p != '/') p++;
        u32 tam = (u32)(p - ini);
        const char *q = p;
        while (*q == '/') q++;
        u8 ino[I_LEN];
        if (inode_ler(atual, ino) < 0) return ERRO_DISCO;
        if ((le16(ino + I_MODO) & S_TIPO) != S_PASTA) return ERRO_NAO_ACHADO;
        if (pai && !*q) {
            if (tam > 255) return ERRO_INVALIDO;
            *pai = atual;
            *ultimo = ini;
            return 0;
        }
        ent_t e;
        if (!procura(atual, ini, tam, &e)) return ERRO_NAO_ACHADO;
        atual = e.ino;
    }
}

/* -------------------------------- Montar --------------------------------- */

static long montar(void)
{
    for (u32 i = 0; i < NCACHE; i++) V->cache[i].bloco = 0;
    V->meta_suja = 0;
    if (disco(DISCO_LER, 2, 2, V->sb) < 0) return ERRO_DISCO;
    const u8 *s = V->sb;
    if (le16(s + SB_MAGICO) != 0xEF53) return ERRO_FORMATO;
    u32 log = le32(s + SB_LOG_BLOCO);
    if (log > 2) return ERRO_FORMATO;
    V->bs        = 1024u << log;
    V->nblocos   = le32(s + SB_BLOCOS);
    V->ninodes   = le32(s + SB_INODES);
    V->primeiro  = le32(s + SB_PRIMEIRO);
    V->bpg       = le32(s + SB_BPG);
    V->ipg       = le32(s + SB_IPG);
    V->tam_inode = le32(s + SB_REV) ? le16(s + SB_TAM_INODE) : 128;
    if (!V->bpg || !V->ipg || V->tam_inode < 128 || V->bpg > 8 * V->bs || V->ipg > 8 * V->bs)
        return ERRO_FORMATO;
    V->ngrupos   = (V->nblocos - V->primeiro + V->bpg - 1) / V->bpg;
    if (V->ngrupos * 32 > GDT_MAX) return ERRO_FORMATO;
    u32 incompat = le32(s + SB_INCOMPAT), ro = le32(s + SB_RO_COMPAT);
    if (incompat & ~INCOMPAT_TIPO) return ERRO_FORMATO;     /* ext3 com diário, ext4... */
    V->com_tipo = (incompat & INCOMPAT_TIPO) != 0;
    V->so_ler   = (ro & ~(RO_SPARSE | RO_GRANDE)) != 0;
    V->gdt_bloco  = V->primeiro + 1;
    V->gdt_blocos = (V->ngrupos * 32 + V->bs - 1) / V->bs;
    for (u32 k = 0; k < V->gdt_blocos; k++)
        if (bloco_es(DISCO_LER, V->gdt_bloco + k, V->gdt + k * V->bs) < 0) return ERRO_DISCO;
    if (!V->so_ler) {                         /* montado: "não está limpo" até desmontar */
        poe16(V->sb + SB_ESTADO, le16(V->sb + SB_ESTADO) & ~1u);
        poe16(V->sb + SB_MNT_COUNT, le16(V->sb + SB_MNT_COUNT) + 1);
        V->meta_suja = 1;
        descarregar();
    }
    return 0;
}

static vol_t *volume_de(const char *c)
{
    vol_t *melhor = 0;
    for (u32 i = 0; i < MAX_VOLUMES; i++) {
        vol_t *v = &volumes[i];
        if (!v->usado) continue;
        u32 k = 0;
        while (k < v->ponto_tam && c[k] == v->ponto[k]) k++;
        if (k < v->ponto_tam || (c[k] != '\0' && c[k] != '/')) continue;
        if (!melhor || v->ponto_tam > melhor->ponto_tam) melhor = v;
    }
    return melhor;
}

/* -------------------------------- Canais --------------------------------- */

typedef struct {
    int    usado;
    vol_t *vol;
    u32    dono;
    u32    ino;
    u32    pos;
} canal_t;

static canal_t canais[MAX_CANAIS];

static int vivo(u32 pid)
{
    msg_t m;
    if (sys_tarefa_info(pid, &m) < 0) return 0;
    return m.arg[0] != ESTADO_LIVRE && m.arg[0] != ESTADO_MORTO && m.arg[0] != ESTADO_ZUMBI;
}

static void responder(u32 para, u32 tipo, u32 a0, u32 a1)
{
    msg_t r;
    msg_zera(&r);
    r.tipo = tipo;
    r.arg[0] = a0;
    r.arg[1] = a1;
    sys_responder(para, &r);
}

static void ok(u32 para, u32 a0, u32 a1) { responder(para, MSG_OK, a0, a1); }
static void erro(u32 para, long e) { responder(para, MSG_ERRO, (u32)(-e), 0); }

static void canal_ler(u32 quem, canal_t *c, u32 max, u32 ender)
{
    u8 ino[I_LEN];
    if (inode_ler(c->ino, ino) < 0) { erro(quem, ERRO_DISCO); return; }
    u32 tam = le32(ino + I_TAM), feito = 0;
    static u8 zeros[BLOCO_MAX];
    while (feito < max && c->pos < tam) {
        u32 off = c->pos % V->bs;
        u32 n = V->bs - off;
        if (n > tam - c->pos) n = tam - c->pos;
        if (n > max - feito) n = max - feito;
        u32 b = bloco_de(ino, c->ino, c->pos / V->bs, 0);
        const u8 *dados = zeros;               /* um buraco: lê zeros */
        if (b) {
            cache_t *q = bloco(b);
            if (!q) { erro(quem, ERRO_DISCO); return; }
            dados = q->d;
        }
        if (sys_copiar_para(quem, (void *)(dados + off), (u64)ender + feito, n) < 0) {
            erro(quem, ERRO_INVALIDO);
            return;
        }
        c->pos += n;
        feito += n;
    }
    ok(quem, feito, 0);
}

static void canal_escrever(u32 quem, canal_t *c, const msg_t *m)
{
    if (V->so_ler) { erro(quem, ERRO_SEM_PERMISSAO); return; }
    u32 total = m->arg[2], ender = m->arg[3];
    if (!ender && total > MSG_TEXTO_MAX - 1) total = MSG_TEXTO_MAX - 1;
    if (ender && total > ESCRITA_MAX) total = ESCRITA_MAX;
    u8 ino[I_LEN];
    if (inode_ler(c->ino, ino) < 0) { erro(quem, ERRO_DISCO); return; }
    u32 feito = 0;
    long e = 0;
    while (feito < total) {
        u32 off = c->pos % V->bs;
        u32 n = V->bs - off;
        if (n > total - feito) n = total - feito;
        u32 b = bloco_de(ino, c->ino, c->pos / V->bs, 1);
        cache_t *q = b ? bloco(b) : 0;
        if (!q) { e = b ? ERRO_DISCO : ERRO_SEM_ESPACO; break; }
        if (ender) {
            if (sys_copiar_de(quem, q->d + off, (u64)ender + feito, n) < 0) { e = ERRO_INVALIDO; break; }
        } else mem_copia(q->d + off, m->texto + feito, n);
        q->sujo = 1;
        c->pos += n;
        feito += n;
        if (c->pos > le32(ino + I_TAM)) poe32(ino + I_TAM, c->pos);
    }
    inode_gravar(c->ino, ino);
    if (e && !feito) { erro(quem, e); return; }
    ok(quem, feito, 0);
}

static void fechar(canal_t *c)
{
    if (!c->usado) return;
    vol_t *antes = V;
    V = c->vol;
    descarregar();
    V = antes;
    c->usado = 0;
}

/* --------------------------------- Pedidos ------------------------------- */

static void info(u32 quem, u32 tipo_msg, u32 n, const char *nome)
{
    u8 ino[I_LEN];
    if (inode_ler(n, ino) < 0) { erro(quem, ERRO_DISCO); return; }
    msg_t r;
    msg_zera(&r);
    r.tipo   = MSG_OK;
    r.arg[0] = (le16(ino + I_MODO) & S_TIPO) == S_PASTA ? ARQ_TIPO_PASTA : ARQ_TIPO_ARQUIVO;
    r.arg[1] = le32(ino + I_TAM);
    r.arg[2] = le16(ino + I_MODO);
    r.arg[3] = le16(ino + I_DONO) | le16(ino + I_GRUPO) << 16;
    if (nome) str_copia(r.texto, nome, MSG_TEXTO_MAX);
    (void)tipo_msg;
    sys_responder(quem, &r);
}

static void pedido(msg_t *m)
{
    u32 quem = m->origem;
    m->texto[MSG_TEXTO_MAX - 1] = '\0';

    if (m->tipo == SA_MONTAR) {
        u32 dev = m->arg[0];
        if (m->texto[0] != '/' || volume_de(m->texto)) { erro(quem, ERRO_OCUPADO); return; }
        vol_t *livre = 0;
        for (u32 i = 0; i < MAX_VOLUMES; i++) {
            if (volumes[i].usado && volumes[i].dev == dev) { erro(quem, ERRO_OCUPADO); return; }
            if (!volumes[i].usado && !livre) livre = &volumes[i];
        }
        if (!livre) { erro(quem, ERRO_LOTADO); return; }
        V = livre;
        V->dev = dev;
        long r = montar();
        if (r < 0) { erro(quem, r); return; }
        str_copia(V->ponto, m->texto, CAMINHO_MAX);
        V->ponto_tam = str_tam(V->ponto);
        if (V->ponto_tam == 1) V->ponto_tam = 0;
        V->usado = 1;
        ok(quem, V->nblocos, V->bs);
        return;
    }
    if (m->tipo == SA_DESMONTAR) {
        vol_t *v = 0;
        for (u32 i = 0; i < MAX_VOLUMES; i++)
            if (volumes[i].usado && str_igual(volumes[i].ponto, m->texto)) v = &volumes[i];
        if (!v) { erro(quem, ERRO_NAO_ACHADO); return; }
        for (u32 i = 0; i < MAX_CANAIS; i++) {
            if (!canais[i].usado || canais[i].vol != v) continue;
            if (vivo(canais[i].dono)) { erro(quem, ERRO_OCUPADO); return; }
            fechar(&canais[i]);
        }
        V = v;
        if (!V->so_ler) {                     /* desmontado direito: "limpo" */
            poe16(V->sb + SB_ESTADO, le16(V->sb + SB_ESTADO) | 1u);
            V->meta_suja = 1;
        }
        descarregar();
        v->usado = 0;
        ok(quem, 0, 0);
        return;
    }

    if (m->tipo == MSG_LER || m->tipo == MSG_ESCREVER || m->tipo == ARQ_FECHAR) {
        u32 n = ARQ_CANAL(m->arg[0]);
        if (n >= MAX_CANAIS || !canais[n].usado) { erro(quem, ERRO_INVALIDO); return; }
        canal_t *c = &canais[n];
        V = c->vol;
        if (m->tipo == MSG_LER) canal_ler(quem, c, m->arg[1], m->arg[2]);
        else if (m->tipo == MSG_ESCREVER) canal_escrever(quem, c, m);
        else { fechar(c); ok(quem, 0, 0); }
        return;
    }

    V = volume_de(m->texto);
    if (!V) { V = &volumes[0]; erro(quem, ERRO_NAO_ACHADO); return; }
    int muda = m->tipo == ARQ_PASTA || m->tipo == ARQ_APAGAR ||
               (m->tipo == ARQ_ABRIR && m->arg[0] != ARQ_MODO_LER);
    if (muda && V->so_ler) { erro(quem, ERRO_SEM_PERMISSAO); return; }

    u32 n, pai;
    const char *ultimo;
    long r;
    u8 ino[I_LEN];
    ent_t e;

    switch (m->tipo) {
    case ARQ_INFO:
        if ((r = resolver(m->texto, &n, 0, 0)) < 0) { erro(quem, r); return; }
        info(quem, ARQ_INFO, n, 0);
        return;

    case ARQ_LISTAR: {
        if ((r = resolver(m->texto, &n, 0, 0)) < 0) { erro(quem, r); return; }
        if (inode_ler(n, ino) < 0) { erro(quem, ERRO_DISCO); return; }
        if ((le16(ino + I_MODO) & S_TIPO) != S_PASTA) { erro(quem, ERRO_NAO_ACHADO); return; }
        u32 bl = 0, pos = 0, k = 0;
        while (proximo(ino, &bl, &pos, &e)) {
            if (e.nome[0] == '.' && (e.nome_tam == 1 || (e.nome_tam == 2 && e.nome[1] == '.'))) continue;
            if (k++ != m->arg[0]) continue;
            info(quem, ARQ_LISTAR, e.ino, e.nome);
            return;
        }
        erro(quem, ERRO_NAO_ACHADO);
        return;
    }

    case ARQ_ABRIR: {
        u32 modo = m->arg[0];
        if ((r = resolver(m->texto, &n, &pai, &ultimo)) < 0) { erro(quem, r); return; }
        u32 tam = str_tam(ultimo);
        if (procura(pai, ultimo, tam, &e)) {
            n = e.ino;
            if (inode_ler(n, ino) < 0) { erro(quem, ERRO_DISCO); return; }
            if ((le16(ino + I_MODO) & S_TIPO) == S_PASTA) { erro(quem, ERRO_INVALIDO); return; }
            if (modo == ARQ_MODO_ESCREVER && le32(ino + I_TAM)) {
                truncar(ino);
                inode_gravar(n, ino);
            }
        } else {
            if (modo == ARQ_MODO_LER) { erro(quem, ERRO_NAO_ACHADO); return; }
            n = alocar_inode(grupo_de(pai), 0);
            if (!n) { erro(quem, ERRO_SEM_ESPACO); return; }
            mem_zera(ino, sizeof ino);
            poe16(ino + I_MODO, S_ARQUIVO | 0644);
            poe16(ino + I_LINKS, 1);
            inode_gravar(n, ino);
            if ((r = adicionar(pai, ultimo, tam, n, 1)) < 0) { liberar_inode(n, 0); erro(quem, r); return; }
        }
        u32 c = MAX_CANAIS;
        for (u32 i = 0; i < MAX_CANAIS; i++) {
            if (canais[i].usado && !vivo(canais[i].dono)) fechar(&canais[i]);   /* morreu aberto */
            if (!canais[i].usado && c == MAX_CANAIS) c = i;
        }
        if (c == MAX_CANAIS) { erro(quem, ERRO_LOTADO); return; }
        canais[c].usado = 1;
        canais[c].vol   = V;
        canais[c].dono  = quem;
        canais[c].ino   = n;
        if (inode_ler(n, ino) < 0) { erro(quem, ERRO_DISCO); return; }
        canais[c].pos   = modo == ARQ_MODO_ANEXAR ? le32(ino + I_TAM) : 0;
        descarregar();
        ok(quem, c, 0);
        return;
    }

    case ARQ_PASTA: {
        if ((r = resolver(m->texto, &n, &pai, &ultimo)) < 0) { erro(quem, r); return; }
        u32 tam = str_tam(ultimo);
        if (procura(pai, ultimo, tam, &e)) { erro(quem, ERRO_EXISTE); return; }
        n = alocar_inode(grupo_de(pai), 1);
        if (!n) { erro(quem, ERRO_SEM_ESPACO); return; }
        mem_zera(ino, sizeof ino);
        poe16(ino + I_MODO, S_PASTA | 0755);
        poe16(ino + I_LINKS, 2);                 /* o nome na pasta de cima e o "." */
        u32 b = bloco_de(ino, n, 0, 1);
        cache_t *q = b ? bloco(b) : 0;
        if (!q) { liberar_inode(n, 1); erro(quem, ERRO_SEM_ESPACO); return; }
        poe_registro(q->d, n, 12, ".", 1, 2);
        poe_registro(q->d + 12, pai, V->bs - 12, "..", 2, 2);
        q->sujo = 1;
        poe32(ino + I_TAM, V->bs);
        inode_gravar(n, ino);
        if ((r = adicionar(pai, ultimo, tam, n, 2)) < 0) {
            truncar(ino);
            liberar_inode(n, 1);
            erro(quem, r);
            return;
        }
        if (inode_ler(pai, ino) == 0) {          /* o ".." da nova aponta para o pai */
            poe16(ino + I_LINKS, le16(ino + I_LINKS) + 1);
            inode_gravar(pai, ino);
        }
        descarregar();
        ok(quem, 0, 0);
        return;
    }

    case ARQ_APAGAR: {
        if ((r = resolver(m->texto, &n, &pai, &ultimo)) < 0) { erro(quem, r); return; }
        if (!procura(pai, ultimo, str_tam(ultimo), &e)) { erro(quem, ERRO_NAO_ACHADO); return; }
        n = e.ino;
        if (inode_ler(n, ino) < 0) { erro(quem, ERRO_DISCO); return; }
        int pasta = (le16(ino + I_MODO) & S_TIPO) == S_PASTA;
        if (pasta && !pasta_vazia(n)) { erro(quem, ERRO_INVALIDO); return; }
        for (u32 i = 0; i < MAX_CANAIS; i++)
            if (canais[i].usado && canais[i].vol == V && canais[i].ino == n) { erro(quem, ERRO_OCUPADO); return; }
        remover(pai, &e);
        u32 links = le16(ino + I_LINKS);
        if (pasta || links <= 1) {
            truncar(ino);
            liberar_inode(n, pasta);
            if (pasta && inode_ler(pai, ino) == 0) {
                poe16(ino + I_LINKS, le16(ino + I_LINKS) - 1);
                inode_gravar(pai, ino);
            }
        } else {                                 /* ainda tem outro nome (um link) */
            poe16(ino + I_LINKS, links - 1);
            inode_gravar(n, ino);
        }
        descarregar();
        ok(quem, 0, 0);
        return;
    }
    }
    erro(quem, ERRO_INVALIDO);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    msg_t m;
    /* Reiniciado depois de uma falha? As montagens antigas eram minhas. */
    for (u32 i = 0; ; i++) {
        msg_zera(&m);
        m.tipo   = MONT_LISTAR;
        m.arg[0] = i;
        if (sys_chamar(PID_ARQUIVOS, &m) < 0 || m.tipo != MSG_OK) break;
        if (m.arg[0] != PID_EXT2) continue;
        msg_t r = m;
        r.tipo = MONT_REMOVER;
        sys_chamar(PID_ARQUIVOS, &r);
        i--;
    }
    for (;;) {
        sys_receber(PID_QUALQUER, &m);
        pedido(&m);
    }
}
