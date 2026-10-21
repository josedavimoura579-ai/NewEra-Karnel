/*
 * ===========================================================================
 *  NovaEra Microkernel v2.0 - kernel.c
 *  C puro "freestanding": sem libc. O único #include é a ABI do sistema.
 *
 *  O que mora AQUI (anel 0, o mínimo possível):
 *    - Gerenciador de memória física (lista de quadros de 4 KiB livres)
 *    - Paginação: cada processo tem o seu próprio espaço de endereçamento
 *    - Processos, TSS e troca de contexto
 *    - Escalonador preemptivo (timer PIT a 100 Hz, round-robin)
 *    - IPC síncrono por mensagens (enviar / receber / chamar / responder)
 *    - IRQs entregues como mensagens aos drivers que as assinaram
 *    - Supervisão: um processo que falha é encerrado e reiniciado
 *
 *  O que mora FORA (anel 3, pasta usuario/):
 *    - video.c    servidor de vídeo (único dono da memória VGA)
 *    - teclado.c  driver PS/2 (recebe a IRQ 1 como mensagem)
 *    - shell.c    o shell, que só conversa com os outros por mensagens
 *
 *  A tela do núcleo (vga_* e term_*) só é usada no boot e no pânico,
 *  antes de o servidor de vídeo existir ou quando tudo já deu errado.
 * ===========================================================================
 */

#include "novaera.h"

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

/* Seletores da GDT (boot.asm). O "| 3" pede o anel 3. */
#define SEL_CODIGO_NUCLEO  0x08
#define SEL_DADOS_NUCLEO   0x10
#define SEL_CODIGO_USUARIO (0x18 | 3)
#define SEL_DADOS_USUARIO  (0x20 | 3)
#define SEL_TSS            0x28

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

/* ------------------------- Utilitários de memória ------------------------ */

static u32 tamanho_str(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    return n;
}

static void mem_copia(void *dst, const void *src, u32 n)
{
    u8 *d = dst;
    const u8 *s = src;
    while (n--) *d++ = *s++;
}

static void mem_zera(void *dst, u32 n)
{
    u8 *d = dst;
    while (n--) *d++ = 0;
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

/* ------------------- Terminal do núcleo (só no boot) --------------------- */

static u32 t_lin, t_col;     /* posição do cursor de texto */
static u8  t_attr = ATTR_FUNDO;

static void term_putc(char c)
{
    if (c == '\n') {
        t_col = 0;
        t_lin++;
    } else {
        vga_putc(t_lin, t_col, c, t_attr);
        if (++t_col >= VGA_COLUNAS) { t_col = 0; t_lin++; }
    }
    if (t_lin >= VGA_LINHAS) t_lin = VGA_LINHAS - 1;   /* o boot cabe na tela */
    vga_mover_cursor(t_lin, t_col);
}

static void term_escreve(const char *s, u8 atributo)
{
    t_attr = atributo;
    for (u32 i = 0; s[i]; i++)
        term_putc(s[i]);
}

static void term_numero(u32 n, u8 atributo)
{
    char buf[11];
    int i = 10;
    buf[i] = '\0';
    do { buf[--i] = (char)('0' + n % 10); n /= 10; } while (n);
    term_escreve(&buf[i], atributo);
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
 *  Gerenciador de memória física
 *
 *  A RAM é dividida em "quadros" de 4 KiB. Os livres formam uma lista
 *  encadeada guardada DENTRO deles mesmos: os 4 primeiros bytes de cada
 *  quadro livre apontam para o próximo. Alocar e liberar custam O(1).
 *  O núcleo enxerga os primeiros 16 MiB, então só gerencia quadros ali.
 * =========================================================================== */

#define QUADRO          4096u
#define MEMORIA_NUCLEO  0x01000000u   /* 16 MiB mapeados no núcleo */

extern u8 _kernel_fim[];               /* definido em linker.ld */

static u32 quadros_livres_lista;       /* endereço físico do 1º quadro livre */
static u32 quadros_livres;
static u32 quadros_total;

static void quadro_liberar(u32 fisico)
{
    *(u32 *)fisico = quadros_livres_lista;
    quadros_livres_lista = fisico;
    quadros_livres++;
}

/* Devolve um quadro zerado, ou 0 se a memória acabou. */
static u32 quadro_alocar(void)
{
    u32 q = quadros_livres_lista;
    if (!q) return 0;
    quadros_livres_lista = *(u32 *)q;
    quadros_livres--;
    mem_zera((void *)q, QUADRO);
    return q;
}

/* Multiboot: se o bit 0 das flags estiver ligado, mem_upper diz quantos
 * KiB de RAM existem acima de 1 MiB. */
static void memoria_iniciar(u32 mbi_endereco)
{
    u32 fim = MEMORIA_NUCLEO;
    if (mbi_endereco) {
        const u32 *mbi = (const u32 *)mbi_endereco;
        if (mbi[0] & 1) {
            u32 real = 0x100000u + mbi[2] * 1024u;
            if (real < fim) fim = real;
        }
    }

    u32 ini = ((u32)_kernel_fim + QUADRO - 1) & ~(QUADRO - 1);
    for (u32 q = fim - QUADRO; q >= ini; q -= QUADRO) {   /* do fim para o início: */
        quadro_liberar(q);                                /* o 1º alocado é o mais baixo */
        quadros_total++;
    }
}

/* ===========================================================================
 *  Paginação
 *
 *  Endereço virtual de 32 bits = [10 bits diretório][10 bits tabela][12 offset]
 *  Os 16 MiB do núcleo usam as MESMAS 4 tabelas em todos os diretórios,
 *  marcadas como "supervisor": o anel 3 não consegue nem ler.
 * =========================================================================== */

#define PG_PRESENTE  0x1u
#define PG_ESCRITA   0x2u
#define PG_USUARIO   0x4u

static u32 diretorio_nucleo[1024]          __attribute__((aligned(4096)));
static u32 tabelas_nucleo[4][1024]         __attribute__((aligned(4096)));

static void paginacao_iniciar(void)
{
    for (u32 t = 0; t < 4; t++) {
        for (u32 i = 0; i < 1024; i++)
            tabelas_nucleo[t][i] = ((t * 1024 + i) * QUADRO) | PG_PRESENTE | PG_ESCRITA;
        diretorio_nucleo[t] = (u32)tabelas_nucleo[t] | PG_PRESENTE | PG_ESCRITA;
    }

    __asm__ volatile ("mov %0, %%cr3" : : "r"(diretorio_nucleo));
    u32 cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;                 /* bit PG: liga a paginação */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));
}

