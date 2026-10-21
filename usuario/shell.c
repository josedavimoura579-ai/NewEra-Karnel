/*
 * ===========================================================================
 *  NovaEra - usuario/shell.c   (PID 3, anel 3)
 *  O shell é um programa comum: não enxerga a tela nem o teclado.
 *  Para escrever, manda mensagem ao servidor de vídeo; para ler uma
 *  tecla, faz uma chamada ao driver de teclado.
 * ===========================================================================
 */
#include "ulib.h"
#include "sistema.h"      /* gerado de sistema.conf pelo build.sh */

/* ------------------------- Conversa com os servidores -------------------- */

static void escreve(const char *s, u8 cor)
{
    msg_t m;
    while (*s) {                           /* textos longos vão em pedaços */
        msg_zera(&m);
        m.tipo   = MSG_VIDEO_ESCREVER;
        m.arg[0] = cor;
        u32 i = 0;
        while (*s && i < MSG_TEXTO_MAX - 1) m.texto[i++] = *s++;
        sys_enviar(PID_VIDEO, &m);
    }
}

static void escreve_num(u32 n, u8 cor)
{
    char buf[11];
    escreve(num_texto(n, buf), cor);
}

/* Escreve s e completa com espaços até 'largura' colunas. */
static void escreve_coluna(const char *s, u32 largura, u8 cor)
{
    escreve(s, cor);
    for (u32 i = str_tam(s); i < largura; i++) escreve(" ", cor);
}

static void limpar_tela(void)
{
    msg_t m;
    msg_zera(&m);
    m.tipo = MSG_VIDEO_LIMPAR;
    sys_enviar(PID_VIDEO, &m);
}

static char ler_tecla(void)
{
    msg_t m;
    msg_zera(&m);
    m.tipo = MSG_TECLADO_LER;
    if (sys_chamar(PID_TECLADO, &m) < 0) return 0;   /* driver reiniciando */
    return (char)m.arg[0];
}

/* ------------------------------- Comandos -------------------------------- */

static const char *const ESTADOS[] = { "livre", "pronto", "enviando", "recebendo", "morto" };
static const char *const EXCECOES[] = {
    "Divisao por zero", "Debug", "NMI", "Breakpoint", "Overflow",
    "Limite excedido", "Opcode invalido", "FPU indisponivel", "Falta dupla",
    "Segmento do coprocessador", "TSS invalido", "Segmento ausente",
    "Falha de pilha", "Protecao geral (GPF)", "Falha de pagina"
};

static void cmd_ajuda(void)
{
    const u8 c = COR(COR_AMARELO), d = COR(COR_CINZA_CLARO);
    escreve("  ajuda    ", c); escreve("esta lista\n", d);
    escreve("  limpar   ", c); escreve("limpa a tela\n", d);
    escreve("  ps       ", c); escreve("lista os processos\n", d);
    escreve("  mem      ", c); escreve("memoria fisica livre\n", d);
    escreve("  tempo    ", c); escreve("tempo desde o boot\n", d);
    escreve("  sobre    ", c); escreve("o que e um microkernel\n", d);
    escreve("  invadir  ", c); escreve("tenta ler a memoria do nucleo (e o shell morre!)\n", d);
    escreve("  reboot   ", c); escreve("reinicia o computador\n", d);
}

static void cmd_ps(void)
{
    const u8 t = COR(COR_CIANO_CLARO), v = COR(COR_BRANCO);
    escreve("PID  NOME       ANEL  ESTADO      REINICIOS\n", t);
    for (u32 pid = 0; ; pid++) {
        msg_t m;
        if (sys_tarefa_info(pid, &m) < 0) break;
        if (m.arg[0] == ESTADO_LIVRE) continue;
        char buf[11];
        escreve_coluna(num_texto(pid, buf), 5, v);
        escreve_coluna(m.texto, 11, v);
        escreve_coluna(pid == 0 ? "0" : "3", 6, v);
        escreve_coluna(m.arg[0] <= ESTADO_MORTO ? ESTADOS[m.arg[0]] : "?", 12, v);
        escreve_num(m.arg[1], v);
        escreve("\n", v);
    }
}

static void cmd_mem(void)
{
    u64 r = sys_memoria();
    escreve_num((u32)(r >> 32), COR(COR_BRANCO));
    escreve(" KiB livres de ", COR(COR_CINZA_CLARO));
    escreve_num((u32)r, COR(COR_BRANCO));
    escreve(" KiB gerenciados pelo nucleo\n", COR(COR_CINZA_CLARO));
}

