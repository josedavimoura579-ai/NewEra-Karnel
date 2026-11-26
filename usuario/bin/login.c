/*
 * usuario/bin/login.c - entra no sistema: pergunta o usuário e a senha e
 * abre um shell como ele, na pasta pessoal dele.
 */
#include "contas.h"

int main(int argc, char **argv)
{
    for (int tentativa = 0; tentativa < 3; tentativa++) {
        char nome[NOME_CONTA_MAX], senha[NOME_CONTA_MAX];
        if (argc > 1 && tentativa == 0) str_copia(nome, argv[1], sizeof nome);
        else if (!ler_linha("login: ", nome, sizeof nome, 1)) continue;
        ler_linha("Senha: ", senha, sizeof senha, 0);
        msg_t r;
        /* sempre confere a senha (mesmo se quem chamou é o root) */
        long e = contas_pedir(CONTAS_CONFERIR, nome, senha, &r);
        if (e == 0) e = contas_pedir(CONTAS_ENTRAR, nome, senha, &r);
        mem_zera(senha, sizeof senha);
        if (e < 0) {
            for (u32 t = sys_tempo(); sys_tempo() - t < 200; ) sys_ceder();
            escreve_erro("Usuario ou senha errados.\n\n");
            continue;
        }
        if (!r.texto[0] || sys_pasta(r.texto) < 0 || arq_info(r.texto, 0) != ARQ_TIPO_PASTA) sys_pasta("/");
        escreve("Bem-vindo, ", COR(COR_CINZA_CLARO));
        escreve(nome, COR(COR_VERDE_CLARO));
        escreve("!\n", COR(COR_CINZA_CLARO));
        sinal_tratar(SINAL_INT, (void (*)(int))SINAL_IGNORAR);
        char *args[] = { "sh", 0 };
        rodar_e_esperar("/bin/sh", args);
        return 0;
    }
    return 1;
}
