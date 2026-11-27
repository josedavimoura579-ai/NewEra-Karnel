/*
 * usuario/bin/ls.c - lista o que tem numa pasta.
 *   ls            a pasta atual
 *   ls /bin /etc  outras pastas
 *   ls -l         com as permissões, o dono e o grupo:
 *                 drwxr-xr-x root root    0 bin
 *                 -rw------- root root   20 sombra
 *                 (d = pasta; depois rwx do dono, do grupo e dos outros)
 */
#include "contas.h"

static int longo;
static char *passwd, *group;

static void permissoes(u32 modo, char *s)
{
    s[0] = (modo & 0xF000) == 0x4000 ? 'd' : '-';
    const char *rwx = "rwxrwxrwx";
    for (u32 i = 0; i < 9; i++) s[1 + i] = (modo & (0400u >> i)) ? rwx[i] : '-';
    if (modo & MODO_STICKY) s[9] = (modo & 1) ? 't' : 'T';
    s[10] = '\0';
}

static void coluna(const char *s, u32 largura, u8 cor)
{
    escreve(s, cor);
    for (u32 n = str_tam(s); n < largura; n++) escreve(" ", 0);
}

static void linha_longa(const char *nome, const arq_estado_t *e)
{
    char p[12], dono[NOME_CONTA_MAX], grupo[NOME_CONTA_MAX], num[12];
    conta_t c;
    permissoes(e->modo ? e->modo : (e->tipo == ARQ_TIPO_PASTA ? 0x41FF : 0x81FF), p);
    if (passwd && conta_procura(passwd, 0, e->dono, &c)) str_copia(dono, c.nome, sizeof dono);
    else num_texto(e->dono, dono);
    if (!group || !grupo_nome(group, e->grupo, grupo, sizeof grupo)) num_texto(e->grupo, grupo);
    coluna(p, 11, COR(COR_CINZA_CLARO));
    coluna(dono, 9, COR(COR_AMARELO));
    coluna(grupo, 9, COR(COR_AMARELO));
    num_texto(e->tipo == ARQ_TIPO_PASTA ? 0 : e->tam, num);
    for (u32 n = str_tam(num); n < 9; n++) escreve(" ", 0);
    escreve(num, COR(COR_CINZA_CLARO));
    escreve(" ", 0);
    escreve(nome, e->tipo == ARQ_TIPO_PASTA ? COR(COR_AZUL_CLARO) : COR(COR_BRANCO));
    escreve("\n", 0);
}

static int listar(const char *pasta, int com_titulo)
{
    arq_estado_t e;
    long r = arq_estado(pasta, &e);
    if (r < 0) {
        escreve_erro(r == ERRO_SEM_PERMISSAO ? "ls: permissao negada: " : "ls: nao encontrado: ");
        escreve_erro(pasta);
        escreve_erro("\n");
        return 1;
    }
    if (e.tipo == ARQ_TIPO_ARQUIVO) {               /* "ls arquivo": só ele */
        if (longo) linha_longa(pasta, &e);
        else { escreve(pasta, COR(COR_BRANCO)); escreve("\n", 0); }
        return 0;
    }
    if (com_titulo) {
        escreve(pasta, COR(COR_CIANO_CLARO));
        escreve(":\n", COR(COR_CIANO_CLARO));
    }
    char nome[MSG_TEXTO_MAX];
    u32 i = 0;
    for (; (r = arq_listar_estado(pasta, i, nome, &e)) == 0; i++) {
        if (longo) { linha_longa(nome, &e); continue; }
        if (e.tipo == ARQ_TIPO_PASTA) {
            escreve(nome, COR(COR_AZUL_CLARO));
            escreve("/\n", COR(COR_AZUL_CLARO));
        } else {
            escreve(nome, COR(COR_BRANCO));
            u32 n = str_tam(nome);
            while (n++ < 20) escreve(" ", 0);
            escreve_num(e.tam, COR(COR_CINZA_CLARO));
            escreve(" bytes\n", COR(COR_CINZA_CLARO));
        }
    }
    if (i == 0 && r == ERRO_SEM_PERMISSAO) {
        escreve_erro("ls: permissao negada: ");
        escreve_erro(pasta);
        escreve_erro("\n");
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i = 1;
    if (i < argc && str_igual(argv[i], "-l")) {
        longo = 1;
        i++;
        passwd = texto_de("/etc/passwd");
        group = texto_de("/etc/group");
    }
    if (i >= argc) return listar(".", 0);
    int erro = 0;
    int varias = argc - i > 1;
    for (; i < argc; i++) erro |= listar(argv[i], varias);
    return erro;
}
