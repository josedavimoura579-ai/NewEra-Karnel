/*
 * usuario/bin/kill.c - manda um sinal a um processo (ou a um grupo).
 *
 *   kill 12            SIGTERM ao processo 12 (pede para terminar)
 *   kill -9 12         SIGKILL: termina sem conversa (não dá para tratar)
 *   kill -INT 12       pelo nome (com ou sem o "SIG")
 *   kill -TERM -12     ao grupo 12 inteiro (o número negativo)
 *   kill -l            lista os sinais
 *
 * Os servidores do sistema (vídeo, teclado, shell...) não aceitam sinais:
 * o núcleo responde "sem permissão".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>

static const char *const NOMES[NSIG] = {
    0, "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "BUS", "FPE", "KILL", "USR1",
    "SEGV", "USR2", "PIPE", "ALRM", "TERM", "STKFLT", "CHLD", "CONT", "STOP", "TSTP",
    "TTIN", "TTOU", "URG", "XCPU", "XFSZ", "VTALRM", "PROF", "WINCH", "IO", "PWR", "SYS"
};

static int sinal_de(const char *t)
{
    if (isdigit((unsigned char)*t)) {
        int n = atoi(t);
        return n >= 0 && n < NSIG ? n : -1;
    }
    if (!strncmp(t, "SIG", 3)) t += 3;
    for (int s = 1; s < NSIG; s++)
        if (!strcmp(t, NOMES[s])) return s;
    return -1;
}

int main(int argc, char **argv)
{
    int sinal = SIGTERM, i = 1;
    if (argc > 1 && !strcmp(argv[1], "-l")) {
        for (int s = 1; s < NSIG; s++)
            printf("%2d SIG%-7s%s", s, NOMES[s], s % 4 == 0 || s == NSIG - 1 ? "\n" : " ");
        return 0;
    }
    if (argc > 2 && argv[1][0] == '-') {
        sinal = sinal_de(argv[1] + 1);
        if (sinal < 0) { fprintf(stderr, "kill: sinal desconhecido: %s (veja: kill -l)\n", argv[1] + 1); return 1; }
        i = 2;
    }
    if (i >= argc) {
        fprintf(stderr, "uso: kill [-SINAL] PID...    (kill -l lista os sinais)\n");
        return 1;
    }
    int erros = 0;
    for (; i < argc; i++) {
        char *fim;
        long pid = strtol(argv[i], &fim, 10);
        if (*fim || pid == 0) { fprintf(stderr, "kill: PID inválido: %s\n", argv[i]); erros++; continue; }
        if (kill((int)pid, sinal) < 0) {
            fprintf(stderr, "kill: %ld: %s\n", pid,
                    errno == EPERM ? "é um servidor do sistema: não aceita sinais"
                                   : "processo não encontrado");
            erros++;
        }
    }
    return erros ? 1 : 0;
}
