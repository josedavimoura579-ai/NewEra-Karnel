/*
 * usuario/bin/filho.c - um programa que cria outro: executa "ola" com
 * argumentos e espera ele terminar (executar() + SYS_ESPERAR).
 */
#include "ulib.h"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    char *args[] = { "ola", "vim", "do", "filho", 0 };
    long pid = executar("/bin/ola", args, 0, 0, 0, 0);
    if (pid < 0) {
        escreve("nao consegui criar o processo\n", COR(COR_VERMELHO_CLARO));
        return 1;
    }
    escreve("filho: criei o processo ", COR(COR_CIANO_CLARO));
    escreve_num((u32)pid, COR(COR_BRANCO));
    escreve(" e estou esperando...\n", COR(COR_CIANO_CLARO));
    long codigo = sys_esperar((u32)pid);
    escreve("filho: ele terminou com codigo ", COR(COR_CIANO_CLARO));
    escreve_num((u32)codigo, COR(COR_BRANCO));
    escreve("\n", COR(COR_BRANCO));
    return 0;
}
