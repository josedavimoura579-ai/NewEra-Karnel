/*
 * ===========================================================================
 *  NovaEra - usuario/fat.c   (PID 8, anel 3)
 *  O disco, em /disco: o sistema de arquivos FAT32, o mesmo de pendrives e
 *  cartões de memória. Um disco do NovaEra abre no Windows, no Linux, no
 *  celular (e com o mtools: mdir, mcopy).
 *
 *  Fala o mesmo protocolo do servidor de arquivos (ARQ_*, MSG_LER e
 *  MSG_ESCREVER): a ulib manda para cá todo caminho que começa em /disco.
 *  Os setores vêm do driver de disco (PID_DISCO).
 *
 *  Como o FAT32 guarda as coisas:
 *    - o disco é dividido em "clusters" (blocos de 512 bytes a 32 KiB)
 *    - a FAT é uma tabela com um número por cluster: o próximo cluster do
 *      mesmo arquivo (uma lista ligada), 0 = livre, 0x0FFFFFF8+ = fim
 *    - uma pasta é um arquivo com registros de 32 bytes: nome no formato
 *      8.3 ("KERNEL  C  "), atributos, o 1º cluster e o tamanho. Nomes
 *      longos ("programas.lista") vêm em registros extras, antes (LFN)
 *  O começo do disco (setor 0, ou o da partição) diz onde fica cada parte.
 * ===========================================================================
 */
#include "ulib.h"

#define SETOR        512u
#define FIM_CADEIA   0x0FFFFFF8u     /* daqui para cima: fim do arquivo */
#define ATR_SO_LER   0x01
#define ATR_ROTULO   0x08
#define ATR_PASTA    0x10
#define ATR_ARQUIVO  0x20
#define ATR_LFN      0x0F
#define DATA_1980    0x0021u         /* 1/1/1980: o NovaEra ainda não sabe a data */
#define MAX_CANAIS   32u
#define NCACHE       4u
#define CLUSTER_MAX  (64u * SETOR)

/* ------------------------------- O disco --------------------------------- */

static int montado;
static u32 part_ini;          /* o setor onde o FAT32 começa (partição) */
static u32 spc;               /* setores por cluster */
static u32 tam_cl;            /* bytes por cluster */
static u32 reservados, nfats, fat_tam, raiz, fsinfo;
static u32 dados_ini;         /* o setor do cluster 2 */
static u32 nclusters;         /* clusters de dados: 2 .. nclusters+1 */
static u32 dica = 2;          /* onde procurar um cluster livre */
static int fsinfo_ok;         /* o FSInfo já foi marcado como "não sei" */

static u16 le16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static u32 le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static void poe16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void poe32(u8 *p, u32 v) { poe16(p, v); poe16(p + 2, v >> 16); }

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
        if (sys_chamar(PID_DISCO, &m) < 0 || m.tipo == MSG_ERRO) return -1;
        lba += k;
        n -= k;
        b += k * SETOR;
    }
    return 0;
}

/* ------------------------------ A FAT ------------------------------------ */

static u8  fbuf[SETOR];
static u32 fbuf_setor = 0xFFFFFFFFu;
static int fbuf_sujo;

static int fat_descarregar(void)
{
    if (!fbuf_sujo) return 0;
    for (u32 k = 0; k < nfats; k++)              /* todas as cópias da FAT */
        if (disco(DISCO_ESCREVER, part_ini + reservados + k * fat_tam + fbuf_setor, 1, fbuf) < 0) return -1;
    fbuf_sujo = 0;
    return 0;
}

static int fat_carregar(u32 setor)
{
    if (setor == fbuf_setor) return 0;
    if (fat_descarregar() < 0) return -1;
    fbuf_setor = 0xFFFFFFFFu;
    if (disco(DISCO_LER, part_ini + reservados + setor, 1, fbuf) < 0) return -1;
    fbuf_setor = setor;
    return 0;
}

static u32 fat_le(u32 c)
{
    if (fat_carregar(c / 128) < 0) return FIM_CADEIA;
    return le32(fbuf + (c % 128) * 4) & 0x0FFFFFFFu;
}

static void fat_poe(u32 c, u32 v)
{
    if (fat_carregar(c / 128) < 0) return;
    u8 *p = fbuf + (c % 128) * 4;
    poe32(p, (le32(p) & 0xF0000000u) | (v & 0x0FFFFFFFu));   /* os 4 bits de cima ficam */
    fbuf_sujo = 1;
}

