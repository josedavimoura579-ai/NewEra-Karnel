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
 *
 *  Ele também é a RAIZ de tudo: guarda a tabela de montagens. Um caminho
 *  que cai dentro de um disco montado (/disco/fotos, /mnt/a.txt) não é
 *  dele: a resposta é ARQ_OUTRO_SERVIDOR, com o PID de quem cuida, e a ulib
 *  refaz o pedido lá. Ler e escrever não passam por aqui: o canal já diz
 *  qual servidor abriu o arquivo.
 * ===========================================================================
 */
#include "ulib.h"

#define MAX_NOS    4096u        /* arquivos e pastas: o código do NovaEra todo cabe */
#define MAX_CANAIS   32u
#define NOME_MAX     64u          /* um nome (cabe no texto da mensagem) */

typedef struct {
    u32  usado;
    u32  tipo;               /* ARQ_TIPO_ARQUIVO ou ARQ_TIPO_PASTA */
    u32  pai;                /* índice da pasta onde está (a raiz é 0) */
    char nome[NOME_MAX];
    u8  *dados;
    u32  tam;
    u32  cap;                /* 0 = os dados são do initrd (só leitura) */
    u32  modo;               /* as permissões: 0644, 0755, 01777... */
    u32  dono, grupo;
} no_t;

typedef struct {
    u32 usado;
    u32 no;
    u32 pos;                 /* onde a próxima leitura ou escrita acontece */
    u32 dono;                /* quem abriu: se morrer, o canal é reaproveitado */
} canal_t;

static no_t    nos[MAX_NOS];
static canal_t canais[MAX_CANAIS];

/* Quem fez o pedido atual (o núcleo diz) e se uma pasta no caminho barrou. */
static identidade_t quem_id;
static int negado;

static u32 modo_completo(const no_t *n) { return (n->tipo == ARQ_TIPO_PASTA ? 0x4000u : 0x8000u) | n->modo; }
static int pode(u32 no, u32 quer) { return permite(&quem_id, modo_completo(&nos[no]), nos[no].dono, nos[no].grupo, quer); }

typedef struct {
    u32  usado;
    char pasta[CAMINHO_MAX];
    u32  tam;                /* str_tam(pasta) */
    u32  servidor, dev;
} montagem_t;

static montagem_t montagens[MONT_MAX];

