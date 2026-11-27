/*
 * usuario/bin/gerar-montagens.c - escreve no /etc/montagens do sistema novo
 * os discos que estão montados dentro dele agora (o genfstab do Arch).
 *
 *   montar sda1 /mnt
 *   mkdir /mnt/dados
 *   montar sda2 /mnt/dados
 *   gerar-montagens /mnt          ->  /mnt/etc/montagens ganha "sda2 /dados"
 */
#include "ulib.h"

int main(int argc, char **argv)
{
    if (argc != 2) { escreve_erro("uso: gerar-montagens PASTA   (ex.: gerar-montagens /mnt)\n"); return 1; }
    char raiz[CAMINHO_MAX], arq[CAMINHO_MAX];
    if (caminho_absoluto(argv[1], raiz) < 0) return 1;
    str_copia(arq, raiz, sizeof arq);
    str_copia(arq + str_tam(arq), "/etc/montagens", sizeof arq - str_tam(arq));
    u32 nr = str_tam(raiz);
    long f = arq_abrir(arq, ARQ_MODO_ANEXAR);
    if (f < 0) { escreve_erro("gerar-montagens: nao consegui abrir "); escreve_erro(arq); escreve_erro("\n"); return 1; }
    char pasta[CAMINHO_MAX], nome[8], linha[CAMINHO_MAX + 16];
    u32 srv, dev, n = 0;
    for (u32 i = 0; montagem(i, pasta, &srv, &dev) == 0; i++) {
        /* só o que está DENTRO da raiz nova (e não ela mesma) */
        if (str_tam(pasta) <= nr || !mem_igual(pasta, raiz, nr) || pasta[nr] != '/') continue;
        dispositivo_nome(dev, nome);
        str_copia(linha, nome, sizeof linha);
        u32 k = str_tam(linha);
        linha[k++] = ' ';
        str_copia(linha + k, pasta + nr, sizeof linha - k);
        k = str_tam(linha);
        linha[k++] = '\n';
        linha[k] = '\0';
        escreve_em(ARQ_SERVIDOR(f), (u32)f, linha, k, 0);
        escreve(linha, COR(COR_BRANCO));
        n++;
    }
    arq_fechar((u32)f);
    if (!n) escreve("(nada montado dentro de ", COR(COR_CINZA_CLARO));
    if (!n) { escreve(raiz, COR(COR_CINZA_CLARO)); escreve(": nada a acrescentar)\n", COR(COR_CINZA_CLARO)); }
    return 0;
}