/* O FSInfo guarda quantos clusters estão livres; o NovaEra não conta:
 * escreve "não sei" (0xFFFFFFFF), e quem precisar recalcula. */
static void fsinfo_invalidar(void)
{
    if (fsinfo_ok || !fsinfo) return;
    u8 s[SETOR];
    if (disco(DISCO_LER, part_ini + fsinfo, 1, s) == 0 && le32(s) == 0x41615252u) {
        poe32(s + 488, 0xFFFFFFFFu);
        disco(DISCO_ESCREVER, part_ini + fsinfo, 1, s);
    }
    fsinfo_ok = 1;
}

/* Um cluster livre, ligado depois de 'anterior' (0 = começo de cadeia). */
static u32 alocar(u32 anterior)
{
    fsinfo_invalidar();
    for (u32 i = 0; i < nclusters; i++) {
        u32 c = 2 + (dica - 2 + i) % nclusters;
        if (fat_le(c) != 0) continue;
        fat_poe(c, 0x0FFFFFFFu);
        if (anterior) fat_poe(anterior, c);
        dica = c + 1;
        if (dica >= nclusters + 2) dica = 2;
        return c;
    }
    return 0;                                         /* disco cheio */
}

static void liberar(u32 c)
{
    fsinfo_invalidar();
    while (c >= 2 && c < FIM_CADEIA && c < nclusters + 2) {
        u32 prox = fat_le(c);
        fat_poe(c, 0);
        c = prox;
    }
}

/* ------------------------- Cache de clusters ----------------------------- */

typedef struct {
    u32 cl;                   /* 0 = vazio */
    int sujo;
    u32 uso;
    u8  d[CLUSTER_MAX];
} cache_t;

static cache_t cache[NCACHE];
static u32 relogio;

static u32 cl_lba(u32 c) { return part_ini + dados_ini + (c - 2) * spc; }

static int cache_descarregar(cache_t *e)
{
    if (e->cl && e->sujo) {
        if (disco(DISCO_ESCREVER, cl_lba(e->cl), spc, e->d) < 0) return -1;
        e->sujo = 0;
    }
    return 0;
}

static int descarregar_tudo(void)
{
    int r = 0;
    for (u32 i = 0; i < NCACHE; i++) r |= cache_descarregar(&cache[i]);
    r |= fat_descarregar();
    return r;
}

/* Os bytes do cluster c (lidos do disco se 'ler'). 0 = erro. */
static cache_t *carregar(u32 c, int ler)
{
    cache_t *livre = &cache[0];
    for (u32 i = 0; i < NCACHE; i++) {
        if (cache[i].cl == c) { cache[i].uso = ++relogio; return &cache[i]; }
        if (cache[i].uso < livre->uso) livre = &cache[i];
    }
    if (cache_descarregar(livre) < 0) return 0;
    livre->cl = 0;
    if (ler && disco(DISCO_LER, cl_lba(c), spc, livre->d) < 0) return 0;
    livre->cl = c;
    livre->sujo = 0;
    livre->uso = ++relogio;
    return livre;
}

/* -------------------------------- Pastas --------------------------------- */

/* O registro número i da pasta (32 bytes); com 'cresce', a pasta ganha
 * clusters novos (zerados) se precisar. 0 = não existe / sem espaço. */
static u8 *registro(u32 pasta, u32 i, int cresce, cache_t **qual)
{
    u32 por_cl = tam_cl / 32, c = pasta;
    for (u32 k = i / por_cl; k > 0; k--) {
        u32 prox = fat_le(c);
        if (prox < 2 || prox >= FIM_CADEIA) {
            if (!cresce) return 0;
            prox = alocar(c);
            if (!prox) return 0;
            cache_t *z = carregar(prox, 0);
            if (!z) return 0;
            mem_zera(z->d, tam_cl);
            z->sujo = 1;
        }
        c = prox;
    }
    cache_t *e = carregar(c, 1);
    if (!e) return 0;
    if (qual) *qual = e;
    return e->d + (i % por_cl) * 32;
}

typedef struct {
    char nome[256];           /* em UTF-8 */
    u8   sfn[11];             /* o nome 8.3 cru */
    u8   atr;
    u32  cluster, tam;
    u32  pasta;               /* onde o registro está */
    u32  primeiro, nreg;      /* os registros (LFN + o 8.3) */
    u32  reg;                 /* o registro 8.3 */
} ent_t;