/* O ponto de montagem que contém o caminho (o mais longo); -1 se nenhum. */
static int montagem_de(const char *c)
{
    int melhor = -1;
    for (u32 i = 0; i < MONT_MAX; i++) {
        montagem_t *x = &montagens[i];
        if (!x->usado) continue;
        u32 k = 0;
        while (k < x->tam && c[k] == x->pasta[k]) k++;
        if (k < x->tam || (c[k] != '\0' && c[k] != '/')) continue;
        if (melhor < 0 || x->tam > montagens[melhor].tam) melhor = (int)i;
    }
    return melhor;
}

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
            if (nos[atual].tipo == ARQ_TIPO_PASTA && !pode((u32)atual, PERM_EXECUTAR)) { negado = 1; return -1; }
            return achar_filho((u32)atual, ini, tam);
        }
        if (nos[atual].tipo != ARQ_TIPO_PASTA) return -1;
        if (!pode((u32)atual, PERM_EXECUTAR)) { negado = 1; return -1; }   /* entrar: o x da pasta */
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
        nos[i].modo  = tipo == ARQ_TIPO_PASTA ? 0755 : 0644;
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
                    const u8 *d = nos[f].dados;                  /* um programa (ELF): rwxr-xr-x */
                    if (bytes >= 4 && d[0] == 0x7F && d[1] == 'E' && d[2] == 'L' && d[3] == 'F')
                        nos[f].modo = 0755;
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
    if (sys_identidade(quem, &quem_id) < 0) { erro(quem, ERRO_PID); return; }
    negado = 0;

    if (m->tipo == MSG_LER || m->tipo == MSG_ESCREVER) {
        u32 c = ARQ_CANAL(m->arg[0]);
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
            /* arg[3]: os dados estão na memória de quem chamou (até
             * ESCRITA_MAX); senão, no texto da mensagem */
            u32 max = m->arg[3] ? ESCRITA_MAX : MSG_TEXTO_MAX;
            u32 qtd = m->arg[2] < max ? m->arg[2] : max;
            u32 fim = canais[c].pos + qtd;
            if (!reservar(n, fim)) { erro(quem, ERRO_SEM_MEMORIA); return; }
            if (m->arg[3]) {
                if (sys_copiar_de(quem, n->dados + canais[c].pos, m->arg[3], qtd) < 0) {
                    erro(quem, ERRO_INVALIDO);
                    return;
                }
            } else mem_copia(n->dados + canais[c].pos, m->texto, qtd);
            canais[c].pos = fim;
            if (fim > n->tam) n->tam = fim;
            ok(quem, qtd, 0);
        }
        return;
    }

    if (m->tipo == ARQ_FECHAR) {
        if (ARQ_CANAL(m->arg[0]) < MAX_CANAIS) canais[ARQ_CANAL(m->arg[0])].usado = 0;
        ok(quem, 0, 0);
        return;
    }

    /* ----- a tabela de montagens ----- */
    if (m->tipo == MONT_LISTAR) {
        u32 n = 0;
        for (u32 i = 0; i < MONT_MAX; i++) {
            if (!montagens[i].usado || n++ != m->arg[0]) continue;
            msg_t r;
            msg_zera(&r);
            r.tipo   = MSG_OK;
            r.arg[0] = montagens[i].servidor;
            r.arg[1] = montagens[i].dev;
            str_copia(r.texto, montagens[i].pasta, MSG_TEXTO_MAX);
            sys_responder(quem, &r);
            return;
        }
        erro(quem, ERRO_NAO_ACHADO);
        return;
    }
    if ((m->tipo == MONT_ADICIONAR || m->tipo == MONT_REMOVER) && quem_id.uid != UID_ROOT) {
        erro(quem, ERRO_SEM_PERMISSAO);                  /* montar é coisa do root */
        return;
    }
    if (m->tipo == MONT_ADICIONAR) {
        /* a pasta tem de existir (aqui, na memória) e não pode estar em uso */
        int alvo = resolver(m->texto, 0, 0);
        if (alvo <= 0 || nos[alvo].tipo != ARQ_TIPO_PASTA || montagem_de(m->texto) >= 0) {
            erro(quem, alvo <= 0 ? ERRO_NAO_ACHADO : ERRO_OCUPADO);
            return;
        }
        for (u32 i = 0; i < MONT_MAX; i++)
            if (montagens[i].usado && montagens[i].dev == m->arg[1] &&
                montagens[i].servidor == m->arg[0]) { erro(quem, ERRO_OCUPADO); return; }
        for (u32 i = 0; i < MONT_MAX; i++) {
            if (montagens[i].usado) continue;
            montagens[i].usado    = 1;
            str_copia(montagens[i].pasta, m->texto, CAMINHO_MAX);
            montagens[i].tam      = str_tam(montagens[i].pasta);
            montagens[i].servidor = m->arg[0];
            montagens[i].dev      = m->arg[1];
            ok(quem, 0, 0);
            return;
        }
        erro(quem, ERRO_LOTADO);
        return;
    }
    if (m->tipo == MONT_REMOVER) {
        for (u32 i = 0; i < MONT_MAX; i++) {
            if (!montagens[i].usado || !str_igual(montagens[i].pasta, m->texto)) continue;
            montagens[i].usado = 0;
            ok(quem, montagens[i].servidor, montagens[i].dev);
            return;
        }
        erro(quem, ERRO_NAO_ACHADO);
        return;
    }

    /* ----- um caminho dentro de um disco montado: não é comigo ----- */
    int mt = montagem_de(m->texto);
    if (mt >= 0) {
        responder(quem, ARQ_OUTRO_SERVIDOR, montagens[mt].servidor, 0);
        return;
    }

    int pai = -1;
    const char *ultimo = 0;
    int alvo = resolver(m->texto, (m->tipo == ARQ_INFO || m->tipo == ARQ_LISTAR) ? 0 : &pai,
                        &ultimo);
    if (alvo < 0 && m->texto[0] == '/' && m->texto[1] == '\0') alvo = 0;
    if (negado) { erro(quem, ERRO_SEM_PERMISSAO); return; }

    switch (m->tipo) {
    case ARQ_INFO: {
        if (alvo < 0) { erro(quem, ERRO_NAO_ACHADO); return; }
        msg_t r;
        msg_zera(&r);
        r.tipo   = MSG_OK;
        r.arg[0] = nos[alvo].tipo;
        r.arg[1] = nos[alvo].tam;
        r.arg[2] = modo_completo(&nos[alvo]);
        r.arg[3] = nos[alvo].dono | nos[alvo].grupo << 16;
        sys_responder(quem, &r);
        return;
    }

    case ARQ_PERMISSOES:                         /* chmod: o dono ou o root */
        if (alvo < 0) { erro(quem, ERRO_NAO_ACHADO); return; }
        if (quem_id.uid != UID_ROOT && quem_id.uid != nos[alvo].dono) { erro(quem, ERRO_SEM_PERMISSAO); return; }
        nos[alvo].modo = m->arg[0] & 07777;
        ok(quem, 0, 0);
        return;

    case ARQ_DONO:                               /* chown: só o root */
        if (alvo < 0) { erro(quem, ERRO_NAO_ACHADO); return; }
        if (quem_id.uid != UID_ROOT) { erro(quem, ERRO_SEM_PERMISSAO); return; }
        if (m->arg[0] != 0xFFFFFFFFu) nos[alvo].dono = m->arg[0];
        if (m->arg[1] != 0xFFFFFFFFu) nos[alvo].grupo = m->arg[1];
        ok(quem, 0, 0);
        return;

    case ARQ_LISTAR: {
        if (alvo < 0 || nos[alvo].tipo != ARQ_TIPO_PASTA) { erro(quem, ERRO_NAO_ACHADO); return; }
        if (!pode((u32)alvo, PERM_LER)) { erro(quem, ERRO_SEM_PERMISSAO); return; }
        u32 n = 0;
        for (u32 i = 1; i < MAX_NOS; i++) {
            if (!nos[i].usado || nos[i].pai != (u32)alvo || n++ != m->arg[0]) continue;
            msg_t r;
            msg_zera(&r);
            r.tipo   = MSG_OK;
            r.arg[0] = nos[i].tipo;
            r.arg[1] = nos[i].tam;
            r.arg[2] = modo_completo(&nos[i]);
            r.arg[3] = nos[i].dono | nos[i].grupo << 16;
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
            if (!pode((u32)pai, PERM_ESCREVER | PERM_EXECUTAR)) { erro(quem, ERRO_SEM_PERMISSAO); return; }
            alvo = criar_no((u32)pai, ultimo, ARQ_TIPO_ARQUIVO);
            if (alvo < 0) { erro(quem, ERRO_SEM_MEMORIA); return; }
            nos[alvo].modo  = m->arg[1] ? m->arg[1] & 07777 : 0644;
            nos[alvo].dono  = quem_id.uid;
            nos[alvo].grupo = quem_id.gid;
        } else if (!pode((u32)alvo, modo == ARQ_MODO_LER ? PERM_LER : PERM_ESCREVER)) {
            erro(quem, ERRO_SEM_PERMISSAO);
            return;
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
        if (!pode((u32)pai, PERM_ESCREVER | PERM_EXECUTAR)) { erro(quem, ERRO_SEM_PERMISSAO); return; }
        {
            int f = criar_no((u32)pai, ultimo, ARQ_TIPO_PASTA);
            if (f < 0) { erro(quem, ERRO_SEM_MEMORIA); return; }
            nos[f].modo  = m->arg[0] ? m->arg[0] & 07777 : 0755;
            nos[f].dono  = quem_id.uid;
            nos[f].grupo = quem_id.gid;
        }
        ok(quem, 0, 0);
        return;

    case ARQ_APAGAR:
        if (alvo <= 0) { erro(quem, alvo == 0 ? ERRO_INVALIDO : ERRO_NAO_ACHADO); return; }
        if (!pode((u32)pai, PERM_ESCREVER | PERM_EXECUTAR)) { erro(quem, ERRO_SEM_PERMISSAO); return; }
        if ((nos[pai].modo & MODO_STICKY) && quem_id.uid != UID_ROOT &&       /* /tmp: só o seu */
            quem_id.uid != nos[alvo].dono && quem_id.uid != nos[pai].dono) { erro(quem, ERRO_SEM_PERMISSAO); return; }
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
    nos[0].modo  = 0755;
    carregar_initrd();
    /* /disco: onde o boot monta o disco; /mnt: para montar outros à mão */
    const char *disco = &ARQ_DISCO[1];
    if (achar_filho(0, disco, str_tam(disco)) < 0) criar_no(0, disco, ARQ_TIPO_PASTA);
    if (achar_filho(0, "tmp", 3) < 0) criar_no(0, "tmp", ARQ_TIPO_PASTA);
    if (achar_filho(0, "mnt", 3) < 0) criar_no(0, "mnt", ARQ_TIPO_PASTA);
    if (achar_filho(0, "home", 4) < 0) criar_no(0, "home", ARQ_TIPO_PASTA);
    if (achar_filho(0, "root", 4) < 0) criar_no(0, "root", ARQ_TIPO_PASTA);
    /* as permissões que não são as de sempre (0755 e 0644) */
    quem_id.uid = UID_ROOT;
    int x;
    if ((x = resolver("/tmp", 0, 0)) > 0)        nos[x].modo = 01777;   /* todos escrevem; cada um apaga o seu */
    if ((x = resolver("/root", 0, 0)) > 0)       nos[x].modo = 0700;
    if ((x = resolver("/etc/sombra", 0, 0)) > 0) nos[x].modo = 0600;    /* as senhas: só o root */

    msg_t m;
    for (;;) {
        sys_receber(PID_QUALQUER, &m);
        pedido(&m);
    }
}
