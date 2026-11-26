/*
 * usuario/contas.h - os usuários e grupos do sistema, como no Unix:
 *
 *   /etc/passwd   nome:x:uid:gid:descrição:pasta pessoal:shell
 *   /etc/group    nome:x:gid:membro1,membro2
 *   /etc/sombra   nome:senha:   (o /etc/shadow; só o root lê)
 *
 * A senha nunca é guardada: só o SHA-256 dela com um "sal" (bytes
 * aleatórios, diferentes para cada usuário), repetido 1000 vezes, para
 * deixar o chute lento:  $nefs$<sal>$<resultado>
 * Campo vazio: sem senha. Começando com ! ou *: conta trancada.
 *
 * Funciona sobre o texto dos arquivos (quem lê é quem usa).
 */
#ifndef CONTAS_H
#define CONTAS_H
#include "ulib.h"
#include "sha256.h"

#define NOME_CONTA_MAX 32u

typedef struct {
    char nome[NOME_CONTA_MAX];
    u32  uid, gid;
    char casa[CAMINHO_MAX];      /* a pasta pessoal */
    char shell[CAMINHO_MAX];
} conta_t;

/* O campo número 'k' (a partir de 0) de uma linha "a:b:c", copiado. */
static inline int campo(const char *linha, u32 k, char *saida, u32 max)
{
    const char *p = linha;
    for (u32 i = 0; i < k; i++) {
        while (*p && *p != ':' && *p != '\n') p++;
        if (*p != ':') return 0;
        p++;
    }
    u32 n = 0;
    while (p[n] && p[n] != ':' && p[n] != '\n' && n + 1 < max) { saida[n] = p[n]; n++; }
    saida[n] = '\0';
    return 1;
}

static inline const char *proxima_linha(const char *p)
{
    while (*p && *p != '\n') p++;
    return *p ? p + 1 : 0;
}

/* Acha a conta pelo nome (se nome != 0) ou pelo uid, no texto do passwd. */
static inline int conta_procura(const char *passwd, const char *nome, u32 uid, conta_t *c)
{
    for (const char *l = passwd; l && *l; l = proxima_linha(l)) {
        char t[CAMINHO_MAX];
        if (*l == '#' || *l == '\n') continue;
        if (!campo(l, 0, c->nome, sizeof c->nome)) continue;
        if (!campo(l, 2, t, sizeof t)) continue;
        c->uid = texto_num(t);
        if (!campo(l, 3, t, sizeof t)) continue;
        c->gid = texto_num(t);
        if (!campo(l, 5, c->casa, sizeof c->casa)) c->casa[0] = '\0';
        if (!campo(l, 6, c->shell, sizeof c->shell) || !c->shell[0]) str_copia(c->shell, "/bin/sh", sizeof c->shell);
        if (nome ? str_igual(c->nome, nome) : c->uid == uid) return 1;
    }
    return 0;
}

/* O nome de um grupo pelo gid (ou 0). */
static inline int grupo_nome(const char *group, u32 gid, char *nome, u32 max)
{
    for (const char *l = group; l && *l; l = proxima_linha(l)) {
        char t[16];
        if (campo(l, 2, t, sizeof t) && texto_num(t) == gid && campo(l, 0, nome, max)) return 1;
    }
    return 0;
}

/* O gid de um grupo pelo nome (-1 se não existe). */
static inline long grupo_gid(const char *group, const char *nome)
{
    for (const char *l = group; l && *l; l = proxima_linha(l)) {
        char n[NOME_CONTA_MAX], t[16];
        if (campo(l, 0, n, sizeof n) && str_igual(n, nome) && campo(l, 2, t, sizeof t)) return texto_num(t);
    }
    return -1;
}

/* O usuário está na lista de membros do grupo (o 4º campo)? */
static inline int grupo_tem(const char *group, const char *grupo, const char *usuario)
{
    for (const char *l = group; l && *l; l = proxima_linha(l)) {
        char n[NOME_CONTA_MAX], membros[256];
        if (!campo(l, 0, n, sizeof n) || !str_igual(n, grupo)) continue;
        if (!campo(l, 3, membros, sizeof membros)) return 0;
        for (char *p = membros; *p; ) {
            char *ini = p;
            while (*p && *p != ',') p++;
            char fim = *p;
            *p = '\0';
            if (str_igual(ini, usuario)) return 1;
            if (!fim) break;
            p++;
        }
        return 0;
    }
    return 0;
}

/* A identidade completa de uma conta: o grupo dela e os grupos extras. */
static inline void conta_identidade(const char *group, const conta_t *c, identidade_t *id)
{
    mem_zera(id, sizeof *id);
    id->uid = c->uid;
    id->gid = c->gid;
    for (const char *l = group; l && *l && id->n < GRUPOS_MAX; l = proxima_linha(l)) {
        char n[NOME_CONTA_MAX], t[16];
        if (!campo(l, 0, n, sizeof n) || !campo(l, 2, t, sizeof t)) continue;
        u32 g = texto_num(t);
        if (g != c->gid && grupo_tem(group, n, c->nome)) id->grupos[id->n++] = g;
    }
}

static inline void hex(const u8 *d, u32 n, char *saida)
{
    for (u32 i = 0; i < n; i++) {
        saida[2 * i]     = "0123456789abcdef"[d[i] >> 4];
        saida[2 * i + 1] = "0123456789abcdef"[d[i] & 15];
    }
    saida[2 * n] = '\0';
}

