/*
 * usuario/bin/cat.c - mostra o conteúdo de arquivos.
 *   cat a b      os arquivos, um depois do outro
 *   cat          o que chegar pela entrada padrão (de um pipe ou de "<")
 */
#include "ulib.h"

/* Copia tudo de um servidor/canal de entrada para a saída padrão. */
static void copiar(u32 servidor, u32 canal)
{
    char buf[512];
    ambiente_t *a = ambiente();
    long n;
    while ((n = ler_de(servidor, canal, buf, sizeof(buf))) > 0)
        escreve_em(a->saida, a->saida_canal, buf, (u64)n, COR(COR_BRANCO));
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        ambiente_t *a = ambiente();
        copiar(a->entrada, a->entrada_canal);
        return 0;
    }
    int erro = 0;
    for (int i = 1; i < argc; i++) {
        long canal = arq_abrir(argv[i], ARQ_MODO_LER);
        if (canal < 0) {
            escreve_erro("cat: nao encontrado: ");
            escreve_erro(argv[i]);
            escreve_erro("\n");
            erro = 1;
            continue;
        }
        copiar(PID_ARQUIVOS, (u32)canal);
        arq_fechar((u32)canal);
    }
    return erro;
}
