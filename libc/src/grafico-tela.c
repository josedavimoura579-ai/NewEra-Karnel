/*
 * nlibc - libc/src/grafico-tela.c
 * A tela cheia (o servidor de vídeo, VIDEO_TELA_*) e o mouse de grafico.h.
 * O programa desenha numa memória compartilhada do tamanho da tela; o
 * servidor de vídeo a mapeia também e copia para a tela quando pedido.
 */
#include <grafico.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include "interno.h"

/* ------------------------------- A tela --------------------------------- */

static long video(unsigned tipo, unsigned a0, unsigned a1, unsigned a2, unsigned a3, msg_t *r)
{
    msg_zera(r);
    r->tipo = tipo;
    r->arg[0] = a0; r->arg[1] = a1; r->arg[2] = a2; r->arg[3] = a3;
    if (sys_chamar(PID_VIDEO, r) < 0) return ERRO_PID;
    return r->tipo == MSG_ERRO ? -(long)r->arg[0] : 0;
}

gr_imagem_t *gr_tela_abrir(void)
{
    char nome[32];
    snprintf(nome, sizeof nome, "/tela-%ld", sys_pid());
    shm_unlink(nome);
    shm_pedido_t p;
    p.nome = (u64)nome;
    p.criar = 2;
    p.modo = 0600;
    long id = sys_shm_abrir(&p);
    if (id < 0) return NULL;
    size_t tam = (size_t)1024 * 768 * 4;
    if (sys_shm_tamanho((u32)id, tam) < 0) { sys_shm_apagar(nome); return NULL; }
    msg_t r;
    if (video(VIDEO_TELA_ABRIR, (unsigned)id, 0, 0, 0, &r) < 0) { sys_shm_apagar(nome); return NULL; }
    int l = (int)r.arg[0], a = (int)r.arg[1];
    mmap_t m;
    m.tam = (u64)l * (u64)a * 4;
    m.objeto = (u64)id;
    m.prot = PROT_LER | PROT_ESCREVER;
    m.reservado = 0;
    long v = sys_mmap(&m);
    sys_shm_apagar(nome);                /* o nome some; a memória fica enquanto mapeada */
    if (v < 0) { video(VIDEO_TELA_FECHAR, 0, 0, 0, 0, &r); return NULL; }
    gr_imagem_t *im = calloc(1, sizeof *im);
    if (!im) { video(VIDEO_TELA_FECHAR, 0, 0, 0, 0, &r); return NULL; }
    im->pontos = (gr_cor *)v;
    im->largura = l;
    im->altura = a;
    im->passo = l;
    im->_tela = 1;
    return im;
}

void gr_tela_mostrar(gr_imagem_t *tela, int x, int y, int l, int a)
{
    msg_t r;
    if (x < 0) { l += x; x = 0; }
    if (y < 0) { a += y; y = 0; }
    if (l <= 0 || a <= 0) return;
    video(VIDEO_TELA_MOSTRAR, (unsigned)x, (unsigned)y, (unsigned)l, (unsigned)a, &r);
    (void)tela;
}

void gr_tela_mostrar_tudo(gr_imagem_t *tela) { gr_tela_mostrar(tela, 0, 0, tela->largura, tela->altura); }

void gr_tela_fechar(gr_imagem_t *tela)
{
    msg_t r;
    if (!tela) return;
    video(VIDEO_TELA_FECHAR, 0, 0, 0, 0, &r);
    sys_munmap((u64)tela->pontos, (u64)tela->largura * (u64)tela->altura * 4);
    free(tela);
}

int gr_mouse(int *x, int *y, int *botoes)
{
    msg_t r;
    if (video(MOUSE_ESPERAR, 0, 0, 0, 0, &r) < 0) return 0;
    if (x) *x = (int)r.arg[0];
    if (y) *y = (int)r.arg[1];
    if (botoes) *botoes = (int)r.arg[2];
    return (int)r.arg[3];
}
