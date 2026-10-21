/*
 * ===========================================================================
 *  NovaEra - usuario/arquivos.c   (PID 4, anel 3)
 *  Servidor de arquivos: pastas e arquivos na memória (um "ramfs").
 *
 *  No boot ele lê o initrd (um .tar que o núcleo mapeia só para ele, só
 *  para leitura) e monta a árvore: /bin com os programas, /etc com a
 *  identidade do sistema. Depois atende pedidos por mensagens: listar,
 *  abrir, ler, escrever, criar pastas, apagar. Tudo some ao desligar.
 *
 *  Um arquivo que veio do initrd é lido direto de lá; na primeira escrita
 *  ele ganha uma cópia própria no heap ("cópia na escrita").
 * ===========================================================================
 */
#include "ulib.h"

#define MAX_NOS     256u
#define MAX_CANAIS   32u
#define NOME_MAX     32u

typedef struct {
    u32  usado;
    u32  tipo;               /* ARQ_TIPO_ARQUIVO ou ARQ_TIPO_PASTA */
    u32  pai;                /* índice da pasta onde está (a raiz é 0) */
    char nome[NOME_MAX];
    u8  *dados;
    u32  tam;
    u32  cap;                /* 0 = os dados são do initrd (só leitura) */
} no_t;

typedef struct {
    u32 usado;
    u32 no;
    u32 pos;                 /* onde a próxima leitura ou escrita acontece */
    u32 dono;                /* quem abriu: se morrer, o canal é reaproveitado */
} canal_t;

static no_t    nos[MAX_NOS];
static canal_t canais[MAX_CANAIS];

/* ------------------------------ A árvore ------------------------------- */

static int achar_filho(u32 pasta, const char *nome, u32 tam)
{
    for (u32 i = 1; i < MAX_NOS; i++) {
        if (!nos[i].usado || nos[i].pai != pasta) continue;
        u32 k = 0;
        while (k < tam && nos[i].nome[k] == nome[k]) k++;
        if (k == tam && nos[i].nome[k] == '\0') return (int)i;
    }
    return -1;
}

/* Segue um caminho absoluto. Se 'pai' não for nulo, para antes do último
 * pedaço: devolve a pasta onde ele estaria e aponta 'ultimo' para o nome. */
static int resolver(const char *caminho, int *pai, const char **ultimo)
{
    if (caminho[0] != '/') return -1;
    int atual = 0;
    const char *p = caminho;
    for (;;) {
        while (*p == '/') p++;
        if (!*p) return atual;
        const char *ini = p;
        while (*p && *p != '/') p++;
        u32 tam = (u32)(p - ini);
        const char *resto = p;
        while (*resto == '/') resto++;
        if (pai && !*resto) {                    /* é o último pedaço */
            if (tam >= NOME_MAX) return -1;
            *pai = atual;
            *ultimo = ini;
            return achar_filho((u32)atual, ini, tam);
        }
        if (nos[atual].tipo != ARQ_TIPO_PASTA) return -1;
        atual = achar_filho((u32)atual, ini, tam);
        if (atual < 0) return -1;
    }
}

static int criar_no(u32 pai, const char *nome, u32 tipo)
{
    for (u32 i = 1; i < MAX_NOS; i++) {
        if (nos[i].usado) continue;
        mem_zera(&nos[i], sizeof(nos[i]));
        nos[i].usado = 1;
        nos[i].tipo  = tipo;
        nos[i].pai   = pai;
        u32 k = 0;
        while (nome[k] && nome[k] != '/' && k < NOME_MAX - 1) { nos[i].nome[k] = nome[k]; k++; }
        return (int)i;
    }
    return -1;
}

/* Garante espaço para 'tam' bytes, copiando do initrd se for preciso. */
static int reservar(no_t *n, u32 tam)
{
    if (tam <= n->cap) return 1;
    u32 cap = n->cap ? n->cap * 2 : 256;
    while (cap < tam) cap *= 2;
    u8 *novo = umalloc(cap);
    if (!novo) return 0;
    mem_copia(novo, n->dados, n->tam);
    if (n->cap) ufree(n->dados);
    n->dados = novo;
    n->cap   = cap;
    return 1;
}

/* ---------------------- Lendo o initrd (formato tar) ----------------------
 * Cabeçalhos de 512 bytes: nome no byte 0, tamanho em octal no 124, tipo
 * no 156 ('0' arquivo, '5' pasta) e a assinatura "ustar" no 257.        */

static u32 octal(const u8 *p, u32 n)
{
    u32 v = 0;
    for (u32 i = 0; i < n && p[i] >= '0' && p[i] <= '7'; i++) v = v * 8 + (u32)(p[i] - '0');
    return v;
}

/* Cria todas as pastas de um caminho relativo ("bin/ola"); devolve a pasta
 * do último pedaço e aponta 'nome' para ele. */
