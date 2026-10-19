/*
 * ===========================================================================
 *  NovaEra Kernel v1.1 - kernel.c   (Passo 2)
 *  C puro "freestanding": sem libc, sem nenhum #include.
 *
 *  Novidades:
 *    - IDT (256 entradas) e PIC 8259 remapeado para os vetores 32-47
 *    - Driver de teclado PS/2 na IRQ 1 (porta 0x60, scancodes set 1)
 *    - Terminal VGA com cursor e rolagem
 *    - Mini shell: "limpar", "reboot" e mensagem de comando desconhecido
 * ===========================================================================
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#define VGA_MEMORIA ((volatile u16 *)0xB8000)
#define VGA_COLUNAS 80u
#define VGA_LINHAS  25u

#define MULTIBOOT_MAGIC_BOOTLOADER 0x2BADB002u

enum vga_cor {
    VGA_PRETO = 0,   VGA_AZUL,         VGA_VERDE,        VGA_CIANO,
    VGA_VERMELHO,    VGA_MAGENTA,      VGA_MARROM,       VGA_CINZA_CLARO,
    VGA_CINZA_ESCURO, VGA_AZUL_CLARO,  VGA_VERDE_CLARO,  VGA_CIANO_CLARO,
    VGA_VERMELHO_CLARO, VGA_MAGENTA_CLARO, VGA_AMARELO,  VGA_BRANCO
};

#define ATTR(texto, fundo) ((u8)((texto) | ((fundo) << 4)))
#define FUNDO       VGA_AZUL
#define ATTR_FUNDO  ATTR(VGA_BRANCO, FUNDO)

/* ----------------------------- E/S de portas ----------------------------- */

static inline void outb(u16 porta, u8 valor)
{
    __asm__ volatile ("outb %0, %1" : : "a"(valor), "Nd"(porta));
}

static inline u8 inb(u16 porta)
{
    u8 valor;
    __asm__ volatile ("inb %1, %0" : "=a"(valor) : "Nd"(porta));
    return valor;
}

/* Pequena pausa: escrever na porta 0x80 leva ~1 microssegundo. */
static inline void io_espera(void)
{
    outb(0x80, 0);
}

/* ------------------------- Utilitários de string ------------------------- */

static u32 tamanho_str(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    return n;
}