/* Cria um diretório novo que já enxerga o núcleo (só no anel 0). */
static u32 *espaco_criar(void)
{
    u32 *dir = (u32 *)quadro_alocar();
    if (!dir) return 0;
    for (u32 t = 0; t < 4; t++)
        dir[t] = diretorio_nucleo[t];
    return dir;
}

static int mapear(u32 *dir, u32 virtual, u32 fisico, u32 flags)
{
    u32 *pde = &dir[virtual >> 22];
    if (!(*pde & PG_PRESENTE)) {
        u32 tabela = quadro_alocar();
        if (!tabela) return 0;
        *pde = tabela | PG_PRESENTE | PG_ESCRITA | PG_USUARIO;
    }
    u32 *tabela = (u32 *)(*pde & ~0xFFFu);
    tabela[(virtual >> 12) & 1023] = fisico | flags;
    return 1;
}

/* Devolve à lista livre tudo o que pertence só ao processo. */
static void espaco_destruir(u32 *dir)
{
    for (u32 i = 4; i < 1024; i++) {
        if (!(dir[i] & PG_PRESENTE)) continue;
        u32 *tabela = (u32 *)(dir[i] & ~0xFFFu);
        for (u32 j = 0; j < 1024; j++) {
            u32 fisico = tabela[j] & ~0xFFFu;
            if ((tabela[j] & PG_PRESENTE) && fisico >= (u32)_kernel_fim)
                quadro_liberar(fisico);    /* a VGA (0xB8000) não é nossa: fica */
        }
        quadro_liberar((u32)tabela);
    }
    quadro_liberar((u32)dir);
}

/* ===========================================================================
 *  GDT/TSS
 *
 *  Quando uma interrupção chega com a CPU no anel 3, ela precisa trocar
 *  para uma pilha do anel 0. Onde fica essa pilha? No campo esp0 da TSS,
 *  que atualizamos a cada troca de processo.
 * =========================================================================== */

typedef struct __attribute__((packed)) {
    u32 anterior, esp0, ss0, esp1, ss1, esp2, ss2;
    u32 cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    u32 es, cs, ss, ds, fs, gs, ldt;
    u16 armadilha, iomap;
} tss_t;

