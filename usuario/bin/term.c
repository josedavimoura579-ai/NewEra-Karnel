/*
 * usuario/bin/term.c - um terminal numa janela, com um shell dentro.
 *
 *   term                  80 colunas x 24 linhas
 *   term -c 60x20         outro tamanho (ou puxe o canto da janela)
 *   term PROGRAMA...      roda o programa em vez do shell
 *
 * Roda dentro do servidor de janelas (janelas). Cria um TERMINAL VIRTUAL
 * no servidor de terminal (TERM_PTY_CRIAR) e põe o shell nele: o que o
 * shell escreve chega aqui (TERM_PTY_LER) e vira letras na janela; o que
 * se digita na janela vai para lá (TERM_PTY_TECLAS), como se fosse o
 * teclado. Cada terminal tem o seu Ctrl+C: ele só vale para o da frente.
 *
 * Três threads: uma lê a saída do shell e desenha; uma espera o shell
 * terminar (aí a janela fecha); a principal leva as teclas e o mouse.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/wait.h>
#include <janela.h>
#include "ulib.h"

#define CL      8                   /* uma letra: 8 x 16 pontos */
#define CA      16
#define MARGEM  4

static const gr_cor PALETA[16] = {      /* as mesmas cores do console */
    0xFF0B1220, 0xFF2457C5, 0xFF2E9E4F, 0xFF2EC4D6, 0xFFC23B3B, 0xFF9B4DCA, 0xFFC77D1E, 0xFFC8CDD6,
    0xFF5C6675, 0xFF5B8DEF, 0xFF5FD07F, 0xFF7FE6F2, 0xFFFF6B6B, 0xFFD38CFF, 0xFFFFC94A, 0xFFFFFFFF };

typedef struct { u32 letra; u8 cor; } celula_t;

static pthread_mutex_t g = PTHREAD_MUTEX_INITIALIZER;
static jn_janela_t *j;
static u32 canal;                       /* o terminal virtual */
static long shell;                      /* o pid do shell (e do grupo dele) */
static celula_t *grade;
static int colunas, linhas, cx, cy, ativa = 1;
static u8 cor;
static int sujo0 = 1 << 30, sujo1 = -1; /* as linhas para redesenhar */

static void sujar(int l0, int l1)
{
    if (l0 < sujo0) sujo0 = l0;
    if (l1 > sujo1) sujo1 = l1;
}

static gr_cor fundo_de(u8 c) { u32 f = (c >> 4) & 15; return PALETA[f == 1 ? 0 : f]; }

static void desenhar_celula(int c, int l)
{
    celula_t *e = &grade[l * colunas + c];
    int x = MARGEM + c * CL, y = MARGEM + l * CA;
    gr_preencher(j->im, x, y, CL, CA, fundo_de(e->cor));
    if (e->letra > ' ') {
        char s[5];
        u32 u = e->letra;
        if (u < 0x80) { s[0] = (char)u; s[1] = 0; }
        else if (u < 0x800) { s[0] = (char)(0xC0 | u >> 6); s[1] = (char)(0x80 | (u & 63)); s[2] = 0; }
        else { s[0] = (char)(0xE0 | u >> 12); s[1] = (char)(0x80 | ((u >> 6) & 63)); s[2] = (char)(0x80 | (u & 63)); s[3] = 0; }
        gr_texto(j->im, x, y, s, PALETA[e->cor & 15], 1);
    }
}

/* Redesenha as linhas sujas, o cursor, e manda o servidor mostrar */
static void mostrar(void)
{
    if (sujo1 < 0) return;
    if (sujo0 < 0) sujo0 = 0;
    if (sujo1 >= linhas) sujo1 = linhas - 1;
    for (int l = sujo0; l <= sujo1; l++)
        for (int c = 0; c < colunas; c++) desenhar_celula(c, l);
    if (cy >= sujo0 && cy <= sujo1 && cx < colunas) {
        int x = MARGEM + cx * CL, y = MARGEM + cy * CA;
        if (ativa) gr_preencher(j->im, x, y + CA - 3, CL, 3, PALETA[11]);
        else gr_retangulo(j->im, x, y, CL, CA, PALETA[8]);
    }
    jn_mostrar(j, 0, MARGEM + sujo0 * CA, j->largura, (sujo1 - sujo0 + 1) * CA);
    sujo0 = 1 << 30;
    sujo1 = -1;
}

static void limpar_linha(int l)
{
    for (int c = 0; c < colunas; c++) { grade[l * colunas + c].letra = ' '; grade[l * colunas + c].cor = cor; }
}

