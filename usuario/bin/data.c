/*
 * usuario/bin/data.c - a data e a hora do relógio do computador (o date).
 * O relógio (CMOS) guarda a hora mesmo desligado, com uma bateria. No
 * QEMU, é a hora do computador de fora, em UTC (o horário de Londres); o
 * /etc/fuso diz quantas horas somar (-3: Brasília).
 *
 *   data          a hora local (com o fuso)
 *   data -u       a hora em UTC
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv)
{
    static const char *const meses[12] = { "janeiro", "fevereiro", "março", "abril", "maio", "junho",
        "julho", "agosto", "setembro", "outubro", "novembro", "dezembro" };
    static const char *const dias[7] = { "domingo", "segunda", "terça", "quarta", "quinta", "sexta", "sábado" };
    int utc = argc > 1 && !strcmp(argv[1], "-u");
    time_t agora = time(NULL);
    struct tm *t = utc ? gmtime(&agora) : localtime(&agora);
    printf("%s, %d de %s de %d, %02d:%02d:%02d%s\n", dias[t->tm_wday], t->tm_mday, meses[t->tm_mon],
           t->tm_year + 1900, t->tm_hour, t->tm_min, t->tm_sec, utc ? " (UTC)" : "");
    return 0;
}