static tss_t tss;
extern u8 gdt_tss[8];                  /* entrada reservada em boot.asm */

static void tss_iniciar(void)
{
    u32 base = (u32)&tss, limite = sizeof(tss) - 1;
    tss.ss0   = SEL_DADOS_NUCLEO;
    tss.iomap = sizeof(tss);           /* sem mapa de portas: vale o IOPL */

    gdt_tss[0] = (u8)limite;
    gdt_tss[1] = (u8)(limite >> 8);
    gdt_tss[2] = (u8)base;
    gdt_tss[3] = (u8)(base >> 8);
    gdt_tss[4] = (u8)(base >> 16);
    gdt_tss[5] = 0x89;                 /* presente, anel 0, TSS 32 bits livre */
    gdt_tss[6] = (u8)((limite >> 16) & 0x0F);
    gdt_tss[7] = (u8)(base >> 24);

    __asm__ volatile ("ltr %0" : : "r"((u16)SEL_TSS));
}

/* ===========================================================================
 *  IDT - Interrupt Descriptor Table
 * =========================================================================== */

typedef struct __attribute__((packed)) {
    u16 base_baixo;     /* bits 0-15 do endereço do handler */
    u16 seletor;        /* seletor de código na GDT (0x08)  */
    u8  zero;
    u8  flags;          /* 0x8E = anel 0 pode chamar; 0xEE = anel 3 também */
    u16 base_alto;      /* bits 16-31 do endereço do handler */
} idt_entrada_t;

typedef struct __attribute__((packed)) {
    u16 limite;
    u32 base;
} idt_ptr_t;

/* Quadro empilhado por isr_comum (boot.asm). É ELE que representa um
 * processo parado: para retomá-lo, basta apontar ESP para cá e dar IRET. */
typedef struct {
    u32 gs, fs, es, ds;                                  /* isr_comum   */
    u32 edi, esi, ebp, esp_pushad, ebx, edx, ecx, eax;   /* PUSHAD      */
    u32 vetor, erro;                                     /* stubs       */
    u32 eip, cs, eflags;                                 /* empilhado pela CPU */
    u32 esp_usuario, ss_usuario;                         /* só vindo do anel 3 */
} quadro_t;

extern const u32 isr_tabela[];     /* definida em boot.asm: 48 endereços */
extern void isr_syscall(void);

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
        idt_definir((u8)i, isr_tabela[i], SEL_CODIGO_NUCLEO, 0x8E);

    /* A ÚNICA porta que o anel 3 pode abrir: as chamadas de sistema. */
    idt_definir(0x80, (u32)isr_syscall, SEL_CODIGO_NUCLEO, 0xEE);

    idt_ptr_t ptr;
    ptr.limite = (u16)(sizeof(idt) - 1);
    ptr.base   = (u32)idt;
    __asm__ volatile ("lidt %0" : : "m"(ptr));
}

/* ===========================================================================
 *  PIC 8259 e timer PIT
 * =========================================================================== */

#define PIC1_CMD   0x20
#define PIC1_DADOS 0x21
#define PIC2_CMD   0xA0
#define PIC2_DADOS 0xA1
#define PIC_EOI    0x20

static u16 pic_mascara = 0xFFFF;   /* bit ligado = IRQ desligada */

static void pic_aplicar_mascara(void)
{
    outb(PIC1_DADOS, (u8)pic_mascara);
    outb(PIC2_DADOS, (u8)(pic_mascara >> 8));
}

static void pic_liberar_irq(u32 irq)
{
    pic_mascara &= (u16)~(1u << irq);
    if (irq >= 8) pic_mascara &= (u16)~(1u << 2);   /* a cascata também */
    pic_aplicar_mascara();
}

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

    pic_aplicar_mascara();                 /* tudo desligado; os drivers pedem */
}

