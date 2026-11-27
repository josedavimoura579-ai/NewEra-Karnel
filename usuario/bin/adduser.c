/*
 * usuario/bin/adduser.c - cria um usuário (só o root: sudo adduser ...).
 *
 *   adduser joao           cria o joao (pede a senha) e a pasta /home/joao
 *   adduser joao sudo      põe o joao no grupo sudo (ele passa a poder
 *                          usar o sudo)
 *
 * O que ele faz é só escrever três linhas:
 *   /etc/passwd   joao:x:1000:1000:joao:/home/joao:/bin/sh
 *   /etc/group    joao:x:1000:
 *   /etc/sombra   joao:$nefs$...:
 */
#include "contas.h"

static int nome_valido(const char *n)
{
    if (!*n || str_tam(n) >= NOME_CONTA_MAX) return 0;
    if (!(*n >= 'a' && *n <= 'z') && *n != '_') return 0;
    for (; *n; n++)
        if (!((*n >= 'a' && *n <= 'z') || (*n >= '0' && *n <= '9') || *n == '_' || *n == '-')) return 0;
    return 1;
}

/* Põe uma linha no fim de um arquivo (com '\n' antes, se faltar). */
static int anexar(const char *caminho, const char *linha)
{
    char *atual = texto_de(caminho);
    int falta_fim = atual && atual[0] && atual[str_tam(atual) - 1] != '\n';
    ufree(atual);
    long c = arq_abrir(caminho, ARQ_MODO_ANEXAR);
    if (c < 0) return -1;
    long r = 0;
    if (falta_fim) r = escreve_em(ARQ_SERVIDOR(c), (u32)c, "\n", 1, 0);
    if (r >= 0) r = escreve_em(ARQ_SERVIDOR(c), (u32)c, linha, str_tam(linha), 0);
    arq_fechar((u32)c);
    return r < 0 ? -1 : 0;
}

static void junta(char *d, u32 max, const char *s) { u32 k = str_tam(d); str_copia(d + k, s, max - k); }
static void junta_num(char *d, u32 max, u32 n) { char b[12]; junta(d, max, num_texto(n, b)); }

/* adduser NOME GRUPO: acrescenta NOME aos membros do GRUPO no /etc/group. */
static int no_grupo_novo(const char *nome, const char *grupo)
{
    char *group = texto_de("/etc/group");
    if (!group) return 1;
    if (grupo_gid(group, grupo) < 0) { escreve_erro("adduser: esse grupo nao existe\n"); return 1; }
    if (grupo_tem(group, grupo, nome)) { escreve("Ja esta no grupo.\n", COR(COR_CINZA_CLARO)); return 0; }
    u32 cap = str_tam(group) + NOME_CONTA_MAX + 4;
    char *novo = umalloc(cap);
    u32 n = 0;
    for (const char *l = group; l && *l; ) {
        const char *prox = proxima_linha(l);
        u32 tam = prox ? (u32)(prox - l) : str_tam(l);
        char g[NOME_CONTA_MAX];
        u32 fim = tam;
        while (fim && (l[fim - 1] == '\n' || l[fim - 1] == '\r')) fim--;
        mem_copia(novo + n, l, fim);
        n += fim;
        if (campo(l, 0, g, sizeof g) && str_igual(g, grupo)) {
            if (l[fim - 1] != ':') novo[n++] = ',';      /* já tem membros */
            mem_copia(novo + n, nome, str_tam(nome));
            n += str_tam(nome);
        }
        novo[n++] = '\n';
        l = prox;
    }
    long c = arq_abrir("/etc/group", ARQ_MODO_ESCREVER);
    long r = c < 0 ? c : escreve_em(ARQ_SERVIDOR(c), (u32)c, novo, n, 0);
    if (c >= 0) arq_fechar((u32)c);
    if (r < 0) { escreve_erro("adduser: nao consegui gravar o /etc/group\n"); return 1; }
    escreve(nome, COR(COR_BRANCO));
    escreve(" agora esta no grupo ", COR(COR_CINZA_CLARO));
    escreve(grupo, COR(COR_BRANCO));
    escreve(" (vale no proximo login ou su).\n", COR(COR_CINZA_CLARO));
    return 0;
}

