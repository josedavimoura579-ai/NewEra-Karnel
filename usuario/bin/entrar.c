/*
 * usuario/bin/entrar.c - entra num sistema instalado num disco montado,
 * antes de ele dar boot (o arch-chroot do Arch). Só o root.
 *
 *   entrar /mnt
 *
 * Abre um shell em que "/" é o /mnt: o /etc/passwd é o /mnt/etc/passwd, o
 * adduser e o passwd mexem no sistema novo. Para voltar: sair.
 */
#include "ulib.h"

int main(int argc, char **argv)
{
    identidade_t eu;
    sys_identidade(0, &eu);
    if (argc != 2) { escreve_erro("uso: entrar PASTA    (ex.: entrar /mnt)\n"); return 1; }
    if (eu.uid != UID_ROOT) { escreve_erro("entrar: precisa ser o root (use: sudo entrar ...)\n"); return 1; }
    char raiz[CAMINHO_MAX], c[CAMINHO_MAX];
    if (caminho_absoluto(argv[1], raiz) < 0 || arq_info(raiz, 0) != ARQ_TIPO_PASTA) {
        escreve_erro("entrar: essa pasta nao existe\n");
        return 1;
    }
    str_copia(c, raiz, sizeof c);
    str_copia(c + str_tam(c), "/bin/sh", sizeof c - str_tam(c));
    if (arq_info(c, 0) != ARQ_TIPO_ARQUIVO) {
        escreve_erro("entrar: nao ha um sistema la (falta o /bin/sh; antes: instalar-sistema)\n");
        return 1;
    }
    if (sys_raiz(raiz, 0) < 0) { escreve_erro("entrar: nao deu\n"); return 1; }
    ulib_raiz_lida = 0;                           /* daqui em diante, tudo dentro dela */
    escreve("Dentro de ", COR(COR_CINZA_CLARO));
    escreve(raiz, COR(COR_BRANCO));
    escreve(": aqui, / e o sistema novo. Para voltar: sair\n", COR(COR_CINZA_CLARO));
    sinal_tratar(SINAL_INT, (void (*)(int))SINAL_IGNORAR);
    char *args[] = { "sh", 0 };
    long pid = executar("/bin/sh", args, 0, 0, 0, 0);
    if (pid < 0) { escreve_erro("entrar: nao consegui rodar o /bin/sh\n"); return 1; }
    sys_esperar((u32)pid);
    return 0;
}
