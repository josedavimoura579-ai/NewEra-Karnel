/*
 * usuario/bin/init.c - o primeiro programa do sistema instalado num disco.
 *
 * O shell do boot (PID 3) monta a raiz e chama o /bin/init, que:
 *   1. monta os discos listados no /etc/montagens (o /etc/fstab do Linux):
 *        # dispositivo   pasta
 *        sda2            /dados
 *   2. mostra o login; quando a pessoa sai, mostra de novo.
 */
#include "contas.h"
#include "sistema.h"     /* gerado de sistema.conf pelo build.sh */

static void montar_tudo(void)
{
    char *t = texto_de("/etc/montagens");
    if (!t) return;
    for (const char *l = t; l && *l; l = proxima_linha(l)) {
        while (*l == ' ' || *l == '\t') l++;
        if (*l == '#' || *l == '\n' || !*l) continue;
        char dev[16], pasta[CAMINHO_MAX];
        u32 k = 0;
        while (*l && *l != ' ' && *l != '\t' && *l != '\n' && k + 1 < sizeof dev) dev[k++] = *l++;
        dev[k] = '\0';
        while (*l == ' ' || *l == '\t') l++;
        k = 0;
        while (*l && *l != ' ' && *l != '\t' && *l != '\n' && k + 1 < sizeof pasta) pasta[k++] = *l++;
        pasta[k] = '\0';
        long d = dispositivo_de(dev);
        /* já montado em outro lugar (o boot põe o 1º FAT32 em /disco)? solta */
        char onde[CAMINHO_MAX];
        u32 s_, d_, srv = 0;
        if (d >= 0 && montado_em((u32)d, onde) && !str_igual(onde, pasta)) {
            for (u32 i = 0; montagem(i, 0, &s_, &d_) == 0; i++) if (d_ == (u32)d) srv = s_;
            if (srv) desmontar_de(srv, onde);
        }
        escreve("[init] montando ", COR(COR_CINZA_CLARO));
        escreve(dev, COR(COR_BRANCO));
        escreve(" em ", COR(COR_CINZA_CLARO));
        escreve(pasta, COR(COR_BRANCO));
        long r = d < 0 || pasta[0] != '/' ? ERRO_INVALIDO
               : arq_info(pasta, 0) != ARQ_TIPO_PASTA ? ERRO_NAO_ACHADO : montar_em((u32)d, pasta);
        escreve(r == 0 ? "\n" : r == ERRO_NAO_ACHADO ? ": a pasta nao existe\n"
                : r == ERRO_FORMATO ? ": nao e FAT32 nem ext2\n" : r == ERRO_OCUPADO ? ": ja montado\n" : ": falhou\n",
                r == 0 ? 0 : COR(COR_VERMELHO_CLARO));
    }
    ufree(t);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    sinal_tratar(SINAL_INT, (void (*)(int))SINAL_IGNORAR);
    montar_tudo();

    /* o root ainda sem senha? avisa (como na primeira vez de um Arch) */
    char *sombra = texto_de("/etc/sombra");
    char campo_root[HASH_MAX];
    int sem_senha = sombra && sombra_de(sombra, "root", campo_root, sizeof campo_root) && !campo_root[0];
    ufree(sombra);

    for (;;) {
        escreve("\n", 0);
        escreve(SISTEMA_NOME, COR(COR_CIANO_CLARO));
        escreve(" - entre com o seu usuario.\n", COR(COR_CINZA_CLARO));
        if (sem_senha)
            escreve("(O root ainda nao tem senha: entre como root, sem senha, e use passwd.)\n",
                    COR(COR_AMARELO));
        char *args[] = { "login", 0 };
        long r = rodar_e_esperar("/bin/login", args);
        if (r == ERRO_NAO_ACHADO) { escreve_erro("init: nao achei o /bin/login\n"); return 1; }
    }
}