static int str_igual(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* ------------------------------ Driver VGA ------------------------------- */

static inline u16 vga_celula(char c, u8 atributo)
{
    return (u16)(((u16)atributo << 8) | (u8)c);
}

static void vga_limpar(u8 atributo)
{
    for (u32 i = 0; i < VGA_COLUNAS * VGA_LINHAS; i++)
        VGA_MEMORIA[i] = vga_celula(' ', atributo);
}

static void vga_putc(u32 linha, u32 coluna, char c, u8 atributo)
{
    if (linha >= VGA_LINHAS || coluna >= VGA_COLUNAS) return;
    VGA_MEMORIA[linha * VGA_COLUNAS + coluna] = vga_celula(c, atributo);
}

/* Escreve caractere por caractere; devolve a próxima coluna livre. */
static u32 vga_print(u32 linha, u32 coluna, const char *s, u8 atributo)
{
    for (u32 i = 0; s[i]; i++)
        vga_putc(linha, coluna + i, s[i], atributo);
    return coluna + tamanho_str(s);
}

static u32 vga_hex(u32 linha, u32 coluna, u32 valor, u8 atributo)
{
    static const char HEX[] = "0123456789ABCDEF";
    coluna = vga_print(linha, coluna, "0x", atributo);
    for (int i = 28; i >= 0; i -= 4)
        vga_putc(linha, coluna++, HEX[(valor >> i) & 0xF], atributo);
    return coluna;
}

/* Moldura de linhas duplas (CP437: 0xC9 0xCD 0xBB 0xBA 0xC8 0xBC) */
static void vga_moldura(u32 linha, u32 coluna, u32 larg, u32 alt, u8 atributo)
{
    for (u32 x = 1; x < larg - 1; x++) {
        vga_putc(linha,           coluna + x, (char)0xCD, atributo);
        vga_putc(linha + alt - 1, coluna + x, (char)0xCD, atributo);
    }
    for (u32 y = 1; y < alt - 1; y++) {
        vga_putc(linha + y, coluna,            (char)0xBA, atributo);
        vga_putc(linha + y, coluna + larg - 1, (char)0xBA, atributo);
    }
    vga_putc(linha,           coluna,            (char)0xC9, atributo);
    vga_putc(linha,           coluna + larg - 1, (char)0xBB, atributo);
    vga_putc(linha + alt - 1, coluna,            (char)0xC8, atributo);
    vga_putc(linha + alt - 1, coluna + larg - 1, (char)0xBC, atributo);
}

/* Cursor de hardware (piscante) da placa VGA */
static void vga_ativar_cursor(void)
{
    outb(0x3D4, 0x0A); outb(0x3D5, (u8)((inb(0x3D5) & 0xC0) | 14)); /* linha inicial */
    outb(0x3D4, 0x0B); outb(0x3D5, (u8)((inb(0x3D5) & 0xE0) | 15)); /* linha final   */
}

static void vga_mover_cursor(u32 linha, u32 coluna)
{
    u16 pos = (u16)(linha * VGA_COLUNAS + coluna);
    outb(0x3D4, 0x0F); outb(0x3D5, (u8)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (u8)((pos >> 8) & 0xFF));
}

/* ------------------------- Terminal (cursor + rolagem) ------------------- */

static u32 t_lin, t_col;     /* posição do cursor de texto */
static u8  t_attr = ATTR_FUNDO;

static void term_rolar(void)
{
    for (u32 i = 0; i < VGA_COLUNAS * (VGA_LINHAS - 1); i++)
        VGA_MEMORIA[i] = VGA_MEMORIA[i + VGA_COLUNAS];
    for (u32 i = 0; i < VGA_COLUNAS; i++)
        VGA_MEMORIA[(VGA_LINHAS - 1) * VGA_COLUNAS + i] = vga_celula(' ', ATTR_FUNDO);
}

static void term_ir(u32 linha, u32 coluna)
{
    t_lin = linha;
    t_col = coluna;
    vga_mover_cursor(t_lin, t_col);
}

static void term_putc(char c)
{
    if (c == '\n') {
        t_col = 0;
        t_lin++;
    } else if (c == '\b') {
        if (t_col > 0)      t_col--;
        else if (t_lin > 0) { t_lin--; t_col = VGA_COLUNAS - 1; }
        else                return;
        vga_putc(t_lin, t_col, ' ', t_attr);
    } else {
        vga_putc(t_lin, t_col, c, t_attr);
        if (++t_col >= VGA_COLUNAS) { t_col = 0; t_lin++; }
    }

    if (t_lin >= VGA_LINHAS) {
        term_rolar();
        t_lin = VGA_LINHAS - 1;
    }
    vga_mover_cursor(t_lin, t_col);
}

static void term_escreve(const char *s, u8 atributo)
{
    t_attr = atributo;
    for (u32 i = 0; s[i]; i++)
        term_putc(s[i]);
}

static void term_status(int ok, const char *texto)
{
    term_escreve("[",                  ATTR(VGA_CINZA_CLARO, FUNDO));
    term_escreve(ok ? " OK " : "FAIL", ATTR(ok ? VGA_VERDE_CLARO : VGA_VERMELHO_CLARO, FUNDO));
    term_escreve("] ",                 ATTR(VGA_CINZA_CLARO, FUNDO));
    term_escreve(texto,                ATTR(VGA_BRANCO, FUNDO));
    term_putc('\n');
}

/* ===========================================================================
 *  IDT - Interrupt Descriptor Table
 * =========================================================================== */

typedef struct __attribute__((packed)) {
    u16 base_baixo;     /* bits 0-15 do endereço do handler */
    u16 seletor;        /* seletor de código na GDT (0x08)  */
    u8  zero;
    u8  flags;          /* 0x8E = presente, anel 0, interrupt gate 32 bits */
    u16 base_alto;      /* bits 16-31 do endereço do handler */
} idt_entrada_t;

typedef struct __attribute__((packed)) {
    u16 limite;
    u32 base;
} idt_ptr_t;

/* Quadro empilhado por isr_comum (boot.asm): PUSHAD + vetor/erro + CPU */
typedef struct {
    u32 edi, esi, ebp, esp_pushad, ebx, edx, ecx, eax;   /* PUSHAD      */
    u32 vetor, erro;                                     /* stubs       */
    u32 eip, cs, eflags;                                 /* empilhado pela CPU */
} quadro_t;

extern const u32 isr_tabela[];     /* definida em boot.asm: 48 endereços */

static idt_entrada_t idt[256];

static void idt_definir(u8 vetor, u32 handler, u16 seletor, u8 flags)
{
    idt[vetor].base_baixo = (u16)(handler & 0xFFFF);
    idt[vetor].seletor    = seletor;
    idt[vetor].zero       = 0;
    idt[vetor].flags      = flags;
    idt[vetor].base_alto  = (u16)((handler >> 16) & 0xFFFF);
}

static void idt_iniciar(void)
{
    for (u32 i = 0; i < 256; i++)
        idt_definir((u8)i, 0, 0, 0);                    /* não presente */

    for (u32 i = 0; i < 48; i++)
        idt_definir((u8)i, isr_tabela[i], 0x08, 0x8E);  /* exceções + IRQs */

    idt_ptr_t ptr;
    ptr.limite = (u16)(sizeof(idt) - 1);
    ptr.base   = (u32)idt;
    __asm__ volatile ("lidt %0" : : "m"(ptr));
}

/* ===========================================================================
 *  PIC 8259 - controlador de interrupções
 * =========================================================================== */

#define PIC1_CMD   0x20
#define PIC1_DADOS 0x21
#define PIC2_CMD   0xA0
#define PIC2_DADOS 0xA1
#define PIC_EOI    0x20

/* Por padrão a BIOS mapeia IRQ 0-7 nos vetores 8-15, que colidem com as
 * exceções da CPU. Remapeamos: IRQ 0-7 -> 32-39 e IRQ 8-15 -> 40-47. */
static void pic_iniciar(void)
{
    outb(PIC1_CMD, 0x11);   io_espera();   /* ICW1: inicialização (precisa ICW4) */
    outb(PIC2_CMD, 0x11);   io_espera();
    outb(PIC1_DADOS, 0x20); io_espera();   /* ICW2: offset do mestre = 32 */
    outb(PIC2_DADOS, 0x28); io_espera();   /* ICW2: offset do escravo = 40 */
    outb(PIC1_DADOS, 0x04); io_espera();   /* ICW3: escravo ligado na IRQ 2 */
    outb(PIC2_DADOS, 0x02); io_espera();   /* ICW3: identidade em cascata = 2 */
    outb(PIC1_DADOS, 0x01); io_espera();   /* ICW4: modo 8086 */
    outb(PIC2_DADOS, 0x01); io_espera();

    outb(PIC1_DADOS, 0xFD);                /* máscara: só a IRQ 1 (teclado) ligada */
    outb(PIC2_DADOS, 0xFF);                /* escravo: tudo mascarado */
}

static void pic_eoi(u32 irq)
{
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

/* ===========================================================================
 *  Driver de teclado PS/2 (IRQ 1)
 * =========================================================================== */

#define TECLADO_DADOS  0x60
#define TECLADO_STATUS 0x64

/* Scancodes "set 1" -> ASCII, layout US. Índice = scancode de tecla pressionada. */
static const char MAPA_NORMAL[58] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' '
};

static const char MAPA_SHIFT[58] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' '
};