/* "$nefs$" + sal (16 letras) + "$" + 64 letras: cabe em 96 bytes. */
#define HASH_MAX 96u

static inline void senha_hash(const char *senha, const char *sal, char *saida)
{
    u8 h[32];
    sha256_t s;
    u32 ns = str_tam(sal), np = str_tam(senha);
    sha256_inicio(&s);
    sha256_mais(&s, sal, ns);
    sha256_mais(&s, senha, np);
    sha256_fim(&s, h);
    for (u32 i = 0; i < 1000; i++) {               /* lento de propósito */
        sha256_inicio(&s);
        sha256_mais(&s, h, 32);
        sha256_mais(&s, sal, ns);
        sha256_mais(&s, senha, np);
        sha256_fim(&s, h);
    }
    str_copia(saida, "$nefs$", HASH_MAX);
    str_copia(saida + 6, sal, HASH_MAX - 6);
    u32 k = str_tam(saida);
    saida[k++] = '$';
    hex(h, 32, saida + k);
}

/* Um sal novo, a partir do relógio da CPU (rdtsc). */
u64 rdtsc(void);                                     /* sys.s */
static inline void sal_novo(char *saida)
{
    u64 x = rdtsc() ^ ((u64)sys_pid() << 40) ^ sys_tempo();
    u8 b[8];
    for (u32 i = 0; i < 8; i++) { x = x * 6364136223846793005ull + 1442695040888963407ull; b[i] = (u8)(x >> 56); }
    hex(b, 8, saida);
}

/* A senha confere com o campo do /etc/sombra? */
static inline int senha_confere(const char *campo_sombra, const char *senha)
{
    if (!campo_sombra[0]) return 1;                    /* sem senha */
    if (campo_sombra[0] == '!' || campo_sombra[0] == '*') return 0;   /* trancada */
    if (!mem_igual(campo_sombra, "$nefs$", 6)) return 0;
    char sal[32];
    u32 i = 0;
    const char *p = campo_sombra + 6;
    while (p[i] && p[i] != '$' && i + 1 < sizeof sal) { sal[i] = p[i]; i++; }
    sal[i] = '\0';
    char h[HASH_MAX];
    senha_hash(senha, sal, h);
    /* compara tudo, sem parar no primeiro byte diferente */
    u32 n = str_tam(h), m = str_tam(campo_sombra), dif = n ^ m;
    for (u32 k = 0; k < n && k < m; k++) dif |= (u32)(h[k] ^ campo_sombra[k]);
    return dif == 0;
}

/* O campo da senha de um usuário no texto do /etc/sombra (0 se não há). */
static inline int sombra_de(const char *sombra, const char *nome, char *saida, u32 max)
{
    for (const char *l = sombra; l && *l; l = proxima_linha(l)) {
        char n[NOME_CONTA_MAX];
        if (campo(l, 0, n, sizeof n) && str_igual(n, nome)) return campo(l, 1, saida, max);
    }
    return 0;
}

/* ------------------- Para os programas (login, su, sudo...) -------------- */

/* Lê uma linha do teclado; com eco = 0, sem mostrar (uma senha). */
static inline long ler_linha(const char *pergunta, char *buf, u32 max, int eco)
{
    escreve(pergunta, COR(COR_BRANCO));
    if (!eco) terminal_eco(0);
    long n = ler(buf, max - 1);
    if (!eco) terminal_eco(1);              /* o Enter o terminal mostra sempre */
    if (n < 0) n = 0;
    buf[n] = '\0';
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
    return n;
}

/* Um pedido ao servidor de contas: texto = "usuario\0senha\0". */
static inline long contas_pedir(u32 tipo, const char *usuario, const char *senha, msg_t *r)
{
    msg_zera(r);
    r->tipo = tipo;
    u32 nu = str_tam(usuario), ns = str_tam(senha);
    if (nu >= NOME_CONTA_MAX || ns >= NOME_CONTA_MAX) return ERRO_INVALIDO;
    mem_copia(r->texto, usuario, nu);
    mem_copia(r->texto + nu + 1, senha, ns);
    long e = sys_chamar(PID_CONTAS, r);       /* a resposta cobre a senha */
    if (e < 0) { mem_zera(r->texto, MSG_TEXTO_MAX); return e; }
    return r->tipo == MSG_ERRO ? -(long)r->arg[0] : 0;
}

/* O texto inteiro de um arquivo, terminado em '\0' (ufree depois). */
static inline char *texto_de(const char *caminho)
{
    u32 tam;
    u8 *d = arq_carregar(caminho, &tam);
    if (!d) return 0;
    char *t = umalloc(tam + 1);
    if (t) { mem_copia(t, d, tam); t[tam] = '\0'; }
    ufree(d);
    return t;
}

/* Roda um programa (o caminho, ou /bin/NOME) com a entrada e a saída
 * herdadas e espera ele terminar; devolve o código de saída. */
static inline long rodar_e_esperar(const char *nome, char **argv)
{
    char caminho[CAMINHO_MAX];
    if (str_tem(nome, '/')) str_copia(caminho, nome, sizeof caminho);
    else { str_copia(caminho, "/bin/", sizeof caminho); str_copia(caminho + 5, nome, sizeof caminho - 5); }
    long pid = executar(caminho, argv, 0, 0, 0, 0);
    if (pid < 0) return pid;
    return sys_esperar((u32)pid);
}
#endif