/* Sobe tudo uma linha: a imagem também (só a última precisa ser desenhada) */
static void rolar(void)
{
    mostrar();
    memmove(grade, grade + colunas, sizeof(celula_t) * (size_t)colunas * (size_t)(linhas - 1));
    limpar_linha(linhas - 1);
    gr_imagem_t *im = j->im;
    memmove(&im->pontos[(size_t)MARGEM * im->passo], &im->pontos[(size_t)(MARGEM + CA) * im->passo],
            sizeof(gr_cor) * (size_t)im->passo * (size_t)((linhas - 1) * CA));
    sujar(linhas - 2, linhas - 1);
    jn_mostrar(j, 0, MARGEM, j->largura, (linhas - 1) * CA);
}

static void nova_linha(void)
{
    sujar(cy, cy);
    cx = 0;
    if (++cy == linhas) { cy = linhas - 1; rolar(); }
    sujar(cy, cy);
}

static void limpar_tela(void)
{
    for (int l = 0; l < linhas; l++) limpar_linha(l);
    cx = cy = 0;
    sujar(0, linhas - 1);
}

/* O cursor anda n letras (com a quebra de linha), sem apagar */
static void andar(int n)
{
    sujar(cy, cy);
    while (n < 0) { if (cx > 0) cx--; else if (cy > 0) { cy--; cx = colunas - 1; } n++; }
    while (n > 0) { if (++cx >= colunas) { cx = 0; if (cy + 1 < linhas) cy++; } n--; }
    sujar(cy, cy);
}

/* O que o shell escreveu: letras (UTF-8), \n, \b, \t, as cores (27, 1, cor) e
 * as sequências do cursor: Esc [ n D (esquerda), Esc [ n C (direita), Esc [ K */
static void processar(const u8 *b, long n)
{
    static int estado, falta, num;
    static u32 u;
    for (long i = 0; i < n; i++) {
        u8 c = b[i];
        if (estado == 1) {
            estado = c == TERM_COR ? 2 : c == '[' ? 3 : 0;
            num = 0;
            if (c == TERM_LIMPA) limpar_tela();
            continue;
        }
        if (estado == 2) { cor = c; estado = 0; continue; }
        if (estado == 3) {
            if (c >= '0' && c <= '9') { num = num * 10 + (c - '0'); continue; }
            estado = 0;
            if (c == 'D') andar(-(num ? num : 1));
            else if (c == 'C') andar(num ? num : 1);
            else if (c == 'K') {
                for (int k = cx; k < colunas; k++) { grade[cy * colunas + k].letra = ' '; grade[cy * colunas + k].cor = cor; }
                sujar(cy, cy);
            }
            continue;
        }
        if (falta) {                                   /* o meio de uma letra UTF-8 */
            if ((c & 0xC0) == 0x80) {
                u = u << 6 | (c & 63);
                if (--falta) continue;
                goto letra;
            }
            falta = 0;                                 /* não era: o byte começa de novo */
        }
        if (c >= 0xC0) {
            falta = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
            u = c & (c >= 0xF0 ? 7 : c >= 0xE0 ? 15 : 31);
            continue;
        }
        u = c;
        if (c == 27) { estado = 1; continue; }
    letra:
        if (u == '\n') { nova_linha(); continue; }
        if (u == '\r') { sujar(cy, cy); cx = 0; continue; }
        if (u == '\b') {
            sujar(cy, cy);
            if (cx > 0) { cx--; grade[cy * colunas + cx].letra = ' '; }
            continue;
        }
        if (u == '\t') { int k = (cx + 8) & ~7; while (cx < k && cx < colunas) grade[cy * colunas + cx++].letra = ' '; sujar(cy, cy); continue; }
        if (u < ' ') continue;
        if (cx >= colunas) nova_linha();
        grade[cy * colunas + cx].letra = u;
        grade[cy * colunas + cx].cor = cor;
        cx++;
        sujar(cy, cy);
    }
}

/* O tamanho mudou: uma grade nova, com o fim da antiga (onde está o cursor) */
static void nova_grade(int novas_colunas, int novas_linhas)
{
    if (novas_colunas < 10) novas_colunas = 10;
    if (novas_linhas < 3) novas_linhas = 3;
    celula_t *n = calloc((size_t)novas_colunas * (size_t)novas_linhas, sizeof *n);
    for (int i = 0; i < novas_colunas * novas_linhas; i++) { n[i].letra = ' '; n[i].cor = cor; }
    int desloca = grade && cy >= novas_linhas ? cy - novas_linhas + 1 : 0;
    for (int l = 0; grade && l < novas_linhas && l + desloca < linhas; l++)
        for (int c = 0; c < novas_colunas && c < colunas; c++) n[l * novas_colunas + c] = grade[(l + desloca) * colunas + c];
    free(grade);
    grade = n;
    cy -= desloca;
    colunas = novas_colunas;
    linhas = novas_linhas;
    if (cx > colunas) cx = colunas;
    gr_preencher(j->im, 0, 0, j->largura, j->altura, PALETA[0]);
    sujar(0, linhas - 1);
}