/* Fila circular: a IRQ produz caracteres, o shell consome. */
#define FILA_TAM 256u
static volatile char fila[FILA_TAM];
static volatile u32  fila_ini, fila_fim;

static void fila_empurra(char c)
{
    u32 prox = (fila_fim + 1) & (FILA_TAM - 1);
    if (prox == fila_ini) return;          /* fila cheia: descarta a tecla */
    fila[fila_fim] = c;
    fila_fim = prox;
}

static u8 kb_shift, kb_caps, kb_estendido;

/* Chamada a CADA tecla: lê a porta 0x60 e converte o scancode em caractere. */
static void teclado_irq(void)
{
    u8 sc = inb(TECLADO_DADOS);

    if (sc == 0xE0) { kb_estendido = 1; return; }          /* prefixo (setas...) */
    if (kb_estendido) { kb_estendido = 0; return; }        /* teclas estendidas: ignoradas */

    if (sc & 0x80) {                                       /* tecla SOLTA */
        u8 solta = sc & 0x7F;
        if (solta == 0x2A || solta == 0x36) kb_shift = 0;
        return;
    }

    if (sc == 0x2A || sc == 0x36) { kb_shift = 1; return; }    /* Shift esq/dir */
    if (sc == 0x3A)               { kb_caps ^= 1; return; }    /* Caps Lock */
    if (sc >= sizeof(MAPA_NORMAL)) return;

    char base = MAPA_NORMAL[sc];
    char c = kb_shift ? MAPA_SHIFT[sc] : base;
    if (kb_caps && base >= 'a' && base <= 'z')
        c = kb_shift ? base : MAPA_SHIFT[sc];

    if (c) fila_empurra(c);
}

