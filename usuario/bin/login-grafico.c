/*
 * usuario/bin/login-grafico.c - a tela de entrada do modo gráfico.
 *
 * O /bin/init chama este programa (no lugar do login de texto) quando o
 * sistema liga em modo gráfico. Ele mostra o papel de parede, a logo e os
 * campos de usuário e senha. Com a senha certa, vira o usuário e abre a
 * sessão: o servidor de janelas, com um terminal, na pasta pessoal. Quando
 * a sessão acaba (Ctrl+Q), ele termina, e o init mostra a entrada de novo.
 *
 * Sai com o código 3 se não houver tela gráfica: o init volta ao login de
 * texto.
 *
 * Por dentro: só a thread principal desenha (o vídeo só aceita a tela de
 * quem a abriu) e só ela confere a senha (a identidade nova vale para quem
 * pediu, e é ela que começa a sessão). As threads do teclado e do mouse só
 * mudam os campos e a acordam.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <grafico.h>
#include <janela.h>      /* (só jn_teclas: as teclas que chegam do terminal) */
#include "contas.h"
#include "papel.h"
#include "sistema.h"     /* gerado de sistema.conf pelo build.sh */

#define CARTAO_L 380
#define CARTAO_A 450
#define CAMPO_MAX 31

static pthread_mutex_t g = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  acordar = PTHREAD_COND_INITIALIZER;
static gr_imagem_t *tela, *fundo;
static gr_fonte_t *f, *n;
static char usuario[CAMPO_MAX + 1], senha[CAMPO_MAX + 1], aviso[80], maquina[32] = "novaera";
static int campo_ativo;                       /* 0 = usuário, 1 = senha */
static int sujo, tentar, entrou, minuto = -1, aviso_erro;
static u32 thread_teclado_id;
static int cx, cy, fx, fl;              /* o cartão; os campos */

/* --------------------------------- Desenhar ------------------------------ */

static void centro(gr_imagem_t *t, gr_fonte_t *fo, int x, int y, const char *s, int tam, gr_cor c)
{
    gr_fonte_texto(t, fo, x - gr_fonte_largura(fo, s, tam) / 2, y, s, tam, c);
}

static void caixa(gr_imagem_t *t, int x, int y, int l, int a, int foco)
{
    gr_preencher(t, x, y, l, a, GR_RGBA(255, 255, 255, foco ? 235 : 210));
    gr_retangulo(t, x, y, l, a, foco ? GR_RGB(46, 196, 214) : GR_RGBA(255, 255, 255, 120));
    if (foco) gr_retangulo(t, x + 1, y + 1, l - 2, a - 2, GR_RGBA(46, 196, 214, 140));
}

/* O sistema.conf vem em CP437 (o do modo texto); as letras bonitas querem UTF-8 */
static void cp437_utf8(const char *de, char *para, size_t max)
{
    static const unsigned short tab[32] = {          /* 0x80-0x9F */
        0xC7, 0xFC, 0xE9, 0xE2, 0xE4, 0xE0, 0xE5, 0xE7, 0xEA, 0xEB, 0xE8, 0xEF, 0xEE, 0xEC, 0xC4, 0xC5,
        0xC9, 0xE6, 0xC6, 0xF4, 0xF6, 0xF2, 0xFB, 0xF9, 0xFF, 0xD6, 0xDC, 0xA2, 0xA3, 0xA5, 0x20A7, 0x192 };
    static const unsigned short tab2[6] = { 0xE1, 0xED, 0xF3, 0xFA, 0xF1, 0xD1 };   /* 0xA0-0xA5 */
    size_t k = 0;
    for (; *de && k + 4 < max; de++) {
        unsigned char c = (unsigned char)*de;
        unsigned u = c < 0x80 ? c : c < 0xA0 ? tab[c - 0x80] : c < 0xA6 ? tab2[c - 0xA0] : '?';
        if (u < 0x80) para[k++] = (char)u;
        else if (u < 0x800) { para[k++] = (char)(0xC0 | u >> 6); para[k++] = (char)(0x80 | (u & 63)); }
        else { para[k++] = (char)(0xE0 | u >> 12); para[k++] = (char)(0x80 | ((u >> 6) & 63)); para[k++] = (char)(0x80 | (u & 63)); }
    }
    para[k] = 0;
}