/* UCS-2 (o LFN) -> UTF-8 */
static u32 utf8_poe(char *d, u32 n, u32 max, u32 c)
{
    if (c < 0x80) { if (n + 1 < max) d[n++] = (char)c; }
    else if (c < 0x800) { if (n + 2 < max) { d[n++] = (char)(0xC0 | c >> 6); d[n++] = (char)(0x80 | (c & 63)); } }
    else if (n + 3 < max) {
        d[n++] = (char)(0xE0 | c >> 12);
        d[n++] = (char)(0x80 | (c >> 6 & 63));
        d[n++] = (char)(0x80 | (c & 63));
    }
    return n;
}

static u8 soma_sfn(const u8 *sfn)
{
    u8 s = 0;
    for (int i = 0; i < 11; i++) s = (u8)(((s & 1) << 7) + (s >> 1) + sfn[i]);
    return s;
}

static const u8 lfn_pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };

/* O próximo registro de verdade da pasta, a partir de *i. 1 = achou. */
static int proximo(u32 pasta, u32 *i, ent_t *e)
{
    u16 lfn[260];
    int tem_lfn = 0;
    u8 soma = 0;
    u32 ini = *i;
    for (;; (*i)++) {
        u8 *r = registro(pasta, *i, 0, 0);
        if (!r || r[0] == 0x00) return 0;                /* fim da pasta */
        if (r[0] == 0xE5) { tem_lfn = 0; continue; }     /* apagado */
        if (r[11] == ATR_LFN) {
            u32 seq = r[0] & 0x1F;
            if (r[0] & 0x40) {                            /* o último pedaço vem primeiro */
                mem_zera(lfn, sizeof lfn);
                tem_lfn = 1;
                soma = r[13];
                ini = *i;
            }
            if (!tem_lfn || seq == 0 || seq > 20 || r[13] != soma) { tem_lfn = 0; continue; }
            for (u32 k = 0; k < 13; k++) lfn[(seq - 1) * 13 + k] = le16(r + lfn_pos[k]);
            continue;
        }
        if (r[11] & ATR_ROTULO) { tem_lfn = 0; continue; }   /* o nome do disco */
        mem_copia(e->sfn, r, 11);
        e->atr = r[11];
        e->cluster = (u32)le16(r + 20) << 16 | le16(r + 26);
        e->tam = le32(r + 28);
        e->pasta = pasta;
        e->reg = *i;
        u32 n = 0;
        if (tem_lfn && soma_sfn(r) == soma) {
            for (u32 k = 0; k < 260 && lfn[k] && lfn[k] != 0xFFFF; k++)
                n = utf8_poe(e->nome, n, sizeof e->nome, lfn[k]);
            e->primeiro = ini;
        } else {
            /* 8.3: "KERNEL  C  " -> "kernel.c" (os bits 3 e 4 do byte 12
             * dizem se o nome e a extensão são minúsculos) */
            for (u32 k = 0; k < 8 && r[k] != ' '; k++) {
                char c = (char)(k == 0 && r[0] == 0x05 ? 0xE5 : r[k]);
                if ((r[12] & 0x08) && c >= 'A' && c <= 'Z') c = (char)(c + 32);
                e->nome[n++] = c;
            }
            if (r[8] != ' ') {
                e->nome[n++] = '.';
                for (u32 k = 8; k < 11 && r[k] != ' '; k++) {
                    char c = (char)r[k];
                    if ((r[12] & 0x10) && c >= 'A' && c <= 'Z') c = (char)(c + 32);
                    e->nome[n++] = c;
                }
            }
            e->primeiro = *i;
        }
        e->nome[n] = '\0';
        e->nreg = *i - e->primeiro + 1;
        (*i)++;
        return 1;
    }
}

static int letra_igual(char a, char b)
{
    if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
    if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
    return a == b;
}

/* Procura um nome na pasta (sem diferença entre maiúsculas e minúsculas). */
static int procura(u32 pasta, const char *nome, u32 tam, ent_t *e)
{
    u32 i = 0;
    while (proximo(pasta, &i, e)) {
        u32 k = 0;
        while (k < tam && e->nome[k] && letra_igual(e->nome[k], nome[k])) k++;
        if (k == tam && e->nome[k] == '\0') return 1;
    }
    return 0;
}