static void teclado_iniciar(void)
{
    /* Esvazia qualquer byte pendente no controlador (máx. 16 leituras). */
    for (int i = 0; i < 16 && (inb(TECLADO_STATUS) & 0x01); i++)
        inb(TECLADO_DADOS);
}

/* Bloqueia (dormindo a CPU com HLT) até haver um caractere na fila. */
static char teclado_ler_char(void)
{
    for (;;) {
        __asm__ volatile ("cli");
        if (fila_ini != fila_fim) {
            char c = fila[fila_ini];
            fila_ini = (fila_ini + 1) & (FILA_TAM - 1);
            __asm__ volatile ("sti");
            return c;
        }
        /* STI + HLT juntos: a interrupção só é aceita depois do HLT,
         * então nenhuma tecla se perde entre o teste e o sono. */
        __asm__ volatile ("sti; hlt");
    }
}

/* ===========================================================================
 *  Despacho de interrupções (chamado por isr_comum em boot.asm)
 * =========================================================================== */

static const char *const EXC_NOMES[22] = {
    "Divisao por zero", "Debug", "NMI", "Breakpoint", "Overflow",
    "Limite excedido", "Opcode invalido", "FPU indisponivel", "Falta dupla",
    "Segmento do coprocessador", "TSS invalido", "Segmento ausente",
    "Falha de pilha", "Protecao geral (GPF)", "Falha de pagina", "Reservado",
    "Erro x87", "Alinhamento", "Machine check", "Erro SIMD",
    "Virtualizacao", "Protecao de fluxo"
};

/* Exceção da CPU: mostra um painel vermelho e para. Sem isso a CPU
 * reiniciaria em loop (triple fault) e você não veria o motivo. */
static void kernel_panico(const quadro_t *q) __attribute__((noreturn));
static void kernel_panico(const quadro_t *q)
{
    const u8 a = ATTR(VGA_BRANCO, VGA_VERMELHO);
    for (u32 i = 0; i < VGA_COLUNAS * 3; i++)
        VGA_MEMORIA[i] = vga_celula(' ', a);

    u32 c = vga_print(0, 1, "*** PANICO DO KERNEL: ", a);
    vga_print(0, c, q->vetor < 22 ? EXC_NOMES[q->vetor] : "Reservado", a);

    c = vga_print(1, 1, "VETOR=", a);   c = vga_hex(1, c, q->vetor, a);
    c = vga_print(1, c, "  ERRO=", a);  vga_hex(1, c, q->erro, a);

    c = vga_print(2, 1, "EIP=", a);     c = vga_hex(2, c, q->eip, a);
    c = vga_print(2, c, " CS=", a);     c = vga_hex(2, c, q->cs, a);
    c = vga_print(2, c, " EFLAGS=", a); vga_hex(2, c, q->eflags, a);

    for (;;)
        __asm__ volatile ("cli; hlt");
}

void interrupt_dispatch(quadro_t *q)
{
    if (q->vetor < 32) {
        kernel_panico(q);
    } else if (q->vetor < 48) {
        u32 irq = q->vetor - 32;
        if (irq == 1)
            teclado_irq();
        pic_eoi(irq);                       /* avisa o PIC: "terminei" */
    }
}

/* ===========================================================================
 *  Reinicialização via controlador de teclado (porta 0x64)
 * =========================================================================== */

static void sistema_reiniciar(void) __attribute__((noreturn));
static void sistema_reiniciar(void)
{
    __asm__ volatile ("cli");

    /* Espera o buffer de entrada do controlador esvaziar (bit 1 do status). */
    for (u32 tentativas = 0x10000; (inb(TECLADO_STATUS) & 0x02) && tentativas; tentativas--)
        ;

    outb(TECLADO_STATUS, 0xFE);             /* comando 0xFE: pulsa a linha RESET da CPU */

    /* Plano B: se o reset não funcionou, força um "triple fault"
     * (IDT vazia + interrupção = a CPU reinicia sozinha). */
    idt_ptr_t vazia = { 0, 0 };
    __asm__ volatile ("lidt %0; int3" : : "m"(vazia));

    for (;;)
        __asm__ volatile ("hlt");
}

/* ===========================================================================
 *  Mini shell do NovaEra
 * =========================================================================== */

