/*
 * ===========================================================================
 *  NovaEra - usuario/contas.c   (PID 10, anel 3)
 *  O servidor de contas: confere senhas e muda quem é um programa.
 *
 *  Um programa comum não pode virar outro usuário: só o root muda a
 *  identidade de um processo (SYS_IDENTIDADE_MUDAR). Este servidor roda
 *  como root, é o único que lê o /etc/sombra (as senhas) e, se a senha
 *  confere, muda a identidade de QUEM PEDIU, enquanto ele espera a resposta:
 *
 *    login, su    CONTAS_ENTRAR: "vire o usuário X" (com a senha de X)
 *    sudo         CONTAS_SUDO: "vire o root" (com a SUA senha, se você
 *                 está no grupo sudo ou wheel)
 *    passwd       CONTAS_CONFERIR e depois CONTAS_SENHA
 *
 *  No Linux, o su e o sudo são programas "setuid": o arquivo diz "rode como
 *  root". O NovaEra não confia em bits de arquivos para isso: quem decide é
 *  este servidor, e o programa só ganha a identidade depois da senha.
 * ===========================================================================
 */
#include "ulib.h"
#include "contas.h"

#define MAX_PIDS 32u
static u32 conferido[MAX_PIDS];          /* pid -> uid + 1 da última senha conferida */

static char *ler_texto(const char *caminho)
{
    u32 tam;
    u8 *d = arq_carregar(caminho, &tam);
    if (!d) return 0;
    u8 *t = umalloc(tam + 1);
    if (t) { mem_copia(t, d, tam); t[tam] = 0; }
    ufree(d);
    return (char *)t;
}

static void responder(u32 para, long erro, u32 a0, u32 a1, const char *texto)
{
    msg_t r;
    msg_zera(&r);
    r.tipo = erro ? MSG_ERRO : MSG_OK;
    r.arg[0] = erro ? (u32)(-erro) : a0;
    r.arg[1] = a1;
    if (texto) str_copia(r.texto, texto, MSG_TEXTO_MAX);
    sys_responder(para, &r);
}

/* "usuario\0senha\0" no texto da mensagem */
static void separar(msg_t *m, char *usuario, char *senha)
{
    u32 i = 0, k = 0;
    while (i < MSG_TEXTO_MAX && m->texto[i] && k + 1 < NOME_CONTA_MAX) usuario[k++] = m->texto[i++];
    usuario[k] = '\0';
    while (i < MSG_TEXTO_MAX && m->texto[i]) i++;
    i++;
    k = 0;
    while (i < MSG_TEXTO_MAX && m->texto[i] && k + 1 < NOME_CONTA_MAX) senha[k++] = m->texto[i++];
    senha[k] = '\0';
}

/* Troca o campo da senha de 'nome' no /etc/sombra. */
static long gravar_senha(const char *nome, const char *hash)
{
    char *sombra = ler_texto("/etc/sombra");
    if (!sombra) return ERRO_NAO_ACHADO;
    u32 cap = str_tam(sombra) + HASH_MAX + 64;
    char *novo = umalloc(cap);
    if (!novo) { ufree(sombra); return ERRO_SEM_MEMORIA; }
    u32 n = 0;
    int achou = 0;
    for (const char *l = sombra; l && *l; ) {
        const char *prox = proxima_linha(l);
        u32 tam = prox ? (u32)(prox - l) : str_tam(l);
        char quem[NOME_CONTA_MAX];
        if (campo(l, 0, quem, sizeof quem) && str_igual(quem, nome)) {
            achou = 1;
            const char *resto = l;                       /* depois do 2º ':' */
            u32 dois = 0;
            while (resto < l + tam && dois < 2) { if (*resto == ':') dois++; resto++; }
            str_copia(novo + n, nome, cap - n); n += str_tam(nome);
            novo[n++] = ':';
            str_copia(novo + n, hash, cap - n); n += str_tam(hash);
            novo[n++] = ':';
            while (resto < l + tam && *resto != '\n') novo[n++] = *resto++;
            novo[n++] = '\n';
        } else {
            mem_copia(novo + n, l, tam);
            n += tam;
            if (!prox) novo[n++] = '\n';
        }
        l = prox;
    }
    ufree(sombra);
    if (!achou) { ufree(novo); return ERRO_NAO_ACHADO; }
    long c = arq_abrir_com("/etc/sombra", ARQ_MODO_ESCREVER, 0600);
    long r = c < 0 ? c : escreve_em(ARQ_SERVIDOR(c), (u32)c, novo, n, 0);
    if (c >= 0) arq_fechar((u32)c);
    ufree(novo);
    return r < 0 ? r : 0;
}

