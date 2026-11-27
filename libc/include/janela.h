/*
 * nlibc - janela.h: abrir uma janela no servidor de janelas (o programa
 * "janelas", no modo gráfico).
 *
 * A janela é uma imagem (grafico.h) numa memória compartilhada: o programa
 * desenha nela quando quiser e avisa o que mudou (jn_mostrar); o servidor
 * junta as janelas na tela. A conversa é por um socket local
 * (JN_SOCKET), em mensagens jn_msg_t de tamanho fixo.
 *
 *   jn_janela_t *j = jn_abrir("Olá", 300, 200);
 *   gr_preencher(j->im, 0, 0, 300, 200, GR_BRANCO);
 *   jn_mostrar_tudo(j);
 *   jn_evento_t e;
 *   while (jn_esperar(j, &e) && e.tipo != JN_FECHAR) { ... }
 *   jn_fechar(j);
 */
#ifndef _JANELA_H
#define _JANELA_H
#include <grafico.h>

#define JN_SOCKET "/tmp/janelas"

/* O que o programa pede ao servidor */
#define JN_CRIAR    1       /* l, a, titulo, shm (o nome da memória) */
#define JN_MOSTRAR  2       /* x, y, l, a: este pedaço mudou */
#define JN_TITULO   3       /* titulo */
/* O que o servidor manda (os eventos) */
#define JN_CRIADA  10       /* id, x, y: a janela apareceu */
#define JN_TECLA   11       /* tecla: o caractere (ASCII; '\n', '\b', 27 = Esc) */
#define JN_MOUSE   12       /* evento, x, y (dentro da janela), botoes; JN_RODA: a = o giro */
#define JN_FECHAR  13       /* clicaram no X da janela */
#define JN_FOCO    14       /* x = 1: a janela foi para a frente; 0: saiu da frente */

#define JN_APERTOU 1
#define JN_SOLTOU  2
#define JN_MOVEU   3
#define JN_RODA    4

typedef struct {
    int  tipo, id;
    int  x, y, l, a;
    int  botoes, tecla, evento;
    int  _reservado[3];
    char titulo[48];
    char shm[32];
} jn_msg_t;
typedef jn_msg_t jn_evento_t;

typedef struct {
    gr_imagem_t *im;        /* onde desenhar */
    int id, fd;
    int largura, altura;
} jn_janela_t;

jn_janela_t *jn_abrir(const char *titulo, int largura, int altura);   /* NULL: sem o servidor */
void jn_mostrar(jn_janela_t *j, int x, int y, int l, int a);
void jn_mostrar_tudo(jn_janela_t *j);
void jn_titulo(jn_janela_t *j, const char *titulo);
int  jn_esperar(jn_janela_t *j, jn_evento_t *e);      /* espera um evento; 0 se o servidor acabou */
int  jn_tem_evento(jn_janela_t *j);                   /* 1 se há um evento esperando (não espera) */
void jn_fechar(jn_janela_t *j);
#endif
