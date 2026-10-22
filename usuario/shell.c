/*
 * ===========================================================================
 *  NovaEra - usuario/shell.c   (PID 3, anel 3)
 *  O shell é um programa comum: não enxerga a tela nem o teclado. Ele lê
 *  linhas do terminal (a entrada padrão), escreve no terminal (a saída
 *  padrão) e executa os programas que o servidor de arquivos guarda em
 *  /bin, ligando a entrada e a saída deles a pipes e arquivos:
 *
 *      ls /bin | cat         echo oi > /tmp/a         cat < /tmp/a
 * ===========================================================================
 */
#include "ulib.h"
#include "sistema.h"      /* gerado de sistema.conf pelo build.sh */

static void escreve_coluna(const char *s, u32 largura, u8 cor)
{
    escreve(s, cor);
    for (u32 i = str_tam(s); i < largura; i++) escreve(" ", cor);
}

/* ---------------------------- Comandos internos --------------------------- */

static const char *const ESTADOS[] = {
    "livre", "pronto", "enviando", "recebendo", "morto", "esperando", "zumbi"
};
static const char *const EXCECOES[] = {
    "Divisao por zero", "Debug", "NMI", "Breakpoint", "Overflow",
    "Limite excedido", "Opcode invalido", "FPU indisponivel", "Falta dupla",
    "Segmento do coprocessador", "TSS invalido", "Segmento ausente",
    "Falha de pilha", "Protecao geral (GPF)", "Falha de pagina"
};

static void cmd_ajuda(void)
{
    const u8 c = COR(COR_AMARELO), d = COR(COR_CINZA_CLARO);
    escreve("Comandos do shell:\n", COR(COR_CIANO_CLARO));
    escreve("  cd, pwd  ", c); escreve("muda / mostra a pasta atual\n", d);
    escreve("  limpar   ", c); escreve("limpa a tela\n", d);
    escreve("  ps       ", c); escreve("lista os processos\n", d);
    escreve("  mem      ", c); escreve("memoria fisica livre\n", d);
    escreve("  tempo    ", c); escreve("tempo desde o boot\n", d);
    escreve("  sobre    ", c); escreve("o que e um microkernel\n", d);
    escreve("  invadir  ", c); escreve("tenta ler a memoria do nucleo (e o shell morre!)\n", d);
    escreve("  reboot   ", c); escreve("reinicia o computador\n", d);
    escreve("Programas em /bin (veja: ls /bin):\n", COR(COR_CIANO_CLARO));
    escreve("  ls cat echo mkdir rm cmp novafetch ola codigo memoria falha filho\n", c);
    escreve("  cproc qbe nefs-cpp nefs-as nefs-ld nefs-ar  (o compilador; nefs livro 4)\n", c);
    escreve("Pipes e arquivos: ", COR(COR_CIANO_CLARO));
    escreve("ls /bin | cat    echo oi > /tmp/a    cat < /tmp/a\n", c);
}