static void pic_eoi(u32 irq)
{
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

#define TIMER_HZ 100u

/* O PIT oscila a 1.193.182 Hz; dividindo, temos uma IRQ 0 a cada 10 ms. */
static void timer_iniciar(void)
{
    u32 divisor = 1193182u / TIMER_HZ;
    outb(0x43, 0x36);                      /* canal 0, modo 3 (onda quadrada) */
    outb(0x40, (u8)divisor);
    outb(0x40, (u8)(divisor >> 8));
    pic_liberar_irq(0);
}

/* ===========================================================================
 *  Processos
 * =========================================================================== */

#define MAX_TAREFAS       8u
#define PILHA_NUCLEO_TAM  QUADRO
#define MAX_REINICIOS     5u

#define T_DRIVER   0x1u        /* pode usar IN/OUT (IOPL 3) e assinar IRQs */
#define T_VGA      0x2u        /* recebe a memória de vídeo mapeada */

typedef struct {
    const u8 *ini, *fim;       /* imagem embutida no kernel.bin */
} programa_t;

typedef struct {
    u32        estado;         /* ESTADO_* (novaera.h) */
    char       nome[12];
    u32        flags;
    programa_t prog;
    u32        reinicios;

    u32       *diretorio;      /* espaço de endereçamento (CR3) */
    u8        *pilha_nucleo;   /* onde o quadro_t fica enquanto está parado */
    quadro_t  *quadro;

    /* IPC */
    u32        alvo;           /* para quem envia / de quem quer receber */
    u32        chamando;       /* SYS_CHAMAR: depois de enviar, espera resposta */
    u32        ordem;          /* ordem de chegada na fila do destino */
    u32        msg_usuario;    /* endereço da msg_t no espaço do processo */
    msg_t      caixa;          /* cópia da mensagem dentro do núcleo */
    u32        entregar;       /* 1 = copiar a caixa para o usuário ao retomar */
    u32        irqs;           /* IRQs assinadas (bits) */
    u32        irqs_pendentes;
} tarefa_t;

static tarefa_t tarefas[MAX_TAREFAS];
static u32      atual;                 /* índice (= PID) de quem está rodando */
static u32      cr3_atual;
static u32      contador_ordem;
static volatile u32 ticks;

extern const u8 prog_video_ini[],   prog_video_fim[];
extern const u8 prog_teclado_ini[], prog_teclado_fim[];
extern const u8 prog_shell_ini[],   prog_shell_fim[];

static void nome_copia(char *dst, const char *src)
{
    u32 i = 0;
    for (; src[i] && i < 11; i++) dst[i] = src[i];
    dst[i] = '\0';
}

/*
 * Prepara um processo para rodar do zero: espaço de endereçamento novo,
 * imagem copiada para 0x40000000, pilha do usuário e um quadro_t "falso"
 * que, quando retomado com IRET, cai no anel 3 no início do programa.
 * EBX e ECX iniciais viram os argumentos de main() (ver usuario/ulib.h).
 */
static int processo_carregar(u32 pid, u32 arg1, u32 arg2)
{
    tarefa_t *t = &tarefas[pid];
    u32 tam = (u32)(t->prog.fim - t->prog.ini);
    if (tam > USUARIO_IMG_TAM) return 0;

    t->diretorio = espaco_criar();
    if (!t->diretorio) return 0;

    for (u32 off = 0; off < USUARIO_IMG_TAM; off += QUADRO) {
        u32 q = quadro_alocar();
        if (!q) return 0;
        if (off < tam)
            mem_copia((void *)q, t->prog.ini + off, tam - off < QUADRO ? tam - off : QUADRO);
        if (!mapear(t->diretorio, USUARIO_BASE + off, q, PG_PRESENTE | PG_ESCRITA | PG_USUARIO))
            return 0;
    }
    for (u32 off = 0; off < USUARIO_PILHA_TAM; off += QUADRO) {
        u32 q = quadro_alocar();
        if (!q || !mapear(t->diretorio, USUARIO_PILHA_BASE + off, q,
                          PG_PRESENTE | PG_ESCRITA | PG_USUARIO))
            return 0;
    }
    if (t->flags & T_VGA)
        mapear(t->diretorio, USUARIO_VGA, 0xB8000, PG_PRESENTE | PG_ESCRITA | PG_USUARIO);

    /* A pilha do núcleo é do PID para sempre: assim um reinício pode
     * acontecer enquanto o núcleo ainda está rodando sobre ela. */
    if (!t->pilha_nucleo) {
        t->pilha_nucleo = (u8 *)quadro_alocar();
        if (!t->pilha_nucleo) return 0;
    }

    quadro_t *q = (quadro_t *)(t->pilha_nucleo + PILHA_NUCLEO_TAM - sizeof(quadro_t));
    mem_zera(q, sizeof(*q));
    q->gs = q->fs = q->es = q->ds = SEL_DADOS_USUARIO;
    q->ebx         = arg1;
    q->ecx         = arg2;
    q->eip         = USUARIO_BASE;                 /* _start fica no começo */
    q->cs          = SEL_CODIGO_USUARIO;
    q->eflags      = 0x202 | ((t->flags & T_DRIVER) ? 0x3000u : 0);  /* IF + IOPL */
    q->esp_usuario = USUARIO_PILHA_BASE + USUARIO_PILHA_TAM;
    q->ss_usuario  = SEL_DADOS_USUARIO;

    t->quadro         = q;
    t->estado         = ESTADO_PRONTO;
    t->chamando       = 0;
    t->entregar       = 0;
    t->irqs           = 0;
    t->irqs_pendentes = 0;
    return 1;
}

static int processo_criar(u32 pid, const char *nome, const u8 *ini, const u8 *fim, u32 flags)
{
    tarefa_t *t = &tarefas[pid];
    nome_copia(t->nome, nome);
    t->flags    = flags;
    t->prog.ini = ini;
    t->prog.fim = fim;
    return processo_carregar(pid, 0, 0);
}

/* O PID 0 é o próprio núcleo: depois do boot, kernel_main() vira o
 * processo "ocioso", que só dorme (HLT) quando ninguém mais quer a CPU. */
static void ocioso_iniciar(void)
{
    nome_copia(tarefas[0].nome, "nucleo");
    tarefas[0].estado    = ESTADO_PRONTO;
    tarefas[0].diretorio = diretorio_nucleo;
    cr3_atual = (u32)diretorio_nucleo;
    atual = 0;
}

/* ===========================================================================
 *  Escalonador: round-robin entre os processos PRONTOS
 * =========================================================================== */

static void escalonar(void)
{
    for (u32 i = 1; i <= MAX_TAREFAS; i++) {
        u32 pid = (atual + i) % MAX_TAREFAS;
        if (pid != 0 && tarefas[pid].estado == ESTADO_PRONTO) {
            atual = pid;
            return;
        }
    }
    atual = 0;                              /* ninguém pronto: o ocioso dorme */
}

/* ===========================================================================
 *  IPC: troca de mensagens síncrona (estilo L4 / MINIX)
 *
 *  ENVIAR bloqueia até o destino RECEBER, e vice-versa: é um "encontro".
 *  A mensagem viaja assim: usuário A -> caixa de A -> caixa de B -> usuário B.
 *  A última cópia só acontece quando B volta a rodar, com o CR3 de B
 *  carregado: o núcleo nunca precisa mapear a memória de um processo
 *  dentro de outro.
 * =========================================================================== */

/* O endereço cabe inteiro na área do programa ou na pilha? */
static int ponteiro_ok(u32 p)
{
    u32 tam = sizeof(msg_t);
    if (p >= USUARIO_BASE && p <= USUARIO_BASE + USUARIO_IMG_TAM - tam) return 1;
    if (p >= USUARIO_PILHA_BASE && p <= USUARIO_PILHA_BASE + USUARIO_PILHA_TAM - tam) return 1;
    return 0;
}

static int pid_vivo(u32 pid)
{
    return pid > 0 && pid < MAX_TAREFAS &&
           tarefas[pid].estado != ESTADO_LIVRE && tarefas[pid].estado != ESTADO_MORTO;
}

/* Acorda um processo bloqueado, devolvendo 'resultado' no seu EAX. */
static void acordar(tarefa_t *t, int resultado)
{
    t->estado     = ESTADO_PRONTO;
    t->quadro->eax = (u32)resultado;
}

/* Coloca a mensagem de 'de' na caixa de 'para' e o acorda. */
static void entregar(tarefa_t *para, const msg_t *msg, u32 origem)
{
    para->caixa        = *msg;
    para->caixa.origem = origem;
    para->entregar     = 1;
    acordar(para, 0);
}

/* O remetente terminou de enviar: se era um CHAMAR, agora espera a resposta. */
static void envio_concluido(tarefa_t *t, u32 destino)
{
    if (t->chamando) {
        t->chamando = 0;
        t->estado   = ESTADO_RECEBENDO;
        t->alvo     = destino;
    } else {
        acordar(t, 0);
    }
}

static int esperando_por(const tarefa_t *t, u32 pid)
{
    return t->estado == ESTADO_RECEBENDO && (t->alvo == PID_QUALQUER || t->alvo == pid);
}

static void ipc_enviar(u32 destino)
{
    tarefa_t *eu = &tarefas[atual];
    tarefa_t *d  = &tarefas[destino];

    if (esperando_por(d, atual)) {           /* o destino já está esperando */
        entregar(d, &eu->caixa, atual);
        envio_concluido(eu, destino);
    } else {                                 /* entra na fila e dorme */
        eu->estado = ESTADO_ENVIANDO;
        eu->alvo   = destino;
        eu->ordem  = contador_ordem++;
    }
}

/* Entrega a IRQ de número mais baixo que estiver pendente. */
static int ipc_entregar_irq(tarefa_t *t)
{
    for (u32 irq = 0; irq < 16; irq++) {
        if (t->irqs_pendentes & (1u << irq)) {
            msg_t m;
            mem_zera(&m, sizeof(m));
            m.tipo   = MSG_IRQ;
            m.arg[0] = irq;
            t->irqs_pendentes &= ~(1u << irq);
            entregar(t, &m, PID_KERNEL);
            return 1;
        }
    }
    return 0;
}

static void ipc_receber(u32 origem)
{
    tarefa_t *eu = &tarefas[atual];

    eu->estado = ESTADO_RECEBENDO;
    eu->alvo   = origem;

    if ((origem == PID_QUALQUER || origem == PID_KERNEL) && ipc_entregar_irq(eu))
        return;

    /* Procura, na ordem de chegada, alguém tentando enviar para mim. */
    tarefa_t *escolhido = 0;
    u32 pid_escolhido = 0;
    for (u32 pid = 1; pid < MAX_TAREFAS; pid++) {
        tarefa_t *s = &tarefas[pid];
        if (s->estado == ESTADO_ENVIANDO && s->alvo == atual &&
            (origem == PID_QUALQUER || origem == pid) &&
            (!escolhido || s->ordem < escolhido->ordem)) {
            escolhido = s;
            pid_escolhido = pid;
        }
    }
    if (escolhido) {
        entregar(eu, &escolhido->caixa, pid_escolhido);
        envio_concluido(escolhido, atual);
    }
    /* senão: continua RECEBENDO, e o escalonador escolhe outro */
}

/* ===========================================================================
 *  Supervisão: o que acontece quando um processo do usuário falha
 *
 *  Num núcleo monolítico, um bug no driver derruba o sistema inteiro.
 *  Aqui, o núcleo encerra só o processo culpado, libera a memória dele e
 *  o carrega de novo, avisando-o (EBX = reinícios, ECX = exceção).
 * =========================================================================== */

static void processo_encerrar(u32 pid, int reiniciar, u32 motivo)
{
    tarefa_t *t = &tarefas[pid];

    if (cr3_atual == (u32)t->diretorio) {   /* nunca destrua o CR3 em uso */
        cr3_atual = (u32)diretorio_nucleo;
        __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3_atual) : "memory");
    }
    espaco_destruir(t->diretorio);
    t->diretorio = 0;
    t->estado = ESTADO_MORTO;

    if (t->irqs) {                          /* ninguém mais atende essas IRQs */
        pic_mascara |= (u16)t->irqs;
        pic_aplicar_mascara();
    }

    /* Quem estava esperando por ele não pode ficar preso para sempre. */
    for (u32 i = 1; i < MAX_TAREFAS; i++) {
        tarefa_t *o = &tarefas[i];
        if ((o->estado == ESTADO_ENVIANDO || o->estado == ESTADO_RECEBENDO) && o->alvo == pid) {
            o->chamando = 0;
            acordar(o, ERRO_PID);
        }
    }

    if (reiniciar && t->reinicios < MAX_REINICIOS) {
        t->reinicios++;
        if (!processo_carregar(pid, t->reinicios, motivo))
            t->estado = ESTADO_MORTO;
    }
}

