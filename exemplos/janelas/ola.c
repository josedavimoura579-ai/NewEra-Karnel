/*
 * ola.c - o primeiro programa de janela: pergunta o nome e diz olá.
 *
 * Dentro do NovaEra, no modo gráfico:
 *   cproc -o ola /usr/share/nefs/exemplos/janelas/ola.c
 *   janelas ./ola
 */
#include <stdio.h>
#include <janela.h>

int main(void)
{
    jn_janela_t *j = jn_abrir("Olá, janela!", 340, 150);
    if (!j) { printf("Rode dentro do servidor de janelas: janelas ./ola\n"); return 1; }

    jn_controle_t *pergunta = jn_rotulo(j, 20, 15, 300, 24, "Qual é o seu nome?");
    jn_controle_t *nome = jn_caixa(j, 20, 45, 200, 30, "");
    jn_controle_t *ok = jn_botao(j, 230, 45, 90, 30, "Dizer olá");
    jn_controle_t *resposta = jn_rotulo(j, 20, 95, 300, 34, "");
    resposta->tamanho = 22;
    resposta->negrito = 1;
    resposta->cor = GR_RGB(255, 106, 0);
    (void)pergunta;
    jn_desenhar_controles(j);

    jn_evento_t e;
    while (jn_esperar(j, &e) && e.tipo != JN_FECHAR) {
        jn_controle_t *c = jn_tratar(j, &e);
        if (c == ok || c == nome) {             /* o botão, ou Enter na caixa */
            char texto[160];
            snprintf(texto, sizeof texto, "Olá, %s!", nome->texto[0] ? nome->texto : "você");
            jn_controle_texto(j, resposta, texto);
        }
    }
    jn_fechar(j);
    return 0;
}