static void cmd_ps(void)
{
    const u8 t = COR(COR_CIANO_CLARO), v = COR(COR_BRANCO);
    escreve("PID  NOME       ANEL  PAI  ESTADO      REINICIOS\n", t);
    for (u32 pid = 0; ; pid++) {
        msg_t m;
        if (sys_tarefa_info(pid, &m) < 0) break;
        if (m.arg[0] == ESTADO_LIVRE) continue;
        char buf[11];
        escreve_coluna(num_texto(pid, buf), 5, v);
        escreve_coluna(m.texto, 11, v);
        escreve_coluna(pid == 0 ? "0" : "3", 6, v);
        escreve_coluna(num_texto(m.arg[3], buf), 5, v);
        escreve_coluna(m.arg[0] <= ESTADO_ZUMBI ? ESTADOS[m.arg[0]] : "?", 12, v);
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
    escreve("processos, mensagens e interrupcoes. Video, teclado, terminal,\n", d);
    escreve("arquivos, pipes e este shell sao processos separados no anel 3,\n", d);
    escreve("cada um na sua memoria, conversando por mensagens.\n", d);
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

static void cmd_limpar(void)
{
    msg_t m;
    msg_zera(&m);
    m.tipo = MSG_VIDEO_LIMPAR;
    sys_enviar(PID_VIDEO, &m);
}

static void cmd_cd(const char *destino)
{
    char abs[CAMINHO_MAX];
    if (!destino) destino = "/";
    if (caminho_absoluto(destino, abs) < 0 || arq_info(abs, 0) != ARQ_TIPO_PASTA) {
        escreve("cd: pasta nao encontrada: ", COR(COR_VERMELHO_CLARO));
        escreve(destino, COR(COR_BRANCO));
        escreve("\n", COR(COR_BRANCO));
        return;
    }
    sys_pasta(abs);
    str_copia(ambiente()->pasta, abs, CAMINHO_MAX);   /* a ulib guarda uma cópia */
}

/* ------------------------- Interpretando a linha --------------------------
 * A linha vira até 4 comandos ligados por '|'. Cada comando tem as suas
 * palavras (argv) e, opcionalmente, '<' arquivo, '>' arquivo, '>>' arquivo. */

#define LINHA_MAX     200u
#define MAX_COMANDOS  4u

typedef struct {
    char *argv[ARGV_MAX + 1];
    u32   argc;
    char *entrada;              /* < arquivo */
    char *saida;                /* > ou >> arquivo */
    u32   anexar;
} comando_t;

static char linha[LINHA_MAX + 1];

static int interpretar(comando_t *cmds, u32 *n)
{
    u32 k = 0;
    mem_zera(cmds, sizeof(comando_t) * MAX_COMANDOS);
    char *p = linha;
    char **proximo_arquivo = 0;
    for (;;) {
        while (*p == ' ' || *p == '\n') *p++ = '\0';
        if (!*p) break;
        char *palavra = p;
        while (*p && *p != ' ' && *p != '\n') p++;
        if (*p) *p++ = '\0';

        comando_t *c = &cmds[k];
        if (str_igual(palavra, "|")) {
            if (c->argc == 0 || ++k == MAX_COMANDOS) return 0;
        } else if (str_igual(palavra, "<")) {
            proximo_arquivo = &c->entrada;
        } else if (str_igual(palavra, ">") || str_igual(palavra, ">>")) {
            proximo_arquivo = &c->saida;
            c->anexar = palavra[1] == '>';
        } else if (proximo_arquivo) {
            *proximo_arquivo = palavra;
            proximo_arquivo = 0;
        } else if (c->argc < ARGV_MAX) {
            c->argv[c->argc++] = palavra;
        }
    }
    if (proximo_arquivo) return 0;                   /* "echo oi >" sem arquivo */
    *n = cmds[k].argc ? k + 1 : k;
    return *n > 0 && (*n == 1 || cmds[*n - 1].argc);
}

static int interno(comando_t *c)
{
    const char *cmd = c->argv[0];
    if      (str_igual(cmd, "ajuda"))   cmd_ajuda();
    else if (str_igual(cmd, "limpar"))  cmd_limpar();
    else if (str_igual(cmd, "ps"))      cmd_ps();
    else if (str_igual(cmd, "mem"))     cmd_mem();
    else if (str_igual(cmd, "tempo"))   cmd_tempo();
    else if (str_igual(cmd, "sobre"))   cmd_sobre();
    else if (str_igual(cmd, "invadir")) cmd_invadir();
    else if (str_igual(cmd, "reboot"))  cmd_reboot();
    else if (str_igual(cmd, "cd"))      cmd_cd(c->argv[1]);
    else if (str_igual(cmd, "pwd"))     { escreve(ambiente()->pasta, COR(COR_BRANCO)); escreve("\n", 0); }
    else return 0;
    return 1;
}

static void avisar_fim(const char *nome, long codigo)
{
    if (codigo >= SAIDA_FALHA_BASE && codigo < SAIDA_FALHA_BASE + 32) {
        u32 v = (u32)(codigo - SAIDA_FALHA_BASE);
        escreve("[nucleo] ", COR(COR_VERMELHO_CLARO));
        escreve(nome, COR(COR_BRANCO));
        escreve(" foi encerrado: ", COR(COR_VERMELHO_CLARO));
        escreve(v < sizeof(EXCECOES) / sizeof(EXCECOES[0]) ? EXCECOES[v] : "excecao",
                COR(COR_BRANCO));
        escreve(". O sistema continua de pe.\n", COR(COR_VERMELHO_CLARO));
    } else if (codigo != 0) {
        escreve("[", COR(COR_CINZA_CLARO));
        escreve(nome, COR(COR_CINZA_CLARO));
        escreve(" terminou com codigo ", COR(COR_CINZA_CLARO));
        escreve_num((u32)codigo, COR(COR_BRANCO));
        escreve("]\n", COR(COR_CINZA_CLARO));
    }
}

static void erro_arquivo(const char *texto, const char *arquivo)
{
    escreve(texto, COR(COR_VERMELHO_CLARO));
    escreve(arquivo, COR(COR_BRANCO));
    escreve("\n", COR(COR_BRANCO));
}

/* Executa os comandos ligados por pipes e espera todos terminarem. */
static void rodar(comando_t *cmds, u32 n)
{
    long pids[MAX_COMANDOS], tubos[MAX_COMANDOS], abertos[2 * MAX_COMANDOS];
    u32 n_abertos = 0;
    for (u32 i = 0; i < n; i++) pids[i] = tubos[i] = -1;

    for (u32 i = 0; i < n; i++) {
        comando_t *c = &cmds[i];
        u32 ent = 0, ent_c = 0, sai = 0, sai_c = 0;    /* 0 = herda (o terminal) */

        if (i > 0) { ent = PID_TUBOS; ent_c = (u32)tubos[i - 1]; }
        if (c->entrada) {
            long a = arq_abrir(c->entrada, ARQ_MODO_LER);
            if (a < 0) { erro_arquivo("Arquivo nao encontrado: ", c->entrada); break; }
            abertos[n_abertos++] = a;
            ent = PID_ARQUIVOS; ent_c = (u32)a;
        }
        if (i + 1 < n) {
            tubos[i] = tubo_criar();
            if (tubos[i] < 0) { escreve("Sem pipes livres\n", COR(COR_VERMELHO_CLARO)); break; }
            sai = PID_TUBOS; sai_c = (u32)tubos[i];
        }
        if (c->saida) {
            long a = arq_abrir(c->saida, c->anexar ? ARQ_MODO_ANEXAR : ARQ_MODO_ESCREVER);
            if (a < 0) { erro_arquivo("Nao consegui criar: ", c->saida); break; }
            abertos[n_abertos++] = a;
            sai = PID_ARQUIVOS; sai_c = (u32)a;
        }

        /* "ola" -> /bin/ola; "./prog" ou "/tmp/prog" -> como escrito */
        char caminho[CAMINHO_MAX];
        const char *nome = c->argv[0];
        int tem_barra = 0;
        for (const char *q = nome; *q; q++) if (*q == '/') tem_barra = 1;
        if (tem_barra) {
            str_copia(caminho, nome, sizeof(caminho));
        } else {
            str_copia(caminho, "/bin/", sizeof(caminho));
            str_copia(caminho + 5, nome, sizeof(caminho) - 5);
        }

        pids[i] = executar(caminho, c->argv, ent, ent_c, sai, sai_c);
        if (pids[i] < 0) {
            escreve(pids[i] == ERRO_NAO_ACHADO ? "Comando desconhecido: "
                    : pids[i] == ERRO_FORMATO   ? "Nao e um programa ELF valido: "
                                                : "Sem memoria ou sem vaga para: ",
                    COR(COR_VERMELHO_CLARO));
            escreve(nome, COR(COR_BRANCO));
            escreve(pids[i] == ERRO_NAO_ACHADO ? "\nDigite 'ajuda' ou 'ls /bin'.\n" : "\n",
                    COR(COR_CINZA_CLARO));
            if (i > 0 && tubos[i - 1] >= 0) {     /* ninguém vai ler: solta quem escreve */
                tubo_fechar((u32)tubos[i - 1], TUBO_LEITURA);
                tubo_fechar((u32)tubos[i - 1], TUBO_ESCRITA);
                tubos[i - 1] = -1;
            }
        }
    }

    /* Espera na ordem. O shell guarda as duas pontas de cada pipe: quando
     * quem escreve termina, ele fecha a escrita (quem lê recebe o "acabou");
     * quando quem lê termina, fecha a leitura, e o pipe é liberado. Os
     * avisos só aparecem no fim, para não se misturar com a saída. */
    long codigos[MAX_COMANDOS];
    for (u32 i = 0; i < n; i++) {
        codigos[i] = pids[i] >= 0 ? sys_esperar((u32)pids[i]) : 0;
        if (tubos[i] >= 0) tubo_fechar((u32)tubos[i], TUBO_ESCRITA);
        if (i > 0 && tubos[i - 1] >= 0) tubo_fechar((u32)tubos[i - 1], TUBO_LEITURA);
    }
    for (u32 i = 0; i < n; i++) if (pids[i] >= 0) avisar_fim(cmds[i].argv[0], codigos[i]);
    for (u32 i = 0; i < n_abertos; i++) arq_fechar((u32)abertos[i]);
}

static void executar_linha(void)
{
    comando_t cmds[MAX_COMANDOS];
    u32 n = 0;
    if (!interpretar(cmds, &n)) {
        if (n) escreve("Linha invalida (veja: ajuda)\n", COR(COR_VERMELHO_CLARO));
        return;
    }
    if (n == 1 && !cmds[0].entrada && !cmds[0].saida && interno(&cmds[0])) return;
    rodar(cmds, n);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    /* Fui reiniciado pelo núcleo? Ele guarda quantas vezes e por quê. */
    msg_t eu;
    sys_tarefa_info((u32)sys_pid(), &eu);
    u32 reinicios = eu.arg[1], falha = eu.arg[2];

    if (reinicios) {
        escreve("\n[nucleo] O shell anterior foi encerrado: ", COR(COR_VERMELHO_CLARO));
        escreve(falha < sizeof(EXCECOES) / sizeof(EXCECOES[0]) ? EXCECOES[falha] : "excecao",
                COR(COR_BRANCO));
        escreve(".\n[nucleo] O resto do sistema continua de pe. Shell reiniciado (",
                COR(COR_VERMELHO_CLARO));
        escreve_num(reinicios, COR(COR_BRANCO));
        escreve(").\n\n", COR(COR_VERMELHO_CLARO));
    } else {
        escreve("Shell do " SISTEMA_NOME " (processo 3, anel 3, 64 bits). Digite 'ajuda'.\n\n",
                COR(COR_CINZA_CLARO));
    }

    for (;;) {
        escreve(SISTEMA_PROMPT, COR(COR_CIANO_CLARO));
        escreve(":", COR(COR_CINZA_CLARO));
        escreve(ambiente()->pasta, COR(COR_AZUL_CLARO));
        escreve("> ", COR(COR_AMARELO));
        long n = ler(linha, LINHA_MAX);
        if (n <= 0) continue;
        linha[n] = '\0';
        executar_linha();
    }
}
