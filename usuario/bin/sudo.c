/*
 * usuario/bin/sudo.c - roda UM comando como root.
 *
 *   sudo particionar sdb
 *   sudo adduser joao
 *
 * Pede a SUA senha (não a do root) e só funciona para quem está no grupo
 * sudo (ou wheel): o root põe alguém lá com "adduser joao sudo".
 */
#include "contas.h"

int main(int argc, char **argv)
{
    if (argc < 2) { escreve_erro("uso: sudo COMANDO [ARGUMENTOS]\n"); return 1; }
    identidade_t eu;
    sys_identidade(0, &eu);
    if (eu.uid != UID_ROOT) {
        char *passwd = texto_de("/etc/passwd");
        conta_t c;
        char pergunta[80];
        str_copia(pergunta, "[sudo] senha de ", sizeof pergunta);
        if (passwd && conta_procura(passwd, 0, eu.uid, &c)) str_copia(pergunta + 16, c.nome, sizeof pergunta - 16);
        u32 k = str_tam(pergunta);
        str_copia(pergunta + k, ": ", sizeof pergunta - k);
        ufree(passwd);

        char senha[NOME_CONTA_MAX];
        ler_linha(pergunta, senha, sizeof senha, 0);
        msg_t r;
        long e = contas_pedir(CONTAS_SUDO, senha, "", &r);
        mem_zera(senha, sizeof senha);
        if (e == ERRO_SEM_PERMISSAO) {
            escreve_erro("sudo: voce nao esta no grupo sudo (o root pode por: adduser SEU_NOME sudo)\n");
            return 1;
        }
        if (e < 0) {
            for (u32 t = sys_tempo(); sys_tempo() - t < 200; ) sys_ceder();
            escreve_erro("sudo: senha errada\n");
            return 1;
        }
    }
    long c = rodar_e_esperar(argv[1], argv + 1);
    if (c == ERRO_NAO_ACHADO) { escreve_erro("sudo: comando nao encontrado\n"); return 1; }
    if (c < 0) { escreve_erro("sudo: nao consegui rodar o comando\n"); return 1; }
    if (SAIDA_FOI_SINAL(c)) {                       /* o comando morreu de um sinal: eu também */
        sinal_tratar((u32)c & 0x7F, (void (*)(int))SINAL_PADRAO);
        sys_sinal(sys_pid(), (u32)c & 0x7F);
    }
    return (int)(c & 0xFF);
}
