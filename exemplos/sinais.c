/*
 * exemplos/sinais.c - um programa que trata o Ctrl+C (SIGINT).
 *
 *   cproc -o /tmp/sinais /usr/share/nefs/exemplos/sinais.c
 *   /tmp/sinais
 *
 * Ele conta para sempre. A cada Ctrl+C o tratador roda, avisa e o programa
 * CONTINUA de onde parou; no terceiro, ele termina sozinho. (Para encerrar
 * sem conversa, em outro programa: kill -9 PID; o SIGKILL não tem tratador.)
 */
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static int ctrl_c;              /* o tratador só marca; o programa decide */

static void tratador(int sinal)
{
    (void)sinal;
    ctrl_c++;
}

int main(void)
{
    struct sigaction sa = {0};
    sa.sa_handler = tratador;
    sigaction(SIGINT, &sa, NULL);

    printf("Sou o processo %d. Aperte Ctrl+C (3 vezes para sair).\n", getpid());
    unsigned long contador = 0;
    int vistos = 0;
    for (;;) {
        contador++;
        if (ctrl_c != vistos) {
            vistos = ctrl_c;
            printf("Ctrl+C numero %d! O contador estava em %lu e continua.\n", vistos, contador);
            if (vistos == 3) {
                printf("Tchau.\n");
                return 0;
            }
        }
        if (contador % 50000000 == 0) printf("contando... %lu\n", contador);
    }
}
