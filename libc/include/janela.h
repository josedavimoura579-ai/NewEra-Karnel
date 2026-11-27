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
 *
 * E os controles prontos (rótulos, botões, caixas de texto):
 *
 *   jn_controle_t *nome = jn_caixa(j, 20, 20, 200, 28, "");
 *   jn_controle_t *ok = jn_botao(j, 230, 20, 60, 28, "OK");
 *   jn_desenhar_controles(j);
 *   while (jn_esperar(j, &e) && e.tipo != JN_FECHAR)
 *       if (jn_tratar(j, &e) == ok) ...          (clicaram no OK)
 */
#ifndef _JANELA_H
#define _JANELA_H
#include <grafico.h>

#define JN_SOCKET "/tmp/janelas"

/* O que o programa pede ao servidor */
#define JN_CRIAR    1       /* l, a, titulo, shm (o nome da memória) */
#define JN_MOSTRAR  2       /* x, y, l, a: este pedaço mudou */
#define JN_TITULO   3       /* titulo */
#define JN_TROCAR   4       /* l, a, shm: a memória nova, depois de redimensionar (a biblioteca faz) */
/* O que o servidor manda (os eventos) */
#define JN_CRIADA  10       /* id, x, y: a janela apareceu */
#define JN_TECLA   11       /* tecla: o caractere (ASCII; '\n', '\b', 27 = Esc) */
#define JN_MOUSE   12       /* evento, x, y (dentro da janela), botoes; JN_RODA: a = o giro */
#define JN_FECHAR  13       /* clicaram no X da janela */
#define JN_FOCO    14       /* x = 1: a janela foi para a frente; 0: saiu da frente */
#define JN_REDIMENSIONAR 15 /* l, a: o tamanho novo (j->im já é a imagem nova: redesenhe tudo) */
#define JN_TROCADA 16       /* (a biblioteca usa: o servidor já trocou para a memória nova) */

/* As opções de jn_abrir_com */
#define JN_REDIMENSIONAVEL 1    /* um canto para arrastar e mudar o tamanho */

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

/* Os controles: um retângulo da janela que se desenha e responde sozinho */
#define JN_ROTULO  1        /* um texto */
#define JN_BOTAO   2        /* clicável */
#define JN_CAIXA   3        /* uma linha de texto para digitar */
#define JN_ESQUERDA 0
#define JN_CENTRO   1
#define JN_DIREITA  2

typedef struct {
    int    tipo;
    int    x, y, l, a;
    char   texto[128];
    int    tamanho;         /* das letras (padrão: 15) */
    int    alinhar;         /* JN_ESQUERDA, JN_CENTRO, JN_DIREITA */
    gr_cor cor, fundo;      /* das letras e do fundo (0 = a cor padrão) */
    int    negrito, visivel;
    int    apertado, foco;  /* (estado: o botão apertado, a caixa com o cursor) */
} jn_controle_t;

#define JN_MAX_CONTROLES 48

typedef struct {
    gr_imagem_t *im;        /* onde desenhar */
    int id, fd;
    int largura, altura;
    gr_cor fundo;           /* a cor de fundo (dos controles também) */
    /* (por dentro) */
    jn_controle_t controles[JN_MAX_CONTROLES];
    int n_controles;
    jn_controle_t *capturado, *foco;
    int  troca_pendente;    /* redimensionou: falta mandar a memória nova */
    char shm_novo[32];
    void *velhos[4];        /* memórias que o servidor ainda pode estar usando */
    unsigned long velhos_tam[4];
    int  n_velhos;
} jn_janela_t;

jn_janela_t *jn_abrir(const char *titulo, int largura, int altura);   /* NULL: sem o servidor */
jn_janela_t *jn_abrir_com(const char *titulo, int largura, int altura, int opcoes);
void jn_mostrar(jn_janela_t *j, int x, int y, int l, int a);
void jn_mostrar_tudo(jn_janela_t *j);
void jn_titulo(jn_janela_t *j, const char *titulo);
int  jn_esperar(jn_janela_t *j, jn_evento_t *e);      /* espera um evento; 0 se o servidor acabou */
int  jn_tem_evento(jn_janela_t *j);                   /* 1 se há um evento esperando (não espera) */
void jn_fechar(jn_janela_t *j);

/* Controles */
jn_controle_t *jn_rotulo(jn_janela_t *j, int x, int y, int l, int a, const char *texto);
jn_controle_t *jn_botao(jn_janela_t *j, int x, int y, int l, int a, const char *texto);
jn_controle_t *jn_caixa(jn_janela_t *j, int x, int y, int l, int a, const char *texto);
void jn_controle_texto(jn_janela_t *j, jn_controle_t *c, const char *texto);   /* troca e redesenha */
void jn_controle_mover(jn_controle_t *c, int x, int y, int l, int a);
void jn_desenhar_controle(jn_janela_t *j, jn_controle_t *c);                  /* e mostra */
void jn_desenhar_controles(jn_janela_t *j);                                   /* todos (e mostra a janela) */
/* Trata um evento: devolve o botão clicado, ou a caixa onde deram Enter;
 * NULL se não foi com os controles (aí o programa decide). */
jn_controle_t *jn_tratar(jn_janela_t *j, const jn_evento_t *e);
gr_fonte_t *jn_fonte(int negrito);                    /* a DejaVu (normal ou negrito), aberta uma vez */
#endif
