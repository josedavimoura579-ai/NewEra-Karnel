/*
 * usuario/bin/novafetch.c - o "neofetch" do NovaEra: o logo e as
 * informações do sistema, lado a lado.
 *
 * Como no Linux, duas fontes diferentes:
 *   - o SISTEMA (a "distribuição") vem de /etc/sistema e /etc/logo.txt,
 *     que o build gera a partir de sistema.conf e logo.txt;
 *   - o NÚCLEO responde por si mesmo (SYS_NUCLEO, como o uname).
 * No NovaEra puro (PURO=sim), não há distribuição, e o novafetch diz isso.
 */
#include "ulib.h"

#define MAX_LINHAS 12u
#define LARGURA_LOGO 24u

static char sistema_txt[512], logo_txt[1024];

/* Lê um arquivo inteiro para 'buf' (terminado em '\0'). */
static void ler_arquivo(const char *caminho, char *buf, u32 max)
{
    buf[0] = '\0';
    long canal = arq_abrir(caminho, ARQ_MODO_LER);
    if (canal < 0) return;
    u32 n = 0;
    long r;
    while (n < max - 1 && (r = ler_de(PID_ARQUIVOS, (u32)canal, buf + n, max - 1 - n)) > 0)
        n += (u32)r;
    buf[n] = '\0';
    arq_fechar((u32)canal);
}

/* O valor de "CHAVE=valor" em /etc/sistema (copiado para 'saida'). */
static const char *campo(const char *chave, char *saida, u32 max)
{
    u32 tam = str_tam(chave);
    for (const char *p = sistema_txt; *p; ) {
        u32 k = 0;
        while (k < tam && p[k] == chave[k]) k++;
        if (k == tam && p[k] == '=') {
            p += tam + 1;
            u32 i = 0;
            while (p[i] && p[i] != '\n' && i < max - 1) { saida[i] = p[i]; i++; }
            saida[i] = '\0';
            return saida;
        }
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    saida[0] = '\0';
    return saida;
}

/* Quantos caracteres aparecem na tela (os bytes de continuação do UTF-8
 * não contam), para alinhar a coluna da direita. */
static u32 largura(const char *s, u32 n)
{
    u32 w = 0;
    for (u32 i = 0; i < n; i++) if (((u8)s[i] & 0xC0) != 0x80) w++;
    return w;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    ler_arquivo("/etc/sistema", sistema_txt, sizeof(sistema_txt));
    ler_arquivo("/etc/logo.txt", logo_txt, sizeof(logo_txt));

    char nome[64], versao[32], lema[64], puro[8];
    campo("NOME", nome, sizeof(nome));
    campo("VERSAO", versao, sizeof(versao));
    campo("LEMA", lema, sizeof(lema));
    campo("PURO", puro, sizeof(puro));

    msg_t nucleo;
    sys_nucleo(&nucleo);

    u32 processos = 0;
    for (u32 pid = 0; ; pid++) {
        msg_t m;
        if (sys_tarefa_info(pid, &m) < 0) break;
        if (m.arg[0] != ESTADO_LIVRE) processos++;
    }
    u64 mem = sys_memoria();
    u32 livre = (u32)(mem >> 32), total = (u32)mem;
    u32 segundos = sys_tempo() / 100;

    /* As linhas da direita: rótulo + valor. */
    char buf[8][96];
    const char *rotulo[8];
    u32 n = 0;
    rotulo[n] = "Sistema:   ";
    if (str_igual(puro, "sim")) {
        str_copia(buf[n], nome[0] ? nome : "NovaEra", 96);
        u32 t = str_tam(buf[n]);
        str_copia(buf[n] + t, " (puro, sem distribuição)", 96 - t);
    } else {
        str_copia(buf[n], nome[0] ? nome : "(sem /etc/sistema)", 96);
        u32 t = str_tam(buf[n]);
        if (versao[0] && t + 1 < 96) { buf[n][t] = ' '; str_copia(buf[n] + t + 1, versao, 95 - t); }
    }
    n++;
    rotulo[n] = "Núcleo:    "; str_copia(buf[n], nucleo.texto, 96); n++;
    rotulo[n] = "Processos: "; num_texto(processos, buf[n]); n++;
    rotulo[n] = "Memória:   ";
    num_texto((total - livre) / 1024, buf[n]);
    str_copia(buf[n] + str_tam(buf[n]), " MiB usados de ", 40);
    num_texto(total / 1024, buf[n] + str_tam(buf[n]));
    str_copia(buf[n] + str_tam(buf[n]), " MiB", 8);
    n++;
    rotulo[n] = "Ligado há: ";
    num_texto(segundos / 60, buf[n]);
    str_copia(buf[n] + str_tam(buf[n]), " min ", 8);
    num_texto(segundos % 60, buf[n] + str_tam(buf[n]));
    str_copia(buf[n] + str_tam(buf[n]), " s", 4);
    n++;
    if (lema[0]) { rotulo[n] = "Lema:      "; str_copia(buf[n], lema, 96); n++; }

    /* Logo à esquerda (linha a linha) e informações à direita. */
    const char *p = logo_txt;
    u32 linhas = n + 1;
    for (u32 l = 0; l < MAX_LINHAS && (l < linhas || *p); l++) {
        const char *ini = p;
        while (*p && *p != '\n') p++;
        u32 tam = (u32)(p - ini);
        if (*p) p++;
        char pedaco[MSG_TEXTO_MAX];
        str_copia(pedaco, ini, tam + 1 < sizeof(pedaco) ? tam + 1 : sizeof(pedaco));
        escreve(pedaco, COR(COR_CIANO_CLARO));
        for (u32 w = largura(ini, tam); w < LARGURA_LOGO; w++) escreve(" ", 0);

        if (l == 0) {
            escreve(nome[0] ? nome : "NovaEra", COR(COR_AMARELO));
        } else if (l - 1 < n) {
            escreve(rotulo[l - 1], COR(COR_AMARELO));
            escreve(buf[l - 1], COR(COR_BRANCO));
        }
        escreve("\n", 0);
    }
    return 0;
}
