/*
 * usuario/bin/ls.c - lista o que tem numa pasta.
 *   ls            a pasta atual
 *   ls /bin /etc  outras pastas
 */
#include "ulib.h"

static int listar(const char *pasta, int com_titulo)
{
    u32 tam;
    long tipo = arq_info(pasta, &tam);
    if (tipo < 0) {
        escreve("ls: nao encontrado: ", COR(COR_VERMELHO_CLARO));
        escreve(pasta, COR(COR_BRANCO));
        escreve("\n", 0);
        return 1;
    }
    if (tipo == ARQ_TIPO_ARQUIVO) {                 /* "ls arquivo": só ele */
        escreve(pasta, COR(COR_BRANCO));
        escreve("\n", 0);
        return 0;
    }
    if (com_titulo) {
        escreve(pasta, COR(COR_CIANO_CLARO));
        escreve(":\n", COR(COR_CIANO_CLARO));
    }
    char nome[MSG_TEXTO_MAX];
    for (u32 i = 0; (tipo = arq_listar(pasta, i, nome, &tam)) > 0; i++) {
        if (tipo == ARQ_TIPO_PASTA) {
            escreve(nome, COR(COR_AZUL_CLARO));
            escreve("/\n", COR(COR_AZUL_CLARO));
        } else {
            escreve(nome, COR(COR_BRANCO));
            u32 n = str_tam(nome);
            while (n++ < 20) escreve(" ", 0);
            escreve_num(tam, COR(COR_CINZA_CLARO));
            escreve(" bytes\n", COR(COR_CINZA_CLARO));
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) return listar(".", 0);
    int erro = 0;
    for (int i = 1; i < argc; i++) erro |= listar(argv[i], argc > 2);
    return erro;
}