#define LINHA_MAX 128u
static char linha[LINHA_MAX + 1];
static u32  linha_len;

static void shell_prompt(void)
{
    term_escreve("NovaEra", ATTR(VGA_CIANO_CLARO, FUNDO));
    term_escreve("> ",      ATTR(VGA_AMARELO, FUNDO));
}

/* Lê uma linha digitada, com eco na tela, até o Enter. */
static void shell_ler_linha(void)
{
    linha_len = 0;
    t_attr = ATTR(VGA_BRANCO, FUNDO);

    for (;;) {
        char c = teclado_ler_char();

        if (c == '\n') {
            term_putc('\n');
            break;
        } else if (c == '\b') {
            if (linha_len > 0) {            /* não apaga o prompt */
                linha_len--;
                term_putc('\b');
            }
        } else if (c >= 32 && c < 127 && linha_len < LINHA_MAX) {
            linha[linha_len++] = c;
            term_putc(c);
        }
    }
    linha[linha_len] = '\0';
}

static void shell_executar(void)
{
    char *cmd = linha;
    while (*cmd == ' ') cmd++;                          /* ignora espaços iniciais */

    u32 n = tamanho_str(cmd);
    while (n > 0 && cmd[n - 1] == ' ') cmd[--n] = '\0'; /* e os finais */

    if (n == 0) return;                                 /* só Enter: nada a fazer */

    if (str_igual(cmd, "limpar")) {
        vga_limpar(ATTR_FUNDO);
        term_ir(0, 0);
    } else if (str_igual(cmd, "reboot")) {
        term_escreve("Reiniciando o NovaEra...\n", ATTR(VGA_AMARELO, FUNDO));
        sistema_reiniciar();
    } else {
        term_escreve("Comando desconhecido: ", ATTR(VGA_VERMELHO_CLARO, FUNDO));
        term_escreve(cmd,                      ATTR(VGA_BRANCO, FUNDO));
        term_putc('\n');
        term_escreve("Comandos disponiveis: limpar, reboot\n", ATTR(VGA_CINZA_CLARO, FUNDO));
    }
}

/* ------------------------------- Mensagem ------------------------------- */

static const char MSG_NOME[] = "NovaEra Kernel v1.0";
static const char MSG_SEP[]  = " - ";
/* A VGA usa CP437, onde 0x87 = 'c cedilha'. Por isso "Come\x87ou". */
static const char MSG_LEMA[] = "A Nova Era Come" "\x87" "ou";

static void banner(void)
{
    const u32 larg = tamanho_str(MSG_NOME) + tamanho_str(MSG_SEP) + tamanho_str(MSG_LEMA);
    const u32 col  = (VGA_COLUNAS - larg) / 2;

    vga_moldura(0, col - 3, larg + 6, 3, ATTR(VGA_CIANO_CLARO, FUNDO));

    u32 c = col;
    c = vga_print(1, c, MSG_NOME, ATTR(VGA_BRANCO, FUNDO));
    c = vga_print(1, c, MSG_SEP,  ATTR(VGA_CINZA_CLARO, FUNDO));
    vga_print(1, c, MSG_LEMA,     ATTR(VGA_AMARELO, FUNDO));
}

/* --------------------------- Ponto de entrada C -------------------------- */

void kernel_main(u32 magic, u32 mbi_endereco)
{
    (void)mbi_endereco;   /* reservado: mapa de memória virá em um próximo passo */

    vga_ativar_cursor();
    vga_limpar(ATTR_FUNDO);
    banner();
    term_ir(4, 0);

    term_status(1, "Modo protegido 32 bits (GDT propria)");
    term_status(magic == MULTIBOOT_MAGIC_BOOTLOADER, "Bootloader Multiboot detectado");

    idt_iniciar();
    term_status(1, "IDT instalada: 32 excecoes + 16 IRQs");

    pic_iniciar();
    term_status(1, "PIC remapeado: IRQ 0-15 -> vetores 32-47");

    teclado_iniciar();
    term_status(1, "Teclado PS/2 ativo na IRQ 1 (porta 0x60)");

    term_escreve("\nDigite um comando: limpar | reboot\n\n", ATTR(VGA_CINZA_CLARO, FUNDO));

    __asm__ volatile ("sti");               /* ACORDA o processador: interrupções ligadas */

    for (;;) {                              /* loop principal do kernel shell */
        shell_prompt();
        shell_ler_linha();
        shell_executar();
    }
}
