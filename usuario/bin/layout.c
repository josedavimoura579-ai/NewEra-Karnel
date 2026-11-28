/*
 * usuario/bin/layout.c - o layout do teclado.
 *
 *   layout          mostra qual está em uso
 *   layout br       o ABNT2 (o teclado brasileiro, com ç e acentos)
 *   layout us       o americano
 *
 * Vale na hora, até desligar. Para ficar valendo em todo boot, o root põe
 * br ou us no /etc/teclado.
 *
 * (No QEMU, a tecla passa por um mapa antes de chegar: o run.sh usa o
 *  -k pt-br, que combina com o ABNT2.)
 */
#include <stdio.h>
#include <string.h>
#include "ulib.h"

int main(int argc, char **argv)
{
    msg_t m;
    msg_zera(&m);
    m.tipo = TECLADO_MAPA;
    m.arg[0] = 2;                                   /* só pergunta */
    if (argc > 1) {
        if (!strcmp(argv[1], "br") || !strcmp(argv[1], "abnt2")) m.arg[0] = 1;
        else if (!strcmp(argv[1], "us")) m.arg[0] = 0;
        else { fprintf(stderr, "uso: layout [br|us]\n"); return 1; }
    }
    if (sys_chamar(PID_TECLADO, &m) < 0 || m.tipo != MSG_OK) { fprintf(stderr, "layout: o teclado não respondeu\n"); return 1; }
    printf("Teclado: %s\n", m.arg[0] ? "br (ABNT2: ç, acentos com ´ ` ~ ^ ¨)" : "us (americano)");
    return 0;
}