/* ===========================================================================
 *  Chamadas de sistema (int 0x80)
 * =========================================================================== */

static int syscall_executar(quadro_t *q)
{
    tarefa_t *eu = &tarefas[atual];
    u32 a = q->ebx, b = q->ecx;

    switch (q->eax) {
    case SYS_ENVIAR:
    case SYS_CHAMAR:
        if (!pid_vivo(a) || a == atual) return ERRO_PID;
        if (!ponteiro_ok(b))            return ERRO_INVALIDO;
        mem_copia(&eu->caixa, (const void *)b, sizeof(msg_t));
        eu->msg_usuario = b;               /* a resposta do CHAMAR volta aqui */
        eu->chamando    = (q->eax == SYS_CHAMAR);
        ipc_enviar(a);
        return 0;

    case SYS_RECEBER:
        if (!ponteiro_ok(b)) return ERRO_INVALIDO;
        if (a != PID_QUALQUER && a != PID_KERNEL && !pid_vivo(a)) return ERRO_PID;
        eu->msg_usuario = b;
        ipc_receber(a);
        return 0;

    case SYS_RESPONDER:
        /* Nunca bloqueia: um servidor não pode ficar refém de um cliente. */
        if (!pid_vivo(a) || !ponteiro_ok(b)) return ERRO_PID;
        if (tarefas[a].estado != ESTADO_RECEBENDO || tarefas[a].alvo != atual)
            return ERRO_INVALIDO;          /* o cliente não está esperando por mim */
        mem_copia(&eu->caixa, (const void *)b, sizeof(msg_t));
        entregar(&tarefas[a], &eu->caixa, atual);
        return 0;

    case SYS_CEDER:
        return 0;

    case SYS_IRQ_ASSINAR:
        if (!(eu->flags & T_DRIVER)) return ERRO_SEM_PERMISSAO;
        if (a < 1 || a > 15)          return ERRO_INVALIDO;   /* a IRQ 0 é do núcleo */
        eu->irqs |= 1u << a;
        pic_liberar_irq(a);
        return 0;

    case SYS_TEMPO:
        return (int)ticks;

    case SYS_TAREFA_INFO: {
        if (a >= MAX_TAREFAS) return ERRO_PID;
        if (!ponteiro_ok(b))  return ERRO_INVALIDO;
        msg_t *m = (msg_t *)b;
        mem_zera(m, sizeof(*m));
        m->arg[0] = tarefas[a].estado;
        m->arg[1] = tarefas[a].reinicios;
        nome_copia(m->texto, tarefas[a].nome);
        return 0;
    }

    case SYS_MEMORIA:
        return (int)((quadros_livres << 16) | (quadros_total & 0xFFFF));

    case SYS_SAIR:
        processo_encerrar(atual, 0, 0);
        return 0;
    }
    return ERRO_INVALIDO;
}