/* A pasta raiz, como um registro. */
static void raiz_ent(ent_t *e)
{
    mem_zera(e, sizeof *e);
    e->atr = ATR_PASTA;
    e->cluster = raiz;
}

static u32 cl_pasta(const ent_t *e) { return e->cluster ? e->cluster : raiz; }

/* "/disco/a/b": e = o registro de b. Com 'pai', para antes do último pedaço:
 * pai = a pasta onde ele está, *ultimo = o nome. */
static long resolver(const char *caminho, ent_t *e, u32 *pai, const char **ultimo)
{
    const char *p = caminho + str_tam(ARQ_DISCO);
    raiz_ent(e);
    for (;;) {
        while (*p == '/') p++;
        if (!*p) return pai ? ERRO_INVALIDO : 0;
        const char *ini = p;
        while (*p && *p != '/') p++;
        u32 tam = (u32)(p - ini);
        const char *q = p;
        while (*q == '/') q++;
        if (!(e->atr & ATR_PASTA)) return ERRO_NAO_ACHADO;
        u32 dir = cl_pasta(e);
        if (pai && !*q) { *pai = dir; *ultimo = ini; return 0; }
        if (!procura(dir, ini, tam, e)) return ERRO_NAO_ACHADO;
    }
}

/* --------------------------- Criar registros ----------------------------- */

static int sfn_valido(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           (c && str_tem("$%'-_@~`!(){}^#&", c));
}

/* O nome cabe no 8.3 sem LFN? (uma parte toda minúscula vira os bits do
 * byte 12, como o Windows faz com "kernel.c") */
static int nome_83(const char *nome, u8 *sfn, u8 *bits)
{
    mem_set(sfn, ' ', 11);
    *bits = 0;
    if (nome[0] == '.') return 0;
    u32 n = 0, ponto = 0, i;
    int min[2] = {0, 0}, mai[2] = {0, 0};
    for (i = 0; nome[i]; i++) {
        char c = nome[i];
        if (c == '.') { if (ponto) return 0; ponto = 1; n = 0; continue; }
        if ((!ponto && n == 8) || (ponto && n == 3)) return 0;
        if (c >= 'a' && c <= 'z') { min[ponto] = 1; c = (char)(c - 32); }
        else if (c >= 'A' && c <= 'Z') mai[ponto] = 1;
        if (!sfn_valido(c)) return 0;
        sfn[(ponto ? 8 : 0) + n++] = (u8)c;
    }
    if (sfn[0] == ' ' || (ponto && sfn[8] == ' ')) return 0;
    if ((min[0] && mai[0]) || (min[1] && mai[1])) return 0;
    *bits = (u8)((min[0] ? 0x08 : 0) | (min[1] ? 0x10 : 0));
    return 1;
}

static int sfn_existe(u32 pasta, const u8 *sfn)
{
    ent_t e;
    u32 i = 0;
    while (proximo(pasta, &i, &e))
        if (mem_igual(e.sfn, sfn, 11)) return 1;
    return 0;
}

/* Um nome 8.3 para um nome longo: "PROGRA~1LIS". */
static void sfn_curto(u32 pasta, const char *nome, u8 *sfn)
{
    u8 base[8], ext[3];
    u32 nb = 0, ne = 0;
    const char *ponto = 0;
    for (const char *c = nome; *c; c++) if (*c == '.') ponto = c;
    if (ponto == nome) ponto = 0;
    for (const char *c = nome; *c && c != ponto; c++) {
        char x = *c;
        if (x == ' ' || x == '.') continue;
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (!sfn_valido(x)) x = '_';
        if (nb < 8) base[nb++] = (u8)x;
    }
    for (const char *c = ponto ? ponto + 1 : ""; *c && ne < 3; c++) {
        char x = *c;
        if (x == ' ' || x == '.') continue;
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (!sfn_valido(x)) x = '_';
        ext[ne++] = (u8)x;
    }
    if (!nb) base[nb++] = '_';
    for (u32 k = 1; k < 1000000; k++) {
        char cauda[8];
        u32 nc = 0;
        u32 v = k, dig[7], nd = 0;
        while (v) { dig[nd++] = v % 10; v /= 10; }
        cauda[nc++] = '~';
        while (nd) cauda[nc++] = (char)('0' + dig[--nd]);
        mem_set(sfn, ' ', 11);
        u32 cabe = nb < 8 - nc ? nb : 8 - nc;
        mem_copia(sfn, base, cabe);
        mem_copia(sfn + cabe, cauda, nc);
        mem_copia(sfn + 8, ext, ne);
        if (!sfn_existe(pasta, sfn)) return;
    }
}

