/*
 * usuario/bin/desligar.c - grava tudo nos discos e desliga (ou reinicia)
 * o computador. Só o root.
 *
 *   desligar        desliga
 *   desligar -r     reinicia
 *
 * Desligar sem isto é como tirar um pendrive sem "ejetar": o que estava
 * no cache dos servidores de disco pode não ter sido gravado.
 */
#include "ulib.h"

int main(int argc, char **argv)
{
    identidade_t id;
    sys_identidade(0, &id);
    if (id.uid != UID_ROOT) { escreve_erro("desligar: precisa ser o root (use: sudo desligar)\n"); return 1; }
    int reiniciar = argc > 1 && str_igual(argv[1], "-r");
    escreve("Gravando os discos...\n", COR(COR_CINZA_CLARO));
    discos_sincronizar();
    escreve(reiniciar ? "Reiniciando.\n" : "Desligando.\n", COR(COR_AMARELO));
    msg_t m;
    msg_zera(&m);
    m.tipo = reiniciar ? MSG_REINICIAR : MSG_DESLIGAR;
    sys_enviar(PID_TECLADO, &m);
    for (u32 t = sys_tempo(); sys_tempo() - t < 200; ) sys_ceder();
    escreve("Pronto: ja pode desligar o computador.\n", COR(COR_CINZA_CLARO));
    return 0;
}