/* ===========================================================================
 *  Pânico (só para falhas DO NÚCLEO; as do usuário são supervisionadas)
 * =========================================================================== */

static const char *const EXC_NOMES[22] = {
    "Divisao por zero", "Debug", "NMI", "Breakpoint", "Overflow",
    "Limite excedido", "Opcode invalido", "FPU indisponivel", "Falta dupla",
    "Segmento do coprocessador", "TSS invalido", "Segmento ausente",
    "Falha de pilha", "Protecao geral (GPF)", "Falha de pagina", "Reservado",
    "Erro x87", "Alinhamento", "Machine check", "Erro SIMD",
    "Virtualizacao", "Protecao de fluxo"
};

static void kernel_panico(const quadro_t *q) __attribute__((noreturn));
static void kernel_panico(const quadro_t *q)
{
    u32 cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

    const u8 a = ATTR(VGA_BRANCO, VGA_VERMELHO);
    for (u32 i = 0; i < VGA_COLUNAS * 3; i++)
        VGA_MEMORIA[i] = vga_celula(' ', a);

    u32 c = vga_print(0, 1, "*** PANICO DO NUCLEO: ", a);
    vga_print(0, c, q->vetor < 22 ? EXC_NOMES[q->vetor] : "Reservado", a);

    c = vga_print(1, 1, "VETOR=", a);   c = vga_hex(1, c, q->vetor, a);
    c = vga_print(1, c, "  ERRO=", a);  c = vga_hex(1, c, q->erro, a);
    c = vga_print(1, c, "  CR2=", a);   vga_hex(1, c, cr2, a);

    c = vga_print(2, 1, "EIP=", a);     c = vga_hex(2, c, q->eip, a);
    c = vga_print(2, c, " CS=", a);     c = vga_hex(2, c, q->cs, a);
    c = vga_print(2, c, " EFLAGS=", a); vga_hex(2, c, q->eflags, a);

    for (;;)
        __asm__ volatile ("cli; hlt");
}