/* UTF-8 -> UCS-2; devolve quantos (ou 0 se o nome for longo demais). */
static u32 ucs2(const char *s, u16 *d, u32 max)
{
    u32 n = 0;
    const u8 *p = (const u8 *)s;
    while (*p) {
        u32 c = *p++;
        if (c >= 0xE0 && p[0] && p[1]) { c = (c & 15) << 12 | (u32)(p[0] & 63) << 6 | (p[1] & 63); p += 2; }
        else if (c >= 0xC0 && p[0]) { c = (c & 31) << 6 | (p[0] & 63); p++; }
        else if (c >= 0x80) c = '_';
        if (n == max) return 0;
        d[n++] = (u16)c;
    }
    return n;
}

/* Um registro novo na pasta (com os LFN, se o nome precisar). */
static long criar_reg(u32 pasta, const char *nome, u8 atr, u32 cluster, ent_t *e)
{
    u8 sfn[11], bits;
    u16 longo[255];
    u32 nl = 0, nreg = 1;
    if (!nome_83(nome, sfn, &bits)) {
        nl = ucs2(nome, longo, 255);
        if (!nl) return ERRO_INVALIDO;
        sfn_curto(pasta, nome, sfn);
        bits = 0;
        nreg += (nl + 12) / 13;
    }
    /* nreg registros livres seguidos */
    u32 ini = 0, seguidos = 0;
    for (u32 i = 0;; i++) {
        u8 *r = registro(pasta, i, 1, 0);
        if (!r) return ERRO_SEM_ESPACO;
        if (r[0] == 0x00 || r[0] == 0xE5) {
            if (!seguidos) ini = i;
            if (++seguidos == nreg) break;
        } else seguidos = 0;
    }
    u8 soma = soma_sfn(sfn);
    for (u32 k = 0; k + 1 < nreg; k++) {          /* os LFN, do último pedaço ao 1º */
        u32 seq = nreg - 1 - k;
        cache_t *q;
        u8 *r = registro(pasta, ini + k, 1, &q);
        if (!r) return ERRO_SEM_ESPACO;
        mem_zera(r, 32);
        r[0] = (u8)(seq | (k == 0 ? 0x40 : 0));
        r[11] = ATR_LFN;
        r[13] = soma;
        for (u32 j = 0; j < 13; j++) {
            u32 pos = (seq - 1) * 13 + j;
            u16 c = pos < nl ? longo[pos] : pos == nl ? 0x0000 : 0xFFFF;
            poe16(r + lfn_pos[j], c);
        }
        q->sujo = 1;
    }
    cache_t *q;
    u8 *r = registro(pasta, ini + nreg - 1, 1, &q);
    if (!r) return ERRO_SEM_ESPACO;
    mem_zera(r, 32);
    mem_copia(r, sfn, 11);
    r[11] = atr;
    r[12] = bits;
    poe16(r + 16, DATA_1980);                     /* criado em */
    poe16(r + 18, DATA_1980);                     /* acessado em */
    poe16(r + 20, cluster >> 16);
    poe16(r + 24, DATA_1980);                     /* mudado em */
    poe16(r + 26, cluster & 0xFFFF);
    q->sujo = 1;
    if (e) {
        str_copia(e->nome, nome, sizeof e->nome);
        mem_copia(e->sfn, sfn, 11);
        e->atr = atr;
        e->cluster = cluster;
        e->tam = 0;
        e->pasta = pasta;
        e->primeiro = ini;
        e->nreg = nreg;
        e->reg = ini + nreg - 1;
    }
    return 0;
}

/* Muda o 1º cluster e o tamanho no registro 8.3 de um arquivo. */
static void atualizar_reg(u32 pasta, u32 reg, u32 cluster, u32 tam)
{
    cache_t *q;
    u8 *r = registro(pasta, reg, 0, &q);
    if (!r) return;
    poe16(r + 20, cluster >> 16);
    poe16(r + 26, cluster & 0xFFFF);
    poe32(r + 28, tam);
    q->sujo = 1;
}

static int pasta_vazia(u32 pasta)
{
    ent_t e;
    u32 i = 0;
    while (proximo(pasta, &i, &e))
        if (!(e.sfn[0] == '.' && (e.sfn[1] == ' ' || (e.sfn[1] == '.' && e.sfn[2] == ' ')))) return 0;
    return 1;
}