/* O que não muda: o papel de parede, as barras, o lugar do mascote */
static void fazer_fundo(void)
{
    int L = tela->largura, A = tela->altura;
    fundo = gr_nova(L, A);
    papel_de_parede(fundo);
    gr_preencher(fundo, 0, 0, L, 34, GR_RGBA(0, 0, 0, 90));
    gr_fonte_texto(fundo, n, 16, 8, maquina, 15, GR_BRANCO);
    char v[48];
    snprintf(v, sizeof v, "NovaEra %s", NOVAERA_VERSAO);
    gr_fonte_texto(fundo, f, L - 16 - gr_fonte_largura(f, v, 15), 8, v, 15, GR_RGB(190, 200, 215));
    int mx = 70, my = A - 250;                              /* o lugar do mascote */
    for (int k = 0; k < 360; k += 6) {
        double a = k * M_PI / 180;
        gr_disco(fundo, mx + 80 + (int)(cos(a) * 80), my + 80 + (int)(sin(a) * 80), 2, GR_RGBA(255, 255, 255, 140));
    }
    centro(fundo, f, mx + 80, my + 68, "o mascote", 15, GR_RGBA(255, 255, 255, 170));
    centro(fundo, f, mx + 80, my + 88, "(em breve)", 13, GR_RGBA(255, 255, 255, 140));
    gr_preencher(fundo, 0, A - 32, L, 32, GR_RGBA(0, 0, 0, 100));
    gr_fonte_texto(fundo, f, 16, A - 24, "Enter entra   ·   Tab troca de campo", 13, GR_RGB(220, 228, 240));
    const char *dica = "Modo texto: escolha no menu do GRUB";
    gr_fonte_texto(fundo, f, L - 16 - gr_fonte_largura(f, dica, 13), A - 24, dica, 13, GR_RGB(220, 228, 240));
    /* o cartão (a parte que não muda) */
    cx = (L - CARTAO_L) / 2;
    cy = 120;
    fx = cx + 40;
    fl = CARTAO_L - 80;
    gr_preencher(fundo, cx + 8, cy + 10, CARTAO_L, CARTAO_A, GR_RGBA(0, 0, 0, 70));
    gr_preencher(fundo, cx, cy, CARTAO_L, CARTAO_A, GR_RGBA(11, 30, 63, 200));
    gr_retangulo(fundo, cx, cy, CARTAO_L, CARTAO_A, GR_RGBA(46, 196, 214, 160));
    gr_imagem_t *logo = gr_carregar("/usr/share/nefs/exemplos/imagens/logo.png");
    if (logo) { gr_escalar(fundo, L / 2 - 48, cy + 26, 96, 96, logo); gr_liberar(logo); }
    int w = gr_fonte_largura(n, "NovaEra", 34), x = (L - w) / 2;
    x += gr_fonte_texto(fundo, n, x, cy + 132, "Nova", 34, GR_BRANCO);
    gr_fonte_texto(fundo, n, x, cy + 132, "Era", 34, GR_LARANJA);
    char lema[96];
    cp437_utf8(SISTEMA_LEMA, lema, sizeof lema);
    centro(fundo, f, L / 2, cy + 180, lema, 15, GR_RGB(159, 179, 207));
    gr_fonte_texto(fundo, f, fx, cy + 220, "Usuário", 13, GR_RGB(190, 200, 215));
    gr_fonte_texto(fundo, f, fx, cy + 288, "Senha", 13, GR_RGB(190, 200, 215));
}

/* Copia o fundo num retângulo da tela */
static void restaurar(int x, int y, int l, int a)
{
    for (int j = y; j < y + a; j++)
        memcpy(&tela->pontos[(size_t)j * tela->passo + x], &fundo->pontos[(size_t)j * fundo->passo + x],
               (size_t)l * sizeof(gr_cor));
}

static void desenhar_relogio(void)
{
    static const char *const dias[7] = { "domingo", "segunda", "terça", "quarta", "quinta", "sexta", "sábado" };
    static const char *const meses[12] = { "janeiro", "fevereiro", "março", "abril", "maio", "junho", "julho",
                                           "agosto", "setembro", "outubro", "novembro", "dezembro" };
    time_t agora = time(NULL);
    struct tm *t = localtime(&agora);
    minuto = t->tm_min;
    char s[64];
    snprintf(s, sizeof s, "%s, %d de %s   %02d:%02d", dias[t->tm_wday], t->tm_mday, meses[t->tm_mon], t->tm_hour, t->tm_min);
    int x0 = tela->largura / 2 - 200;
    restaurar(x0, 0, 400, 34);
    centro(tela, f, tela->largura / 2, 8, s, 15, GR_RGB(220, 228, 240));
    gr_tela_mostrar(tela, x0, 0, 400, 34);
}