static void pedido(msg_t *m)
{
    u32 quem = m->origem;
    identidade_t eu;
    if (sys_identidade(quem, &eu) < 0) { responder(quem, ERRO_PID, 0, 0, 0); return; }

    char usuario[NOME_CONTA_MAX], senha[NOME_CONTA_MAX];
    separar(m, usuario, senha);
    if (m->tipo == CONTAS_SUDO) {                      /* só a senha, de quem pede */
        str_copia(senha, usuario, sizeof senha);
        usuario[0] = '\0';
    }

    char *passwd = ler_texto("/etc/passwd"), *group = ler_texto("/etc/group");
    char *sombra = ler_texto("/etc/sombra");
    conta_t c;
    char guardada[HASH_MAX];
    long erro = 0;
    if (!passwd || !group || !sombra) { erro = ERRO_NAO_ACHADO; goto fim; }

    if (m->tipo == CONTAS_SUDO) {
        if (eu.uid == UID_ROOT) { responder(quem, 0, 0, 0, "/root"); goto limpa; }
        if (!conta_procura(passwd, 0, eu.uid, &c)) { erro = ERRO_NAO_ACHADO; goto fim; }
        if (!grupo_tem(group, "sudo", c.nome) && !grupo_tem(group, "wheel", c.nome)) {
            erro = ERRO_SEM_PERMISSAO;                 /* não está entre os que podem */
            goto fim;
        }
        if (!sombra_de(sombra, c.nome, guardada, sizeof guardada) || !senha_confere(guardada, senha)) {
            erro = ERRO_INVALIDO;                      /* senha errada */
            goto fim;
        }
        identidade_t root;
        mem_zera(&root, sizeof root);
        if (sys_identidade_mudar(quem, &root) < 0) { erro = ERRO_PID; goto fim; }
        responder(quem, 0, 0, 0, "/root");
        goto limpa;
    }

    if (!conta_procura(passwd, usuario, 0, &c)) { erro = ERRO_NAO_ACHADO; goto fim; }

    switch (m->tipo) {
    case CONTAS_ENTRAR:
    case CONTAS_CONFERIR:
        if (eu.uid != UID_ROOT || m->tipo == CONTAS_CONFERIR) {
            if (!sombra_de(sombra, c.nome, guardada, sizeof guardada) || !senha_confere(guardada, senha)) {
                erro = ERRO_INVALIDO;
                goto fim;
            }
        }
        if (m->tipo == CONTAS_CONFERIR) {
            if (quem < MAX_PIDS) conferido[quem] = c.uid + 1;
            responder(quem, 0, c.uid, c.gid, c.casa);
            goto limpa;
        }
        {
            identidade_t nova;
            conta_identidade(group, &c, &nova);
            if (sys_identidade_mudar(quem, &nova) < 0) { erro = ERRO_PID; goto fim; }
        }
        responder(quem, 0, c.uid, c.gid, c.casa);
        goto limpa;

    case CONTAS_SENHA: {
        int pode = eu.uid == UID_ROOT ||
                   (eu.uid == c.uid && quem < MAX_PIDS && conferido[quem] == c.uid + 1);
        if (quem < MAX_PIDS) conferido[quem] = 0;
        if (!pode) { erro = ERRO_SEM_PERMISSAO; goto fim; }
        char sal[20], hash[HASH_MAX];
        sal_novo(sal);
        senha_hash(senha, sal, hash);
        erro = gravar_senha(c.nome, hash);
        if (!erro) { responder(quem, 0, 0, 0, 0); goto limpa; }
        goto fim;
    }
    }
    erro = ERRO_INVALIDO;
fim:
    responder(quem, erro, 0, 0, 0);
limpa:
    mem_zera(senha, sizeof senha);
    ufree(passwd);
    ufree(group);
    ufree(sombra);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    msg_t m;
    for (;;) {
        sys_receber(PID_QUALQUER, &m);
        pedido(&m);
        mem_zera(&m, sizeof m);                     /* a senha não fica na memória */
    }
}