/* -------------------------------- Montar --------------------------------- */

static int montar(void)
{
    montado = 0;
    fsinfo_ok = 0;
    fbuf_setor = 0xFFFFFFFFu;
    fbuf_sujo = 0;
    for (u32 i = 0; i < NCACHE; i++) cache[i].cl = 0;

    msg_t m;
    msg_zera(&m);
    m.tipo = DISCO_INFO;
    if (sys_chamar(PID_DISCO, &m) < 0 || m.tipo != MSG_OK || m.arg[0] == 0) return -1;

    u8 s[SETOR];
    part_ini = 0;
    if (disco(DISCO_LER, 0, 1, s) < 0 || s[510] != 0x55 || s[511] != 0xAA) return -1;
    if (!mem_igual(s + 82, "FAT32", 5)) {
        /* uma tabela de partições (MBR): a 1ª partição FAT32 */
        for (u32 k = 0; k < 4; k++) {
            const u8 *p = s + 446 + 16 * k;
            if (p[4] == 0x0B || p[4] == 0x0C) { part_ini = le32(p + 8); break; }
        }
        if (!part_ini || disco(DISCO_LER, part_ini, 1, s) < 0 || !mem_igual(s + 82, "FAT32", 5)) return -1;
    }
    if (le16(s + 11) != SETOR) return -1;
    spc        = s[13];
    reservados = le16(s + 14);
    nfats      = s[16];
    fat_tam    = le32(s + 36);
    raiz       = le32(s + 44);
    fsinfo     = le16(s + 48);
    u32 total  = le16(s + 19) ? le16(s + 19) : le32(s + 32);
    if (!spc || spc > 64 || (spc & (spc - 1)) || !nfats || !fat_tam) return -1;
    tam_cl     = spc * SETOR;
    dados_ini  = reservados + nfats * fat_tam;
    if (total <= dados_ini) return -1;
    nclusters  = (total - dados_ini) / spc;
    if (nclusters > fat_tam * 128 - 2) nclusters = fat_tam * 128 - 2;
    dica = 2;
    montado = 1;
    return 0;
}

/* -------------------------------- Canais --------------------------------- */

typedef struct {
    int usado;
    u32 dono;
    u32 pasta, reg;            /* o registro do arquivo */
    u32 primeiro, tam, pos;
    u32 cur_i, cur_cl;         /* o último cluster visitado (leitura em ordem) */
} canal_t;

static canal_t canais[MAX_CANAIS];

static int vivo(u32 pid)
{
    msg_t m;
    if (sys_tarefa_info(pid, &m) < 0) return 0;
    return m.arg[0] != ESTADO_LIVRE && m.arg[0] != ESTADO_MORTO && m.arg[0] != ESTADO_ZUMBI;
}

/* O cluster número i do arquivo; com 'cresce', aloca os que faltarem. */
static u32 cluster_de(canal_t *c, u32 i, int cresce)
{
    if (!c->primeiro) {
        if (!cresce) return 0;
        c->primeiro = alocar(0);
        if (!c->primeiro) return 0;
        c->cur_i = 0;
        c->cur_cl = c->primeiro;
    }
    u32 k = 0, cl = c->primeiro;
    if (c->cur_cl && c->cur_i <= i) { k = c->cur_i; cl = c->cur_cl; }
    while (k < i) {
        u32 prox = fat_le(cl);
        if (prox < 2 || prox >= FIM_CADEIA) {
            if (!cresce) return 0;
            prox = alocar(cl);
            if (!prox) return 0;
        }
        cl = prox;
        k++;
    }
    c->cur_i = k;
    c->cur_cl = cl;
    return cl;
}

static void fechar(canal_t *c)
{
    if (!c->usado) return;
    atualizar_reg(c->pasta, c->reg, c->primeiro, c->tam);
    descarregar_tudo();
    c->usado = 0;
}

/* ------------------------------- Respostas ------------------------------- */

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
    u32 feito = 0;
    while (feito < max && c->pos < c->tam) {
        u32 off = c->pos % tam_cl;
        u32 n = tam_cl - off;
        if (n > c->tam - c->pos) n = c->tam - c->pos;
        if (n > max - feito) n = max - feito;
        u32 cl = cluster_de(c, c->pos / tam_cl, 0);
        cache_t *q = cl ? carregar(cl, 1) : 0;
        if (!q) { erro(quem, ERRO_DISCO); return; }
        if (sys_copiar_para(quem, q->d + off, (u64)ender + feito, n) < 0) { erro(quem, ERRO_INVALIDO); return; }
        c->pos += n;
        feito += n;
    }
    ok(quem, feito, 0);
}

