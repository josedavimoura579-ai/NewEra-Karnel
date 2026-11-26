/*
 * usuario/bin/su.c - vira outro usuário (o root, se não disser qual).
 *
 *   su                 vira o root (pede a senha do root)
 *   su joao            vira o joao (pede a senha do joao)
 *   su - joao          e vai para a pasta pessoal dele
 *   sair               volta a ser quem era
 *
 * Quem confere a senha e muda a identidade é o servidor de contas; o su só
 * pergunta e depois roda um shell novo (/bin/sh) já como o outro usuário.
 */
#include "contas.h"

int main(int argc, char **argv)
{
    int i = 1, para_casa = 0;
    if (i < argc && (str_igual(argv[i], "-") || str_igual(argv[i], "-l"))) { para_casa = 1; i++; }
    const char *alvo = i < argc ? argv[i] : "root";

    identidade_t eu;
    sys_identidade(0, &eu);
    char senha[NOME_CONTA_MAX];
    senha[0] = '\0';
    if (eu.uid != UID_ROOT) ler_linha("Senha: ", senha, sizeof senha, 0);

    msg_t r;
    long e = contas_pedir(CONTAS_ENTRAR, alvo, senha, &r);
    mem_zera(senha, sizeof senha);
    if (e == ERRO_NAO_ACHADO) { escreve_erro("su: esse usuario nao existe\n"); return 1; }
    if (e < 0) {
        for (u32 t = sys_tempo(); sys_tempo() - t < 200; ) sys_ceder();   /* 2 s: chutar é lento */
        escreve_erro("su: senha errada\n");
        return 1;
    }
    if (para_casa && r.texto[0]) sys_pasta(r.texto);

    sinal_tratar(SINAL_INT, (void (*)(int))SINAL_IGNORAR);    /* o Ctrl+C é do shell novo */
    char *args[] = { "sh", 0 };
    long c = rodar_e_esperar("/bin/sh", args);
    if (c < 0) { escreve_erro("su: nao consegui rodar o /bin/sh\n"); return 1; }
    return 0;
}
