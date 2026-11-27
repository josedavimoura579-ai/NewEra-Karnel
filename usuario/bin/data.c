/*
 * usuario/bin/data.c - a data e a hora do relógio do computador (o date).
 * O relógio (CMOS) guarda a hora mesmo desligado, com uma bateria.
 * No QEMU, é a hora do computador de fora (em UTC, o horário de Londres).
 */
#include <stdio.h>
#include "ulib.h"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    static const char *const meses[12] = { "janeiro", "fevereiro", "marco", "abril", "maio", "junho",
        "julho", "agosto", "setembro", "outubro", "novembro", "dezembro" };
    msg_t m;
    if (sys_relogio(&m) < 0) { fprintf(stderr, "data: nao consegui ler o relogio\n"); return 1; }
    u32 mes = (m.arg[1] >> 8) & 0xFF, dia = m.arg[1] & 0xFF;
    printf("%u de %s de %u, %02u:%02u:%02u (UTC)\n", dia, mes >= 1 && mes <= 12 ? meses[mes - 1] : "?",
           m.arg[0], (m.arg[2] >> 16) & 0xFF, (m.arg[2] >> 8) & 0xFF, m.arg[2] & 0xFF);
    return 0;
}