static int criar_caminho(const char *caminho, const char **nome)
{
    int atual = 0;
    const char *p = caminho;
    for (;;) {
        while (*p == '/') p++;
        const char *ini = p;
        while (*p && *p != '/') p++;
        if (!*p || !p[1]) { *nome = ini; return atual; }
        int f = achar_filho((u32)atual, ini, (u32)(p - ini));
        if (f < 0) f = criar_no((u32)atual, ini, ARQ_TIPO_PASTA);
        if (f < 0) return -1;
        atual = f;
    }
}

static void carregar_initrd(void)
{
    msg_t m;
    if (sys_initrd(&m) < 0 || m.arg[1] == 0) return;
    const u8 *tar = (const u8 *)(u64)m.arg[0];
    u32 tam = m.arg[1];

    for (u32 pos = 0; pos + 512 <= tam; ) {
        const u8 *h = tar + pos;
        if (h[0] == 0) break;                                   /* fim */
        if (h[257] != 'u' || h[258] != 's' || h[259] != 't') break;
        u32 bytes = octal(h + 124, 12);
        if (bytes > tam - pos - 512) break;

        char nome[100];
        str_copia(nome, (const char *)h, sizeof(nome));
        const char *rel = nome;
        if (rel[0] == '.' && rel[1] == '/') rel += 2;
        const char *ultimo;
        int pasta = rel[0] ? criar_caminho(rel, &ultimo) : -1;
        if (pasta >= 0 && *ultimo && achar_filho((u32)pasta, ultimo, str_tam(ultimo)) < 0) {
            u32 n = 0;
            while (ultimo[n] && ultimo[n] != '/') n++;
            if (h[156] == '5') {
                if (achar_filho((u32)pasta, ultimo, n) < 0)
                    criar_no((u32)pasta, ultimo, ARQ_TIPO_PASTA);
            } else if (h[156] == '0' || h[156] == 0) {
                int f = criar_no((u32)pasta, ultimo, ARQ_TIPO_ARQUIVO);
                if (f >= 0) {
                    nos[f].dados = (u8 *)(h + 512);             /* direto do initrd */
                    nos[f].tam   = bytes;
                }
            }
        }
        pos += 512 + ((bytes + 511) & ~511u);
    }
}

/* ------------------------------- Respostas ------------------------------- */

static void responder(u32 para, u32 tipo, u32 a0, u32 a1)
{
    msg_t r;
    msg_zera(&r);
    r.tipo   = tipo;
    r.arg[0] = a0;
    r.arg[1] = a1;
    sys_responder(para, &r);
}

static void ok(u32 para, u32 a0, u32 a1) { responder(para, MSG_OK, a0, a1); }
static void erro(u32 para, long codigo)  { responder(para, MSG_ERRO, (u32)(-codigo), 0); }

static int processo_vivo(u32 pid)
{
    msg_t m;
    if (sys_tarefa_info(pid, &m) < 0) return 0;
    return m.arg[0] != ESTADO_LIVRE && m.arg[0] != ESTADO_MORTO && m.arg[0] != ESTADO_ZUMBI;
}

static int canal_novo(u32 no, u32 pos, u32 dono)
{
    for (int volta = 0; volta < 2; volta++) {
        for (u32 c = 0; c < MAX_CANAIS; c++) {
            if (canais[c].usado) continue;
            canais[c].usado = 1;
            canais[c].no    = no;
            canais[c].pos   = pos;
            canais[c].dono  = dono;
            return (int)c;
        }
        /* Lotado: recupera os canais de quem já terminou sem fechar. */
        for (u32 c = 0; c < MAX_CANAIS; c++)
            if (!processo_vivo(canais[c].dono)) canais[c].usado = 0;
    }
    return -1;
}

/* -------------------------------- Pedidos -------------------------------- */