/* Os campos, o botão e o aviso: a parte do cartão que muda */
static void desenhar_campos(void)
{
    int y0 = cy + 236, a = CARTAO_A - 236 - 8;
    restaurar(cx + 1, y0, CARTAO_L - 2, a);
    caixa(tela, fx, cy + 240, fl, 36, campo_ativo == 0 && !entrou);
    gr_fonte_texto(tela, f, fx + 12, cy + 248, usuario, 16, GR_RGB(30, 40, 60));
    if (campo_ativo == 0 && !entrou)
        gr_preencher(tela, fx + 13 + gr_fonte_largura(f, usuario, 16), cy + 247, 2, 22, GR_RGB(36, 87, 197));
    caixa(tela, fx, cy + 308, fl, 36, campo_ativo == 1 && !entrou);
    int k = (int)strlen(senha);
    for (int i = 0; i < k && i < 20; i++) gr_disco(tela, fx + 18 + i * 16, cy + 326, 4, GR_RGB(30, 40, 60));
    if (campo_ativo == 1 && !entrou) gr_preencher(tela, fx + 12 + (k < 20 ? k : 20) * 16, cy + 315, 2, 22, GR_RGB(36, 87, 197));
    gr_degrade(tela, fx, cy + 364, fl, 40, GR_RGB(255, 150, 60), GR_RGB(240, 106, 20), 1);
    centro(tela, n, tela->largura / 2, cy + 374, entrou ? "Entrando..." : "Entrar", 17, GR_BRANCO);
    if (aviso[0]) centro(tela, f, tela->largura / 2, cy + 414, aviso, 14,
                         entrou ? GR_RGB(130, 230, 160) : aviso_erro ? GR_RGB(255, 120, 110) : GR_AMARELO);
    gr_tela_mostrar(tela, cx, y0, CARTAO_L, a);
}

/* --------------------------------- Entrar -------------------------------- */

static void terminal_cru(int ligar)
{
    msg_t m;
    msg_zera(&m);
    m.tipo = TERM_CRU;
    m.arg[0] = (u32)ligar;
    m.arg[1] = terminal_canal();
    sys_chamar(PID_TERMINAL, &m);
}

/* Confere a senha e, se der, abre a sessão. Na thread principal, com g. */
static void entrar(void)
{
    msg_t r;
    long e = contas_pedir(CONTAS_CONFERIR, usuario, senha, &r);
    if (e == 0) e = contas_pedir(CONTAS_ENTRAR, usuario, senha, &r);   /* esta thread vira o usuário */
    mem_zera(senha, sizeof senha);
    if (e < 0) {
        snprintf(aviso, sizeof aviso, "Usuário ou senha errados.");
        aviso_erro = 1;
        campo_ativo = 1;
        desenhar_campos();
        pthread_mutex_unlock(&g);
        sleep(1);                                             /* (sem adivinhar senhas depressa) */
        pthread_mutex_lock(&g);
        return;
    }
    entrou = 1;
    snprintf(aviso, sizeof aviso, "Bem-vindo, %s!", usuario);
    desenhar_campos();
    if (!r.texto[0] || sys_pasta(r.texto) < 0) sys_pasta("/");
    /* larga o teclado (a thread dele estava esperando uma tecla) */
    msg_t m;
    msg_zera(&m);
    m.tipo = TERM_ACORDAR;
    m.arg[0] = thread_teclado_id;
    m.arg[1] = terminal_canal();
    sys_chamar(PID_TERMINAL, &m);
    gr_tela_soltar(tela);                                     /* a imagem fica até as janelas abrirem */
    char *args[] = { "janelas", "--sessao", "term", 0 };
    long pid = executar("/bin/janelas", args, 0, 0, 0, 0);
    if (pid > 0) sys_esperar((u32)pid);
    terminal_cru(0);
    exit(pid > 0 ? 0 : 1);
}

/* ------------------------------ Teclado e mouse -------------------------- */

