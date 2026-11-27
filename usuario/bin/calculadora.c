/*
 * usuario/bin/calculadora.c - uma calculadora, numa janela.
 *
 * Roda dentro do servidor de janelas (janelas). Um exemplo dos controles
 * de janela.h: o visor é um rótulo; as teclas, botões. Também aceita o
 * teclado: números, + - * /, vírgula (ou ponto), Enter (=), Backspace e
 * Esc (limpa). Puxe o canto de baixo: os botões se espalham.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <janela.h>

static jn_janela_t *j;
static jn_controle_t *visor, *conta, *botoes[20];
static const char *const nomes[20] = {
    "C", "<", "%", "/",
    "7", "8", "9", "*",
    "4", "5", "6", "-",
    "1", "2", "3", "+",
    "+/-", "0", ",", "=" };

static char entrada[32] = "0";      /* o número sendo digitado */
static double acumulado;
static char operacao;               /* + - * / (0 = nenhuma) */
static int novo_numero = 1;         /* o próximo dígito começa um número */

/* O número com vírgula, sem zeros sobrando */
static void formatar(double v, char *s, size_t n)
{
    snprintf(s, n, "%.10g", v);
    if (strchr(s, 'e') == NULL && strlen(s) > 14) snprintf(s, n, "%.6e", v);
    for (char *p = s; *p; p++) if (*p == '.') *p = ',';
    if (!strcmp(s, "-0")) strcpy(s, "0");
}

static double valor(void)
{
    char t[32];
    strcpy(t, entrada);
    for (char *p = t; *p; p++) if (*p == ',') *p = '.';
    return strtod(t, NULL);
}

static void mostrar(void)
{
    jn_controle_texto(j, visor, entrada);
    char c[64] = "";
    if (operacao) {
        char a[32];
        formatar(acumulado, a, sizeof a);
        snprintf(c, sizeof c, "%s %c", a, operacao == '*' ? 'x' : operacao);
    }
    jn_controle_texto(j, conta, c);
}

static void calcular(void)
{
    double b = valor();
    switch (operacao) {
    case '+': acumulado += b; break;
    case '-': acumulado -= b; break;
    case '*': acumulado *= b; break;
    case '/': acumulado = b != 0 ? acumulado / b : 0; break;
    default: acumulado = b;
    }
    if (operacao == '/' && b == 0) strcpy(entrada, "não divide por 0");
    else formatar(acumulado, entrada, sizeof entrada);
}

static void tecla(const char *t)
{
    char c = t[0];
    if (c >= '0' && c <= '9') {
        if (novo_numero || !strcmp(entrada, "0")) { entrada[0] = 0; novo_numero = 0; }
        if (strlen(entrada) < 15) strncat(entrada, t, 1);
    } else if (c == ',' || c == '.') {
        if (novo_numero) { strcpy(entrada, "0"); novo_numero = 0; }
        if (!strchr(entrada, ',')) strcat(entrada, ",");
    } else if (c == 'C') {
        strcpy(entrada, "0"); acumulado = 0; operacao = 0; novo_numero = 1;
    } else if (c == '<') {
        size_t n = strlen(entrada);
        if (!novo_numero && n > 1) entrada[n - 1] = 0; else { strcpy(entrada, "0"); novo_numero = 1; }
    } else if (!strcmp(t, "+/-")) {
        if (entrada[0] == '-') memmove(entrada, entrada + 1, strlen(entrada));
        else if (strcmp(entrada, "0") && strlen(entrada) < 30) { memmove(entrada + 1, entrada, strlen(entrada) + 1); entrada[0] = '-'; }
    } else if (c == '%') {
        double v = operacao ? acumulado * valor() / 100 : valor() / 100;
        formatar(v, entrada, sizeof entrada);
        novo_numero = 1;
    } else if (c == '=') {
        if (operacao) { calcular(); operacao = 0; }
        novo_numero = 1;
    } else if (strchr("+-*/", c)) {
        if (operacao && !novo_numero) calcular();
        else if (!operacao) acumulado = valor();
        operacao = c;
        novo_numero = 1;
    }
    mostrar();
}

/* Espalha o visor e os botões pelo tamanho da janela */
static void arrumar(void)
{
    int L = j->largura, A = j->altura, m = 10, alto_visor = A / 5;
    jn_controle_mover(conta, m, m, L - 2 * m, alto_visor / 3);
    jn_controle_mover(visor, m, m + alto_visor / 3, L - 2 * m, alto_visor - alto_visor / 3);
    visor->tamanho = alto_visor / 2 > 14 ? alto_visor / 2 : 14;
    int y0 = m + alto_visor + m, bl = (L - m * 5) / 4, ba = (A - y0 - m * 5) / 5;
    for (int i = 0; i < 20; i++) {
        jn_controle_mover(botoes[i], m + (i % 4) * (bl + m), y0 + (i / 4) * (ba + m), bl, ba);
        botoes[i]->tamanho = ba / 2 > 13 ? (ba / 2 < 24 ? ba / 2 : 24) : 13;
    }
    gr_preencher(j->im, 0, 0, L, A, j->fundo);
    jn_desenhar_controles(j);
}

int main(void)
{
    j = jn_abrir_com("Calculadora", 260, 340, JN_REDIMENSIONAVEL);
    if (!j) { fprintf(stderr, "calculadora: o servidor de janelas não está rodando (rode: janelas)\n"); return 1; }
    conta = jn_rotulo(j, 0, 0, 1, 1, "");
    conta->alinhar = JN_DIREITA;
    conta->cor = GR_RGB(110, 120, 140);
    conta->tamanho = 13;
    visor = jn_rotulo(j, 0, 0, 1, 1, entrada);
    visor->alinhar = JN_DIREITA;
    visor->negrito = 1;
    for (int i = 0; i < 20; i++) {
        botoes[i] = jn_botao(j, 0, 0, 1, 1, nomes[i]);
        char c = nomes[i][0];
        if (strchr("/*-+%", c) && nomes[i][1] == 0) botoes[i]->cor = GR_RGB(36, 87, 197);
        if (c == 'C' || c == '<') botoes[i]->cor = GR_RGB(200, 60, 50);
    }
    botoes[19]->fundo = GR_RGB(255, 130, 40);              /* o "=" em laranja */
    botoes[19]->cor = GR_BRANCO;
    arrumar();

    jn_evento_t e;
    while (jn_esperar(j, &e) && e.tipo != JN_FECHAR) {
        if (e.tipo == JN_REDIMENSIONAR) { arrumar(); continue; }
        jn_controle_t *c = jn_tratar(j, &e);
        if (c) { tecla(c->texto); continue; }
        if (e.tipo == JN_TECLA) {
            char t[2] = { (char)e.tecla, 0 };
            if (e.tecla == '\n') t[0] = '=';
            else if (e.tecla == '\b') t[0] = '<';
            else if (e.tecla == 27 || e.tecla == 'c') t[0] = 'C';
            if (t[0]) tecla(t);
        }
    }
    jn_fechar(j);
    return 0;
}
