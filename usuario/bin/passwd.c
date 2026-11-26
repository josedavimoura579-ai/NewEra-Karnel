/*
 * usuario/bin/passwd.c - troca a senha.
 *
 *   passwd             a sua (pede a atual antes)
 *   passwd joao        a do joao (só o root)
 *
 * A senha vai para o servidor de contas, que guarda só o SHA-256 dela
 * (com sal) no /etc/sombra. Até 31 letras.
 */
#include "contas.h"

int main(int argc, char **argv)
{
    identidade_t eu;
    sys_identidade(0, &eu);
    char *passwd = texto_de("/etc/passwd");
    conta_t c;
    if (!passwd || !conta_procura(passwd, 0, eu.uid, &c)) { escreve_erro("passwd: nao sei quem voce e\n"); return 1; }
    ufree(passwd);
    const char *alvo = argc > 1 ? argv[1] : c.nome;
    if (eu.uid != UID_ROOT && !str_igual(alvo, c.nome)) {
        escreve_erro("passwd: so o root troca a senha de outro usuario\n");
        return 1;
    }
    msg_t r;
    char senha[NOME_CONTA_MAX], de_novo[NOME_CONTA_MAX];
    if (eu.uid != UID_ROOT) {
        ler_linha("Senha atual: ", senha, sizeof senha, 0);
        long e = contas_pedir(CONTAS_CONFERIR, alvo, senha, &r);
        if (e < 0) { escreve_erro("passwd: senha errada\n"); return 1; }
    }
    ler_linha("Nova senha: ", senha, sizeof senha, 0);
    ler_linha("Repita a nova senha: ", de_novo, sizeof de_novo, 0);
    if (!str_igual(senha, de_novo)) { escreve_erro("passwd: as duas nao sao iguais\n"); return 1; }
    long e = contas_pedir(CONTAS_SENHA, alvo, senha, &r);
    mem_zera(senha, sizeof senha);
    mem_zera(de_novo, sizeof de_novo);
    if (e < 0) { escreve_erro("passwd: nao consegui trocar a senha\n"); return 1; }
    escreve("Senha de ", COR(COR_CINZA_CLARO));
    escreve(alvo, COR(COR_BRANCO));
    escreve(" trocada.\n", COR(COR_CINZA_CLARO));
    return 0;
}
