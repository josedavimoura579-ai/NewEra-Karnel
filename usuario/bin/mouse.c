/*
 * usuario/bin/mouse.c - mostra o que o mouse faz (só no modo gráfico).
 *
 *   mouse          clique, solte, gire a roda... (Ctrl+C para sair)
 *
 * Cada evento vem do servidor de vídeo (MOUSE_ESPERAR): a posição da seta
 * na tela, os botões apertados e quanto a roda girou.
 */
#include <stdio.h>
#include "ulib.h"

static const char *nome(u32 b)
{
    return b == MOUSE_ESQUERDO ? "esquerdo" : b == MOUSE_DIREITO ? "direito" : "do meio";
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    u32 antes = 0, x = 0, y = 0;
    int primeiro = 1;
    printf("Mexa o mouse, clique, gire a roda. Ctrl+C para sair.\n");
    fflush(stdout);
    for (;;) {
        msg_t m;
        msg_zera(&m);
        m.tipo = MOUSE_ESPERAR;
        if (sys_chamar(PID_VIDEO, &m) < 0 || m.tipo != MSG_OK) {
            printf("mouse: só no modo gráfico (bash run.sh grafico ou vnc)\n");
            return 1;
        }
        x = m.arg[0];
        y = m.arg[1];
        u32 b = m.arg[2];
        int roda = (int)m.arg[3];
        if (primeiro) { printf("A seta está em (%u, %u).\n", x, y); primeiro = 0; }
        for (u32 bit = 1; bit <= 4; bit <<= 1) {
            if ((b & bit) && !(antes & bit)) printf("botão %s apertado em (%u, %u)\n", nome(bit), x, y);
            if (!(b & bit) && (antes & bit)) printf("botão %s solto em (%u, %u)\n", nome(bit), x, y);
        }
        if (roda) printf("roda: %+d (%s) em (%u, %u)\n", roda, roda > 0 ? "para cima" : "para baixo", x, y);
        antes = b;
        fflush(stdout);
    }
}