/* ===========================================================================
 *  Despacho de interrupções (chamado por isr_comum em boot.asm)
 *
 *  Recebe o quadro de quem foi interrompido e DEVOLVE o quadro de quem
 *  deve rodar agora. É aqui que mora a troca de contexto.
 * =========================================================================== */

quadro_t *interrupt_dispatch(quadro_t *q)
{
    tarefas[atual].quadro = q;
    int trocar = 0;

    if (q->vetor < 32) {
        if ((q->cs & 3) == 0)
            kernel_panico(q);                    /* o núcleo falhou: fim */
        processo_encerrar(atual, 1, q->vetor);  /* o usuário falhou: isola */
    } else if (q->vetor < 48) {
        u32 irq = q->vetor - 32;
        if (irq == 0) {
            ticks++;
            trocar = 1;                          /* fatia de tempo esgotada */
        } else {
            for (u32 pid = 1; pid < MAX_TAREFAS; pid++) {
                tarefa_t *t = &tarefas[pid];
                if (!(t->irqs & (1u << irq))) continue;
                t->irqs_pendentes |= 1u << irq;
                if (esperando_por(t, PID_KERNEL))
                    ipc_entregar_irq(t);
            }
        }
        pic_eoi(irq);
    } else if (q->vetor == 0x80) {
        u32 numero = q->eax;
        int r = syscall_executar(q);
        if (tarefas[atual].estado == ESTADO_PRONTO)
            q->eax = (u32)r;                     /* bloqueado: recebe depois */
        if (numero == SYS_CEDER)
            trocar = 1;
    }

    if (trocar || tarefas[atual].estado != ESTADO_PRONTO)
        escalonar();

    tarefa_t *t = &tarefas[atual];
    if ((u32)t->diretorio != cr3_atual) {
        cr3_atual = (u32)t->diretorio;
        __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3_atual) : "memory");
    }
    if (t->pilha_nucleo)
        tss.esp0 = (u32)t->pilha_nucleo + PILHA_NUCLEO_TAM;

    if (t->entregar) {                           /* última etapa do IPC */
        mem_copia((void *)t->msg_usuario, &t->caixa, sizeof(msg_t));
        t->entregar = 0;
    }
    return t->quadro;
}