static void canal_escrever(u32 quem, canal_t *c, const msg_t *m)
{
    u32 total = m->arg[2], ender = m->arg[3];
    if (!ender && total > MSG_TEXTO_MAX - 1) total = MSG_TEXTO_MAX - 1;
    if (ender && total > ESCRITA_MAX) total = ESCRITA_MAX;
    u32 feito = 0;
    while (feito < total) {
        u32 off = c->pos % tam_cl;
        u32 n = tam_cl - off;
        if (n > total - feito) n = total - feito;
        u32 cl = cluster_de(c, c->pos / tam_cl, 1);
        if (!cl) { erro(quem, ERRO_SEM_ESPACO); return; }
        /* um cluster inteiro novo não precisa ser lido antes */
        cache_t *q = carregar(cl, !(off == 0 && n == tam_cl));
        if (!q) { erro(quem, ERRO_DISCO); return; }
        if (ender) {
            if (sys_copiar_de(quem, q->d + off, (u64)ender + feito, n) < 0) { erro(quem, ERRO_INVALIDO); return; }
        } else mem_copia(q->d + off, m->texto + feito, n);
        q->sujo = 1;
        c->pos += n;
        feito += n;
        if (c->pos > c->tam) c->tam = c->pos;
    }
    atualizar_reg(c->pasta, c->reg, c->primeiro, c->tam);
    ok(quem, feito, 0);
}

/* --------------------------------- main ---------------------------------- */