static void pedido(msg_t *m)
{
    u32 quem = m->origem;
    m->texto[MSG_TEXTO_MAX - 1] = '\0';

    if (m->tipo == MSG_LER || m->tipo == MSG_ESCREVER) {
        u32 c = m->arg[0];
        if (c >= MAX_CANAIS || !canais[c].usado) { erro(quem, ERRO_INVALIDO); return; }
        no_t *n = &nos[canais[c].no];
        if (m->tipo == MSG_LER) {
            u32 resta = n->tam > canais[c].pos ? n->tam - canais[c].pos : 0;
            u32 qtd = m->arg[1] < resta ? m->arg[1] : resta;
            if (qtd && sys_copiar_para(quem, n->dados + canais[c].pos, m->arg[2], qtd) < 0) {
                erro(quem, ERRO_INVALIDO);
                return;
            }
            canais[c].pos += qtd;
            ok(quem, qtd, 0);
        } else {
            u32 qtd = m->arg[2] < MSG_TEXTO_MAX ? m->arg[2] : MSG_TEXTO_MAX;
            u32 fim = canais[c].pos + qtd;
            if (!reservar(n, fim)) { erro(quem, ERRO_SEM_MEMORIA); return; }
            mem_copia(n->dados + canais[c].pos, m->texto, qtd);
            canais[c].pos = fim;
            if (fim > n->tam) n->tam = fim;
            ok(quem, qtd, 0);
        }
        return;
    }

    if (m->tipo == ARQ_FECHAR) {
        if (m->arg[0] < MAX_CANAIS) canais[m->arg[0]].usado = 0;
        ok(quem, 0, 0);
        return;
    }

    int pai = -1;
    const char *ultimo = 0;
    int alvo = resolver(m->texto, (m->tipo == ARQ_INFO || m->tipo == ARQ_LISTAR) ? 0 : &pai,
                        &ultimo);
    if (alvo < 0 && m->texto[0] == '/' && m->texto[1] == '\0') alvo = 0;

    switch (m->tipo) {
    case ARQ_INFO:
        if (alvo < 0) { erro(quem, ERRO_NAO_ACHADO); return; }
        ok(quem, nos[alvo].tipo, nos[alvo].tam);
        return;

    case ARQ_LISTAR: {
        if (alvo < 0 || nos[alvo].tipo != ARQ_TIPO_PASTA) { erro(quem, ERRO_NAO_ACHADO); return; }
        u32 n = 0;
        for (u32 i = 1; i < MAX_NOS; i++) {
            if (!nos[i].usado || nos[i].pai != (u32)alvo || n++ != m->arg[0]) continue;
            msg_t r;
            msg_zera(&r);
            r.tipo   = MSG_OK;
            r.arg[0] = nos[i].tipo;
            r.arg[1] = nos[i].tam;
            str_copia(r.texto, nos[i].nome, MSG_TEXTO_MAX);
            sys_responder(quem, &r);
            return;
        }
        erro(quem, ERRO_NAO_ACHADO);                 /* acabou a lista */
        return;
    }

    case ARQ_ABRIR: {
        u32 modo = m->arg[0];
        if (alvo == 0) { erro(quem, ERRO_INVALIDO); return; }    /* a raiz é pasta */
        if (alvo < 0) {
            if (modo == ARQ_MODO_LER || pai < 0 || nos[pai].tipo != ARQ_TIPO_PASTA) {
                erro(quem, ERRO_NAO_ACHADO);
                return;
            }
            alvo = criar_no((u32)pai, ultimo, ARQ_TIPO_ARQUIVO);
            if (alvo < 0) { erro(quem, ERRO_SEM_MEMORIA); return; }
        }
        if (nos[alvo].tipo != ARQ_TIPO_ARQUIVO) { erro(quem, ERRO_INVALIDO); return; }
        if (modo == ARQ_MODO_ESCREVER) nos[alvo].tam = 0;
        int c = canal_novo((u32)alvo, modo == ARQ_MODO_ANEXAR ? nos[alvo].tam : 0, quem);
        if (c < 0) { erro(quem, ERRO_LOTADO); return; }
        ok(quem, (u32)c, 0);
        return;
    }

    case ARQ_PASTA:
        if (alvo >= 0) { erro(quem, ERRO_INVALIDO); return; }    /* já existe */
        if (pai < 0 || nos[pai].tipo != ARQ_TIPO_PASTA) { erro(quem, ERRO_NAO_ACHADO); return; }
        if (criar_no((u32)pai, ultimo, ARQ_TIPO_PASTA) < 0) { erro(quem, ERRO_SEM_MEMORIA); return; }
        ok(quem, 0, 0);
        return;

    case ARQ_APAGAR:
        if (alvo <= 0) { erro(quem, alvo == 0 ? ERRO_INVALIDO : ERRO_NAO_ACHADO); return; }
        for (u32 i = 1; i < MAX_NOS; i++)
            if (nos[i].usado && nos[i].pai == (u32)alvo) { erro(quem, ERRO_INVALIDO); return; }
        for (u32 c = 0; c < MAX_CANAIS; c++)
            if (canais[c].usado && canais[c].no == (u32)alvo) canais[c].usado = 0;
        if (nos[alvo].cap) ufree(nos[alvo].dados);
        nos[alvo].usado = 0;
        ok(quem, 0, 0);
        return;
    }
    erro(quem, ERRO_INVALIDO);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    nos[0].usado = 1;                            /* a raiz, "/" */
    nos[0].tipo  = ARQ_TIPO_PASTA;
    carregar_initrd();
    if (achar_filho(0, "tmp", 3) < 0) criar_no(0, "tmp", ARQ_TIPO_PASTA);

    msg_t m;
    for (;;) {
        sys_receber(PID_QUALQUER, &m);
        pedido(&m);
    }
}