/* ------------------------------- Mensagem ------------------------------- */

static const char MSG_NOME[] = "NovaEra Microkernel v2.0";
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
    vga_ativar_cursor();
    vga_limpar(ATTR_FUNDO);
    banner();
    t_lin = 4; t_col = 0;

    term_status(1, "Modo protegido 32 bits (GDT com aneis 0 e 3)");
    term_status(magic == MULTIBOOT_MAGIC_BOOTLOADER, "Bootloader Multiboot detectado");

    memoria_iniciar(magic == MULTIBOOT_MAGIC_BOOTLOADER ? mbi_endereco : 0);
    term_escreve("[",    ATTR(VGA_CINZA_CLARO, FUNDO));
    term_escreve(" OK ", ATTR(VGA_VERDE_CLARO, FUNDO));
    term_escreve("] Memoria fisica: ", ATTR(VGA_CINZA_CLARO, FUNDO));
    term_numero(quadros_livres * 4, ATTR(VGA_BRANCO, FUNDO));
    term_escreve(" KiB livres em quadros de 4 KiB\n", ATTR(VGA_BRANCO, FUNDO));

    paginacao_iniciar();
    term_status(1, "Paginacao ligada: nucleo protegido do anel 3");

    tss_iniciar();
    idt_iniciar();
    term_status(1, "IDT: 32 excecoes + 16 IRQs + int 0x80 (syscalls)");

    pic_iniciar();
    timer_iniciar();
    term_status(1, "Timer PIT a 100 Hz: escalonador preemptivo");

    ocioso_iniciar();
    int ok = processo_criar(PID_VIDEO,   "video",   prog_video_ini,   prog_video_fim,   T_DRIVER | T_VGA)
           & processo_criar(PID_TECLADO, "teclado", prog_teclado_ini, prog_teclado_fim, T_DRIVER)
           & processo_criar(PID_SHELL,   "shell",   prog_shell_ini,   prog_shell_fim,   0);
    term_status(ok, "Servidores no anel 3: video, teclado, shell");

    term_escreve("\nEntregando a CPU aos processos do usuario...\n\n", ATTR(VGA_CINZA_CLARO, FUNDO));
    vga_mover_cursor(t_lin, t_col);         /* o servidor de vídeo continua daqui */

    /* A partir daqui este código é o processo 0 ("nucleo", ocioso).
     * A próxima IRQ do timer chama o escalonador, e a festa começa. */
    __asm__ volatile ("sti");
    for (;;)
        __asm__ volatile ("hlt");
}