static void pedido(msg_t *m)
{
    u32 quem = m->origem;
    m->texto[MSG_TEXTO_MAX - 1] = '\0';

    if (m->tipo == FAT_MONTAR) {
        for (u32 i = 0; i < MAX_CANAIS; i++) canais[i].usado = 0;
        if (montar() < 0) erro(quem, ERRO_NAO_ACHADO); else ok(quem, nclusters, tam_cl);
        return;
    }
    if (!montado && montar() < 0) { erro(quem, ERRO_NAO_ACHADO); return; }

    if (m->tipo == MSG_LER || m->tipo == MSG_ESCREVER || m->tipo == ARQ_FECHAR) {
        u32 n = ARQ_CANAL(m->arg[0]);
        if (n >= MAX_CANAIS || !canais[n].usado) { erro(quem, ERRO_INVALIDO); return; }
        canal_t *c = &canais[n];
        if (m->tipo == MSG_LER) canal_ler(quem, c, m->arg[1], m->arg[2]);
        else if (m->tipo == MSG_ESCREVER) canal_escrever(quem, c, m);
        else { fechar(c); ok(quem, 0, 0); }
        return;
    }

    ent_t e;
    u32 pai;
    const char *ultimo;
    long r;

    switch (m->tipo) {
    case ARQ_INFO:
        if ((r = resolver(m->texto, &e, 0, 0)) < 0) { erro(quem, r); return; }
        if (e.atr & ATR_PASTA) ok(quem, ARQ_TIPO_PASTA, 0);
        else ok(quem, ARQ_TIPO_ARQUIVO, e.tam);
        return;

    case ARQ_LISTAR: {
        if ((r = resolver(m->texto, &e, 0, 0)) < 0) { erro(quem, r); return; }
        if (!(e.atr & ATR_PASTA)) { erro(quem, ERRO_NAO_ACHADO); return; }
        u32 i = 0, n = 0, dir = cl_pasta(&e);
        while (proximo(dir, &i, &e)) {
            if (e.sfn[0] == '.') continue;               /* "." e ".." */
            if (n++ != m->arg[0]) continue;
            msg_t s;
            msg_zera(&s);
            s.tipo = MSG_OK;
            s.arg[0] = (e.atr & ATR_PASTA) ? ARQ_TIPO_PASTA : ARQ_TIPO_ARQUIVO;
            s.arg[1] = e.tam;
            str_copia(s.texto, e.nome, MSG_TEXTO_MAX);
            sys_responder(quem, &s);
            return;
        }
        erro(quem, ERRO_NAO_ACHADO);                    /* acabou a lista */
        return;
    }

    case ARQ_ABRIR: {
        u32 modo = m->arg[0];
        if ((r = resolver(m->texto, &e, &pai, &ultimo)) < 0) { erro(quem, r); return; }
        u32 tam = str_tam(ultimo);
        int achou = procura(pai, ultimo, tam, &e);
        if (achou && (e.atr & ATR_PASTA)) { erro(quem, ERRO_INVALIDO); return; }
        if (!achou) {
            if (modo == ARQ_MODO_LER) { erro(quem, ERRO_NAO_ACHADO); return; }
            if ((r = criar_reg(pai, ultimo, ATR_ARQUIVO, 0, &e)) < 0) { erro(quem, r); return; }
        } else if (modo == ARQ_MODO_ESCREVER && e.cluster) {
            liberar(e.cluster);                         /* apaga o conteúdo */
            e.cluster = 0;
            e.tam = 0;
            atualizar_reg(e.pasta, e.reg, 0, 0);
        }
        u32 n = MAX_CANAIS;
        for (u32 i = 0; i < MAX_CANAIS; i++) {
            if (canais[i].usado && !vivo(canais[i].dono)) fechar(&canais[i]);   /* morreu aberto */
            if (!canais[i].usado && n == MAX_CANAIS) n = i;
        }
        if (n == MAX_CANAIS) { erro(quem, ERRO_LOTADO); return; }
        canal_t *c = &canais[n];
        mem_zera(c, sizeof *c);
        c->usado = 1;
        c->dono = quem;
        c->pasta = e.pasta;
        c->reg = e.reg;
        c->primeiro = e.cluster;
        c->tam = e.tam;
        c->pos = modo == ARQ_MODO_ANEXAR ? e.tam : 0;
        descarregar_tudo();
        ok(quem, n, 0);
        return;
    }

    case ARQ_PASTA: {
        if ((r = resolver(m->texto, &e, &pai, &ultimo)) < 0) { erro(quem, r); return; }
        if (procura(pai, ultimo, str_tam(ultimo), &e)) { erro(quem, ERRO_EXISTE); return; }
        u32 cl = alocar(0);
        if (!cl) { erro(quem, ERRO_SEM_ESPACO); return; }
        cache_t *q = carregar(cl, 0);
        if (!q) { erro(quem, ERRO_DISCO); return; }
        mem_zera(q->d, tam_cl);
        /* "." (ela mesma) e ".." (a pasta de cima; 0 = a raiz) */
        u32 cima = pai == raiz ? 0 : pai;
        mem_set(q->d, ' ', 11);
        q->d[0] = '.';
        mem_set(q->d + 32, ' ', 11);
        q->d[32] = q->d[33] = '.';
        for (u32 k = 0; k < 2; k++) {
            u8 *x = q->d + 32 * k;
            u32 alvo = k ? cima : cl;
            x[11] = ATR_PASTA;
            poe16(x + 16, DATA_1980);
            poe16(x + 18, DATA_1980);
            poe16(x + 20, alvo >> 16);
            poe16(x + 24, DATA_1980);
            poe16(x + 26, alvo & 0xFFFF);
        }
        q->sujo = 1;
        if ((r = criar_reg(pai, ultimo, ATR_PASTA, cl, 0)) < 0) { liberar(cl); erro(quem, r); return; }
        descarregar_tudo();
        ok(quem, 0, 0);
        return;
    }

    case ARQ_APAGAR: {
        if ((r = resolver(m->texto, &e, &pai, &ultimo)) < 0) { erro(quem, r); return; }
        if (!procura(pai, ultimo, str_tam(ultimo), &e)) { erro(quem, ERRO_NAO_ACHADO); return; }
        if ((e.atr & ATR_PASTA) && !pasta_vazia(e.cluster)) { erro(quem, ERRO_INVALIDO); return; }
        liberar(e.cluster);
        for (u32 k = 0; k < e.nreg; k++) {
            cache_t *q;
            u8 *x = registro(e.pasta, e.primeiro + k, 0, &q);
            if (x) { x[0] = 0xE5; q->sujo = 1; }
        }
        descarregar_tudo();
        ok(quem, 0, 0);
        return;
    }
    }
    erro(quem, ERRO_INVALIDO);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    montar();                                   /* sem disco, tenta de novo a cada pedido */
    msg_t m;
    for (;;) {
        sys_receber(PID_QUALQUER, &m);
        pedido(&m);
    }
}