int main(int argc, char **argv)
{
    identidade_t eu;
    sys_identidade(0, &eu);
    if (argc < 2) { escreve_erro("uso: adduser NOME     ou     adduser NOME GRUPO\n"); return 1; }
    if (eu.uid != UID_ROOT) { escreve_erro("adduser: precisa ser o root (use: sudo adduser ...)\n"); return 1; }
    const char *nome = argv[1];
    char *passwd = texto_de("/etc/passwd"), *group = texto_de("/etc/group");
    if (!passwd || !group) { escreve_erro("adduser: nao achei o /etc/passwd e o /etc/group\n"); return 1; }
    conta_t c;
    if (argc > 2) {
        if (!conta_procura(passwd, nome, 0, &c)) { escreve_erro("adduser: esse usuario nao existe\n"); return 1; }
        return no_grupo_novo(nome, argv[2]);
    }
    if (!nome_valido(nome)) {
        escreve_erro("adduser: use letras minusculas, numeros, _ e - (comecando com letra)\n");
        return 1;
    }
    if (conta_procura(passwd, nome, 0, &c) || grupo_gid(group, nome) >= 0) {
        escreve_erro("adduser: esse nome ja existe\n");
        return 1;
    }

    /* o próximo uid livre a partir de 1000 (o costume do Linux) */
    u32 uid = 1000;
    for (const char *l = passwd; l && *l; l = proxima_linha(l)) {
        char t[16];
        if (campo(l, 2, t, sizeof t) && texto_num(t) >= uid && texto_num(t) < 60000) uid = texto_num(t) + 1;
    }
    u32 gid = uid;
    while (grupo_nome(group, gid, c.nome, sizeof c.nome)) gid++;

    char senha[NOME_CONTA_MAX], de_novo[NOME_CONTA_MAX];
    ler_linha("Senha nova: ", senha, sizeof senha, 0);
    ler_linha("Repita a senha: ", de_novo, sizeof de_novo, 0);
    if (!str_igual(senha, de_novo)) { escreve_erro("adduser: as duas nao sao iguais\n"); return 1; }
    char sal[20], hash[HASH_MAX];
    sal_novo(sal);
    senha_hash(senha, sal, hash);
    mem_zera(senha, sizeof senha);
    mem_zera(de_novo, sizeof de_novo);

    char casa[CAMINHO_MAX], linha[256];
    str_copia(casa, "/home/", sizeof casa);
    junta(casa, sizeof casa, nome);

    linha[0] = '\0';
    junta(linha, sizeof linha, nome); junta(linha, sizeof linha, ":x:");
    junta_num(linha, sizeof linha, uid); junta(linha, sizeof linha, ":");
    junta_num(linha, sizeof linha, gid); junta(linha, sizeof linha, ":");
    junta(linha, sizeof linha, nome); junta(linha, sizeof linha, ":");
    junta(linha, sizeof linha, casa); junta(linha, sizeof linha, ":/bin/sh\n");
    int r = anexar("/etc/passwd", linha);

    linha[0] = '\0';
    junta(linha, sizeof linha, nome); junta(linha, sizeof linha, ":x:");
    junta_num(linha, sizeof linha, gid); junta(linha, sizeof linha, ":\n");
    r |= anexar("/etc/group", linha);

    linha[0] = '\0';
    junta(linha, sizeof linha, nome); junta(linha, sizeof linha, ":");
    junta(linha, sizeof linha, hash); junta(linha, sizeof linha, ":\n");
    r |= anexar("/etc/sombra", linha);
    if (r) { escreve_erro("adduser: nao consegui gravar em /etc\n"); return 1; }

    /* a pasta pessoal, dele */
    if (arq_info(casa, 0) < 0) arq_pasta(casa);
    arq_dono(casa, uid, gid);
    arq_permissoes(casa, 0755);

    escreve("Usuario ", COR(COR_CINZA_CLARO));
    escreve(nome, COR(COR_VERDE_CLARO));
    escreve(" criado (uid ", COR(COR_CINZA_CLARO));
    escreve_num(uid, COR(COR_BRANCO));
    escreve(", pasta ", COR(COR_CINZA_CLARO));
    escreve(casa, COR(COR_BRANCO));
    escreve(").\n", COR(COR_CINZA_CLARO));
    return 0;
}