static void cmd_tempo(void)
{
    u32 t = sys_tempo();
    escreve("Ligado ha ", COR(COR_CINZA_CLARO));
    escreve_num(t / 100, COR(COR_BRANCO));
    escreve(" segundos (", COR(COR_CINZA_CLARO));
    escreve_num(t, COR(COR_BRANCO));
    escreve(" ticks do timer)\n", COR(COR_CINZA_CLARO));
}

static void cmd_sobre(void)
{
    const u8 d = COR(COR_CINZA_CLARO);
    escreve("NovaEra e um MICROKERNEL: o nucleo (anel 0) so cuida de memoria,\n", d);
    escreve("processos, mensagens e interrupcoes. Video, teclado e este shell\n", d);
    escreve("sao processos separados no anel 3, cada um na sua memoria, e\n", d);
    escreve("conversam por mensagens. Se um deles falhar, o nucleo o reinicia.\n", d);
}

static void cmd_invadir(void)
{
    escreve("Lendo 0x00100000, onde mora o codigo do nucleo...\n", COR(COR_AMARELO));
    u32 valor = mem_le32(0x00100000);      /* a paginação proíbe: falha de página */
    escreve("Consegui ler?! Isso nao deveria acontecer: ", COR(COR_VERMELHO_CLARO));
    escreve_num(valor, COR(COR_VERMELHO_CLARO));
    escreve("\n", COR(COR_VERMELHO_CLARO));
}

static void cmd_reboot(void)
{
    escreve("Pedindo ao driver de teclado para reiniciar...\n", COR(COR_AMARELO));
    msg_t m;
    msg_zera(&m);
    m.tipo = MSG_REINICIAR;
    sys_enviar(PID_TECLADO, &m);
}

/* --------------------------------- Shell --------------------------------- */

#define LINHA_MAX 128u
static char linha[LINHA_MAX + 1];

static void ler_linha(void)
{
    u32 n = 0;
    char eco[2] = { 0, 0 };

    for (;;) {
        char c = ler_tecla();
        if (c == '\n') {
            escreve("\n", COR(COR_BRANCO));
            break;
        } else if (c == '\b') {
            if (n > 0) { n--; escreve("\b", COR(COR_BRANCO)); }
        } else if (c >= 32 && c < 127 && n < LINHA_MAX) {
            linha[n++] = c;
            eco[0] = c;
            escreve(eco, COR(COR_BRANCO));
        }
    }
    linha[n] = '\0';
}

static void executar(void)
{
    char *cmd = linha;
    while (*cmd == ' ') cmd++;
    u32 n = str_tam(cmd);
    while (n > 0 && cmd[n - 1] == ' ') cmd[--n] = '\0';
    if (n == 0) return;

    if      (str_igual(cmd, "ajuda"))   cmd_ajuda();
    else if (str_igual(cmd, "limpar"))  limpar_tela();
    else if (str_igual(cmd, "ps"))      cmd_ps();
    else if (str_igual(cmd, "mem"))     cmd_mem();
    else if (str_igual(cmd, "tempo"))   cmd_tempo();
    else if (str_igual(cmd, "sobre"))   cmd_sobre();
    else if (str_igual(cmd, "invadir")) cmd_invadir();
    else if (str_igual(cmd, "reboot"))  cmd_reboot();
    else {
        escreve("Comando desconhecido: ", COR(COR_VERMELHO_CLARO));
        escreve(cmd, COR(COR_BRANCO));
        escreve("\nDigite 'ajuda' para ver os comandos.\n", COR(COR_CINZA_CLARO));
    }
}

void main(u64 reinicios, u64 falha)
{
    if (reinicios) {
        escreve("\n[nucleo] O shell anterior foi encerrado: ", COR(COR_VERMELHO_CLARO));
        escreve(falha < sizeof(EXCECOES) / sizeof(EXCECOES[0]) ? EXCECOES[falha] : "excecao",
                COR(COR_BRANCO));
        escreve(".\n[nucleo] O resto do sistema continua de pe. Shell reiniciado (",
                COR(COR_VERMELHO_CLARO));
        escreve_num((u32)reinicios, COR(COR_BRANCO));
        escreve(").\n\n", COR(COR_VERMELHO_CLARO));
    } else {
        escreve("Shell do " SISTEMA_NOME " (processo 3, anel 3, 64 bits). Digite 'ajuda'.\n\n",
                COR(COR_CINZA_CLARO));
    }

    for (;;) {
        escreve(SISTEMA_PROMPT, COR(COR_CIANO_CLARO));
        escreve("> ",      COR(COR_AMARELO));
        ler_linha();
        executar();
    }
}