static void *ler_saida(void *x)
{
    (void)x;
    static u8 buf[4096];
    for (;;) {
        msg_t m;
        msg_zera(&m);
        m.tipo = TERM_PTY_LER;
        m.arg[0] = canal;
        m.arg[1] = sizeof buf;
        m.arg[2] = (u32)(u64)buf;
        if (sys_chamar(PID_TERMINAL, &m) < 0 || m.tipo != MSG_OK || m.arg[0] == 0) break;
        pthread_mutex_lock(&g);
        processar(buf, m.arg[0]);
        mostrar();
        pthread_mutex_unlock(&g);
    }
    return NULL;
}

static void terminar(void)
{
    msg_t m;
    msg_zera(&m);
    m.tipo = TERM_PTY_FECHAR;
    m.arg[0] = canal;
    sys_chamar(PID_TERMINAL, &m);
    if (shell > 0) kill(-(pid_t)shell, SIGKILL);       /* o shell e o que ele estiver rodando */
    jn_fechar(j);
    exit(0);
}

static void *esperar_shell(void *x)
{
    (void)x;
    int st;
    waitpid((pid_t)shell, &st, 0);
    pthread_mutex_lock(&g);
    shell = 0;
    terminar();
    return NULL;
}

static void teclas(const char *t, u32 n)
{
    msg_t m;
    msg_zera(&m);
    m.tipo = TERM_PTY_TECLAS;
    m.arg[0] = canal;
    m.arg[2] = n;
    memcpy(m.texto, t, n);
    sys_chamar(PID_TERMINAL, &m);
}

int main(int argc, char **argv)
{
    int c = 80, l = 24, a = 1;
    if (argc > 2 && !strcmp(argv[1], "-c")) { sscanf(argv[2], "%dx%d", &c, &l); a = 3; }
    if (c < 10) c = 10;
    if (l < 3) l = 3;

    msg_t m;
    msg_zera(&m);
    m.tipo = TERM_PTY_CRIAR;
    if (sys_chamar(PID_TERMINAL, &m) < 0 || m.tipo != MSG_OK) { fprintf(stderr, "term: não há terminal virtual livre\n"); return 1; }
    canal = m.arg[0];
    char titulo[48];
    snprintf(titulo, sizeof titulo, "Terminal %u", canal);
    j = jn_abrir_com(titulo, 2 * MARGEM + c * CL, 2 * MARGEM + l * CA, JN_REDIMENSIONAVEL);
    if (!j) {
        fprintf(stderr, "term: o servidor de janelas não está rodando (rode: janelas)\n");
        msg_zera(&m); m.tipo = TERM_PTY_FECHAR; m.arg[0] = canal; sys_chamar(PID_TERMINAL, &m);
        return 1;
    }
    cor = COR(COR_BRANCO);
    nova_grade(c, l);
    mostrar();

    /* o shell (ou o programa pedido), com a entrada e a saída no terminal novo, num grupo só dele */
    char *sh[] = { "sh", NULL };
    char **prog = a < argc ? argv + a : sh;
    char caminho[CAMINHO_MAX];
    if (strchr(prog[0], '/')) snprintf(caminho, sizeof caminho, "%s", prog[0]);
    else snprintf(caminho, sizeof caminho, "/bin/%s", prog[0]);
    shell = executar_grupo(caminho, prog, PID_TERMINAL, canal, PID_TERMINAL, canal, GRUPO_NOVO);
    if (shell < 0) {
        char e[96];
        snprintf(e, sizeof e, "term: não consegui rodar %s\n", caminho);
        processar((const u8 *)e, (long)strlen(e));
        mostrar();
    }

    pthread_t t;
    pthread_create(&t, NULL, ler_saida, NULL);
    if (shell > 0) pthread_create(&t, NULL, esperar_shell, NULL);

    for (;;) {
        if (!jn_tem_evento(j)) { usleep(20000); continue; }
        jn_evento_t e;
        pthread_mutex_lock(&g);
        if (!jn_esperar(j, &e) || e.tipo == JN_FECHAR) terminar();
        if (e.tipo == JN_TECLA) {                     /* a tecla volta a ser bytes (UTF-8, Esc [ A...) */
            char k[8];
            int nk = jn_tecla_bytes(e.tecla, e.botoes, k);
            pthread_mutex_unlock(&g);
            if (nk) teclas(k, (u32)nk);
            continue;
        }
        if (e.tipo == JN_FOCO) { ativa = e.x; sujar(cy, cy); }
        if (e.tipo == JN_REDIMENSIONAR) nova_grade((e.l - 2 * MARGEM) / CL, (e.a - 2 * MARGEM) / CA);
        mostrar();
        pthread_mutex_unlock(&g);
    }
}