static void tecla(int c)
{
    char *s = campo_ativo ? senha : usuario;
    size_t k = strlen(s);
    if (c == '\t') campo_ativo = !campo_ativo;
    else if (c == '\n') { if (campo_ativo == 0 && usuario[0]) campo_ativo = 1; else if (usuario[0]) tentar = 1; }
    else if (c == '\b') {                             /* a última letra (UTF-8: 1 a 3 bytes) */
        while (k > 0 && ((unsigned char)s[k - 1] & 0xC0) == 0x80) s[--k] = 0;
        if (k) s[--k] = 0;
    }
    else if (c == 27) { s[0] = 0; }
    else if (c > ' ' && c != 127 && c < 0xE000 && k + 3 < CAMPO_MAX) { k += (size_t)jn_utf8(c, s + k); s[k] = 0; }
    else return;
    if (c != '\n') aviso[0] = 0;
    sujo = 1;
    pthread_cond_signal(&acordar);
}

static void *thread_teclado(void *x)
{
    (void)x;
    thread_teclado_id = (u32)sys_pid();
    char b[32];
    long n;
    while ((n = read(0, b, sizeof b)) > 0) {
        int teclas[32], mods[32];
        int k = jn_teclas(b, (int)n, teclas, mods, 32);
        pthread_mutex_lock(&g);
        if (entrou) { pthread_mutex_unlock(&g); break; }
        for (int i = 0; i < k; i++) if (!mods[i]) tecla(teclas[i]);   /* (as setas e o Alt não fazem nada aqui) */
        pthread_mutex_unlock(&g);
    }
    return NULL;
}

static void *thread_mouse(void *x)
{
    (void)x;
    int antes = 0;
    for (;;) {
        int mx, my, b;
        gr_mouse(&mx, &my, &b);
        pthread_mutex_lock(&g);
        if (entrou) { pthread_mutex_unlock(&g); break; }
        if ((b & GR_ESQUERDO) && !(antes & GR_ESQUERDO) && mx >= fx && mx < fx + fl) {
            if (my >= cy + 240 && my < cy + 276) { campo_ativo = 0; sujo = 1; }
            else if (my >= cy + 308 && my < cy + 344) { campo_ativo = 1; sujo = 1; }
            else if (my >= cy + 364 && my < cy + 404 && usuario[0]) tentar = 1;
            pthread_cond_signal(&acordar);
        }
        antes = b;
        pthread_mutex_unlock(&g);
    }
    return NULL;
}

static void *thread_relogio(void *x)
{
    (void)x;
    for (;;) {
        sleep(1);
        pthread_mutex_lock(&g);
        time_t agora = time(NULL);
        if (localtime(&agora)->tm_min != minuto) { sujo = 2; pthread_cond_signal(&acordar); }
        pthread_mutex_unlock(&g);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    f = gr_fonte_abrir(GR_FONTE_NORMAL);
    n = gr_fonte_abrir(GR_FONTE_NEGRITO);
    if (!f || !n) return 3;
    tela = gr_tela_abrir();
    if (!tela) return 3;                                      /* sem tela gráfica: o init usa o login de texto */
    u32 tam;
    u8 *h = arq_carregar("/etc/hostname", &tam);
    if (h) {
        u32 k = 0;
        while (k < tam && k + 1 < sizeof maquina && h[k] != '\n' && h[k] != '\r') { maquina[k] = (char)h[k]; k++; }
        if (k) maquina[k] = 0;
        ufree(h);
    }
    /* o root ainda sem senha? avisa (como o login de texto) */
    char *sombra = texto_de("/etc/sombra");
    char campo_root[HASH_MAX];
    if (sombra && sombra_de(sombra, "root", campo_root, sizeof campo_root) && !campo_root[0])
        snprintf(aviso, sizeof aviso, "O root ainda não tem senha: entre como root.");
    ufree(sombra);

    fazer_fundo();
    memcpy(tela->pontos, fundo->pontos, (size_t)tela->largura * (size_t)tela->altura * sizeof(gr_cor));
    gr_tela_mostrar_tudo(tela);
    desenhar_relogio();
    desenhar_campos();
    terminal_cru(1);

    pthread_t t;
    pthread_create(&t, NULL, thread_teclado, NULL);
    pthread_create(&t, NULL, thread_mouse, NULL);
    pthread_create(&t, NULL, thread_relogio, NULL);

    pthread_mutex_lock(&g);
    for (;;) {
        while (!sujo && !tentar) pthread_cond_wait(&acordar, &g);
        if (sujo == 2) desenhar_relogio();
        sujo = 0;
        if (tentar) { tentar = 0; entrar(); }
        desenhar_campos();
    }
}
