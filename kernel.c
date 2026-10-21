/*
 * ===========================================================================
 *  NovaEra Microkernel v3.0 (x86_64) - kernel.c
 *  C puro "freestanding": sem libc. Os #include são a ABI do sistema e
 *  os arquivos que o build.sh gera de sistema.conf e programas.lista.
 *  Quando kernel_main() começa, boot.asm já colocou a CPU em 64 bits.
 *
 *  Este arquivo não tem nenhum assembly embutido (__asm__) nem atributos
 *  de compilador: as instruções especiais ficam em cpu.asm. Assim ele
 *  compila com o clang e também com o compilador do NEFS (cproc + QBE).
 *
 *  O que mora AQUI (anel 0, o mínimo possível):
 *    - Gerenciador de memória física (lista de quadros de 4 KiB livres)
 *    - Paginação de 4 níveis: cada processo tem o seu próprio espaço
 *    - Processos, TSS e troca de contexto
 *    - Escalonador preemptivo (timer PIT a 100 Hz, round-robin)
 *    - Programas ELF (os do boot vêm no kernel.bin; os outros chegam pela
 *      memória de quem os cria), com argumentos, heap, pilha sob demanda
 *      e código de saída
 *    - Cópia de dados entre um servidor e o cliente que espera por ele
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
#include "sistema.h"      /* gerado de sistema.conf pelo build.sh */

#define VGA_MEMORIA 0xB8000ull   /* acessada por mem_escreve16 (cpu.asm) */
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

/* --------------------- Instruções especiais (cpu.asm) -------------------- */

void outb(u16 porta, u8 valor);
u8   inb(u16 porta);
void mem_escreve16(u64 endereco, u16 valor);
void cpu_carregar_cr3(u64 pml4);
u64  cpu_ler_cr2(void);
void cpu_carregar_idt(u64 base, u16 limite);
void cpu_carregar_tr(u16 seletor);
_Noreturn void cpu_ocioso(void);
_Noreturn void cpu_parar(void);

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
        mem_escreve16(VGA_MEMORIA + 2 * i, vga_celula(' ', atributo));
}

static void vga_putc(u32 linha, u32 coluna, char c, u8 atributo)
{
    if (linha >= VGA_LINHAS || coluna >= VGA_COLUNAS) return;
    mem_escreve16(VGA_MEMORIA + 2 * (linha * VGA_COLUNAS + coluna), vga_celula(c, atributo));
}

/* Escreve caractere por caractere; devolve a próxima coluna livre. */
static u32 vga_print(u32 linha, u32 coluna, const char *s, u8 atributo)
{
    for (u32 i = 0; s[i]; i++)
        vga_putc(linha, coluna + i, s[i], atributo);
    return coluna + tamanho_str(s);
}

static u32 vga_hex(u32 linha, u32 coluna, u64 valor, u8 atributo)
{
    static const char HEX[] = "0123456789ABCDEF";
    coluna = vga_print(linha, coluna, "0x", atributo);
    for (int i = 60; i >= 0; i -= 4)
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
 *  O núcleo enxerga o 1º GiB (boot.asm), então só gerencia quadros ali.
 * =========================================================================== */

#define QUADRO          4096u
#define MEMORIA_NUCLEO  0x40000000ull  /* 1 GiB mapeado no núcleo */

extern u8 _kernel_fim[];               /* definido em linker.ld */

static u64 quadros_livres_lista;       /* endereço físico do 1º quadro livre */
static u64 quadros_livres;
static u64 quadros_total;

static void quadro_liberar(u64 fisico)
{
    *(u64 *)fisico = quadros_livres_lista;
    quadros_livres_lista = fisico;
    quadros_livres++;
}

/* Devolve um quadro zerado, ou 0 se a memória acabou. */
static u64 quadro_alocar(void)
{
    u64 q = quadros_livres_lista;
    if (!q) return 0;
    quadros_livres_lista = *(u64 *)q;
    quadros_livres--;
    mem_zera((void *)q, QUADRO);
    return q;
}

/* O initrd: um arquivo .tar que o GRUB ou o QEMU (-initrd) carregam na
 * memória junto com o núcleo, como "módulo" Multiboot. O núcleo não lê o
 * tar: só o entrega, mapeado, ao servidor de arquivos. */
static const u8 *initrd;
static u64       initrd_tam;

/* Multiboot: se o bit 0 das flags estiver ligado, mem_upper diz quantos
 * KiB de RAM existem acima de 1 MiB; o bit 3 diz que há módulos. */
static void memoria_iniciar(u64 mbi_endereco)
{
    u64 fim = MEMORIA_NUCLEO;
    u64 ocupado = (u64)_kernel_fim;          /* nada abaixo disto é livre */
    if (mbi_endereco) {
        const u32 *mbi = (const u32 *)mbi_endereco;
        if (mbi[0] & 1) {
            u64 real = 0x100000ull + (u64)mbi[2] * 1024u;
            if (real < fim) fim = real;
        }
        if ((mbi[0] & 8) && mbi[5] > 0) {
            /* Cada módulo: [início, fim, texto, reservado]. Usamos o 1º. */
            const u32 *mod = (const u32 *)(u64)mbi[6];
            initrd     = (const u8 *)(u64)mod[0];
            initrd_tam = (u64)(mod[1] - mod[0]);
            for (u32 i = 0; i < mbi[5]; i++)
                if ((u64)mod[4 * i + 1] > ocupado)
                    ocupado = mod[4 * i + 1];
        }
    }

    u64 ini = (ocupado + QUADRO - 1) & ~(u64)(QUADRO - 1);
    for (u64 q = fim - QUADRO; q >= ini; q -= QUADRO) {   /* do fim para o início: */
        quadro_liberar(q);                                /* o 1º alocado é o mais baixo */
        quadros_total++;
    }
}

/* ===========================================================================
 *  Paginação de 4 níveis (x86_64)
 *
 *  Endereço virtual = [9 PML4][9 PDPT][9 PD][9 PT][12 offset] = 48 bits.
 *  Cada nível é uma tabela de 512 entradas de 8 bytes (um quadro).
 *
 *  O 1º GiB é do núcleo: boot.asm já montou PML4 -> PDPT -> PD com páginas
 *  de 2 MiB. Todo processo reaproveita esse MESMO PD na entrada 0 do seu
 *  PDPT, marcado como "supervisor": o anel 3 não consegue nem ler.
 *  Os programas moram a partir de 0x40000000 (entrada 1 do PDPT).
 * =========================================================================== */

#define PG_PRESENTE  0x1ull
#define PG_ESCRITA   0x2ull
#define PG_USUARIO   0x4ull
#define PG_ENDERECO  0x000FFFFFFFFFF000ull

extern u64 pml4_boot[512];             /* boot.asm */
extern u64 pd_boot[512];

static u64 *tabela_de(u64 entrada)
{
    return (u64 *)(entrada & PG_ENDERECO);
}

/* Cria um espaço de endereçamento novo que já enxerga o núcleo (anel 0). */
static u64 *espaco_criar(void)
{
    u64 *pml4 = (u64 *)quadro_alocar();
    u64 *pdpt = (u64 *)quadro_alocar();
    if (!pml4 || !pdpt) return 0;
    pml4[0] = (u64)pdpt | PG_PRESENTE | PG_ESCRITA | PG_USUARIO;
    pdpt[0] = (u64)pd_boot | PG_PRESENTE | PG_ESCRITA;      /* núcleo: sem U */
    return pml4;
}

/* Desce um nível da árvore, criando a tabela se ela não existir. */
static u64 *descer(u64 *tabela, u32 indice)
{
    if (!(tabela[indice] & PG_PRESENTE)) {
        u64 nova = quadro_alocar();
        if (!nova) return 0;
        tabela[indice] = nova | PG_PRESENTE | PG_ESCRITA | PG_USUARIO;
    }
    return tabela_de(tabela[indice]);
}

static int mapear(u64 *pml4, u64 virtual, u64 fisico, u64 flags)
{
    u64 *pdpt = descer(pml4, (virtual >> 39) & 511);
    u64 *pd   = pdpt ? descer(pdpt, (virtual >> 30) & 511) : 0;
    u64 *pt   = pd   ? descer(pd,   (virtual >> 21) & 511) : 0;
    if (!pt) return 0;
    pt[(virtual >> 12) & 511] = fisico | flags;
    return 1;
}

/* Endereço físico por trás de um endereço virtual, ou 0 se não houver
 * página presente e acessível ao anel 3. */
static u64 traduzir(u64 *pml4, u64 virtual)
{
    u64 *tabela = pml4;
    for (int nivel = 39; nivel >= 12; nivel -= 9) {
        u64 e = tabela[(virtual >> nivel) & 511];
        if (!(e & PG_PRESENTE) || !(e & PG_USUARIO)) return 0;
        if (nivel == 12) return (e & PG_ENDERECO) | (virtual & 0xFFF);
        tabela = tabela_de(e);
    }
    return 0;
}

/* Devolve à lista livre tudo o que pertence só ao processo. */
static void espaco_destruir(u64 *pml4)
{
    for (u32 i = 0; i < 512; i++) {
        if (!(pml4[i] & PG_PRESENTE)) continue;
        u64 *pdpt = tabela_de(pml4[i]);
        for (u32 j = 0; j < 512; j++) {
            if (!(pdpt[j] & PG_PRESENTE)) continue;
            if (i == 0 && j == 0) continue;          /* o PD do núcleo é de todos */
            u64 *pd = tabela_de(pdpt[j]);
            for (u32 k = 0; k < 512; k++) {
                if (!(pd[k] & PG_PRESENTE)) continue;
                u64 *pt = tabela_de(pd[k]);
                for (u32 l = 0; l < 512; l++) {
                    u64 fisico = pt[l] & PG_ENDERECO;
                    if ((pt[l] & PG_PRESENTE) && fisico >= (u64)_kernel_fim &&
                        !(fisico >= ((u64)initrd & ~0xFFFull) && fisico < (u64)initrd + initrd_tam))
                        quadro_liberar(fisico);      /* a VGA e o initrd não são do processo */
                }
                quadro_liberar((u64)pt);
            }
            quadro_liberar((u64)pd);
        }
        quadro_liberar((u64)pdpt);
    }
    quadro_liberar((u64)pml4);
}

/* ===========================================================================
 *  GDT/TSS
 *
 *  Quando uma interrupção chega com a CPU no anel 3, ela precisa trocar
 *  para uma pilha do anel 0. Onde fica essa pilha? No campo rsp0 da TSS,
 *  que atualizamos a cada troca de processo.
 * =========================================================================== */

/* Em 64 bits a TSS não guarda mais registradores: só as pilhas.
 * São 104 bytes, mas o campo rsp0 começa no byte 4 (fora do alinhamento
 * de um u64). Por isso ela é um vetor de u32, sem struct "packed":
 *   [0] reservado  [1..2] rsp0  [3..6] rsp1, rsp2  [7..8] reservado
 *   [9..22] ist1..7  [23..24] reservado  [25] iomap nos 16 bits de cima */
#define TSS_TAM 104u
static u32 tss[TSS_TAM / 4];
extern u8 gdt_tss[16];                 /* entrada reservada em boot.asm */

static void tss_definir_rsp0(u64 rsp0)
{
    tss[1] = (u32)rsp0;
    tss[2] = (u32)(rsp0 >> 32);
}

static void tss_iniciar(void)
{
    u64 base = (u64)tss;
    u32 limite = TSS_TAM - 1;
    tss[25] = TSS_TAM << 16;           /* sem mapa de portas: vale o IOPL */

    gdt_tss[0] = (u8)limite;
    gdt_tss[1] = (u8)(limite >> 8);
    gdt_tss[2] = (u8)base;
    gdt_tss[3] = (u8)(base >> 8);
    gdt_tss[4] = (u8)(base >> 16);
    gdt_tss[5] = 0x89;                 /* presente, anel 0, TSS livre */
    gdt_tss[6] = (u8)((limite >> 16) & 0x0F);
    gdt_tss[7] = (u8)(base >> 24);
    for (u32 i = 0; i < 4; i++)        /* 2ª metade: bits 32-63 da base */
        gdt_tss[8 + i] = (u8)(base >> (32 + 8 * i));

    cpu_carregar_tr(SEL_TSS);
}

/* ===========================================================================
 *  IDT - Interrupt Descriptor Table
 * =========================================================================== */

/* 16 bytes. Os campos já caem alinhados, então não há preenchimento. */
typedef struct {
    u16 base_baixo;     /* bits 0-15 do endereço do handler  */
    u16 seletor;        /* seletor de código na GDT (0x08)   */
    u8  ist;
    u8  flags;          /* 0x8E = anel 0 pode chamar; 0xEE = anel 3 também */
    u16 base_meio;      /* bits 16-31 */
    u32 base_alto;      /* bits 32-63 */
    u32 reservado;
} idt_entrada_t;

/* Quadro empilhado por isr_comum (boot.asm). É ELE que representa um
 * processo parado: para retomá-lo, basta apontar RSP para cá e dar IRETQ. */
typedef struct {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;            /* isr_comum   */
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 vetor, erro;                                     /* stubs       */
    u64 rip, cs, rflags, rsp, ss;                        /* empilhado pela CPU */
} quadro_t;

extern const u64 isr_tabela[];     /* definida em boot.asm: 48 endereços */
extern void isr_syscall(void);

static idt_entrada_t idt[256];

static void idt_definir(u8 vetor, u64 handler, u16 seletor, u8 flags)
{
    idt[vetor].base_baixo = (u16)handler;
    idt[vetor].seletor    = seletor;
    idt[vetor].ist        = 0;
    idt[vetor].flags      = flags;
    idt[vetor].base_meio  = (u16)(handler >> 16);
    idt[vetor].base_alto  = (u32)(handler >> 32);
    idt[vetor].reservado  = 0;
}

static void idt_iniciar(void)
{
    for (u32 i = 0; i < 256; i++)
        idt_definir((u8)i, 0, 0, 0);                    /* não presente */

    for (u32 i = 0; i < 48; i++)
        idt_definir((u8)i, isr_tabela[i], SEL_CODIGO_NUCLEO, 0x8E);

    /* A ÚNICA porta que o anel 3 pode abrir: as chamadas de sistema. */
    idt_definir(0x80, (u64)isr_syscall, SEL_CODIGO_NUCLEO, 0xEE);

    cpu_carregar_idt((u64)idt, (u16)(sizeof(idt) - 1));
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

#define MAX_TAREFAS       32u
#define PILHA_NUCLEO_TAM  QUADRO
#define MAX_REINICIOS     5u

#define T_DRIVER     0x1u      /* pode usar IN/OUT (IOPL 3) e assinar IRQs */
#define T_VGA        0x2u      /* recebe a memória de vídeo mapeada */
#define T_REINICIAR  0x4u      /* servidor do boot: recarregado se falhar */
#define T_INITRD     0x8u      /* recebe o initrd mapeado (só leitura) */

/* Uma linha de programas.lista (a tabela PROGRAMAS vem de build/programas.h) */
typedef struct {
    const char *nome;
    const u8   *ini, *fim;
    u32         flags;
} programa_info_t;

typedef struct {
    u32        estado;         /* ESTADO_* (novaera.h) */
    char       nome[12];
    u32        flags;
    const u8  *imagem;         /* o ELF: no kernel.bin (ou emprestado, em SYS_CRIAR) */
    u64        imagem_tam;
    u32        pai;            /* quem o criou (0 = o núcleo, no boot) */
    u32        reinicios;
    u32        ultima_falha;   /* exceção que causou o último encerramento */
    long       codigo_saida;   /* guardado até o pai chamar SYS_ESPERAR */

    /* Entrada e saída padrão (servidor + canal) e a pasta atual: só o
     * núcleo guarda, para os filhos herdarem; quem usa é a ulib. */
    u32        entrada, entrada_canal, saida, saida_canal;
    char       pasta[CAMINHO_MAX];

    u64       *diretorio;      /* PML4: o espaço de endereçamento (CR3) */
    u8        *pilha_nucleo;   /* onde o quadro_t fica enquanto está parado */
    quadro_t  *quadro;
    u64        heap_ini, heap_fim;   /* o heap cresce com SYS_SBRK */
    u64        pilha_baixo;    /* página mais baixa da pilha já mapeada */

    /* IPC */
    u32        alvo;           /* para quem envia / de quem quer receber / filho esperado */
    u32        chamando;       /* SYS_CHAMAR: depois de enviar, espera resposta */
    u32        ordem;          /* ordem de chegada na fila do destino */
    u64        msg_usuario;    /* endereço da msg_t no espaço do processo */
    msg_t      caixa;          /* cópia da mensagem dentro do núcleo */
    u32        entregar;       /* 1 = copiar a caixa para o usuário ao retomar */
    u32        irqs;           /* IRQs assinadas (bits) */
    u32        irqs_pendentes;
} tarefa_t;

static tarefa_t tarefas[MAX_TAREFAS];
static u32      atual;                 /* índice (= PID) de quem está rodando */
static u64      cr3_atual;
static u32      contador_ordem;
static u32      ticks;          /* só muda e é lida no núcleo, com IF=0 */

#include "programas.h"     /* gerado de programas.lista pelo build.sh */

/* Guarda o nome curto do processo: o que vem depois da última '/'. */
static void nome_copia(char *dst, const char *src)
{
    for (const char *p = src; *p; p++)
        if (*p == '/') src = p + 1;
    u32 i = 0;
    for (; src[i] && i < 11; i++) dst[i] = src[i];
    dst[i] = '\0';
}

/* --------------------------- Carregador ELF ------------------------------
 * Um executável ELF de 64 bits diz, no cabeçalho, onde começa (e_entry) e
 * quais pedaços do arquivo vão para quais endereços (os segmentos
 * PT_LOAD). Os campos são lidos byte a byte: sem structs "packed".       */

#define ELF_PT_LOAD   1u
#define ELF_X86_64   62u

static u64 le16(const u8 *p) { return (u64)p[0] | (u64)p[1] << 8; }
static u64 le32(const u8 *p) { return le16(p) | le16(p + 2) << 16; }
static u64 le64(const u8 *p) { return le32(p) | le32(p + 4) << 32; }

/* Mapeia páginas novas (zeradas) cobrindo [ini, fim), menos as que já existem. */
static int mapear_faixa(u64 *pml4, u64 ini, u64 fim)
{
    for (u64 v = ini & ~0xFFFull; v < fim; v += QUADRO) {
        if (traduzir(pml4, v)) continue;
        u64 q = quadro_alocar();
        if (!q || !mapear(pml4, v, q, PG_PRESENTE | PG_ESCRITA | PG_USUARIO))
            return 0;
    }
    return 1;
}

/* Copia bytes para dentro de OUTRO espaço de endereçamento, página a
 * página, pelo endereço físico (o núcleo enxerga toda a RAM). */
static int copiar_para_espaco(u64 *pml4, u64 destino, const void *origem, u64 n)
{
    const u8 *o = origem;
    while (n) {
        u64 fisico = traduzir(pml4, destino);
        if (!fisico) return 0;
        u64 cabe = QUADRO - (destino & 0xFFF);
        if (cabe > n) cabe = n;
        mem_copia((void *)fisico, o, cabe);
        destino += cabe;
        o       += cabe;
        n       -= cabe;
    }
    return 1;
}

/* Carrega o ELF do processo no seu espaço; devolve o ponto de entrada (0 = erro). */
static u64 elf_carregar(tarefa_t *t)
{
    const u8 *e = t->imagem;
    u64 tam = t->imagem_tam;
    if (tam < 64 || e[0] != 0x7F || e[1] != 'E' || e[2] != 'L' || e[3] != 'F' ||
        e[4] != 2 || e[5] != 1 || le16(e + 18) != ELF_X86_64)
        return 0;                                    /* não é ELF64 x86_64 */

    u64 phoff = le64(e + 32), phent = le16(e + 54), phnum = le16(e + 56);
    if (phent < 56 || phoff > tam || phent * phnum > tam - phoff) return 0;

    u64 fim_prog = USUARIO_BASE;
    for (u64 i = 0; i < phnum; i++) {
        const u8 *ph = e + phoff + i * phent;
        if (le32(ph) != ELF_PT_LOAD) continue;
        u64 off = le64(ph + 8), va = le64(ph + 16);
        u64 no_arquivo = le64(ph + 32), na_memoria = le64(ph + 40);
        if (va < USUARIO_BASE || va >= USUARIO_PROG_FIM ||
            na_memoria > USUARIO_PROG_FIM - va || no_arquivo > na_memoria ||
            off > tam || no_arquivo > tam - off)
            return 0;                                /* fora da área do programa */
        if (!mapear_faixa(t->diretorio, va, va + na_memoria) ||
            !copiar_para_espaco(t->diretorio, va, e + off, no_arquivo))
            return 0;                                /* o resto (.bss) fica zerado */
        if (va + na_memoria > fim_prog) fim_prog = va + na_memoria;
    }

    u64 entrada = le64(e + 24);
    if (entrada < USUARIO_BASE || entrada >= fim_prog) return 0;
    t->heap_ini = t->heap_fim = (fim_prog + QUADRO - 1) & ~(u64)(QUADRO - 1);
    return entrada;
}

/* ------------------------- Argumentos (argc, argv) -----------------------
 * Ficam no topo da pilha do novo processo: primeiro os textos, depois o
 * vetor de ponteiros terminado em 0. _start recebe RDI = argc, RSI = argv. */

typedef struct {
    u32  argc;
    u32  bytes;                       /* quanto de 'texto' está em uso */
    char texto[ARGV_BYTES];           /* "ola\0mundo\0..." */
} args_t;

static args_t args_tmp;               /* o núcleo roda com IF=0: um só basta */

static int args_por(args_t *a, const char *texto)
{
    u32 n = tamanho_str(texto) + 1;
    if (a->argc >= ARGV_MAX || n > ARGV_BYTES - a->bytes) return 0;
    mem_copia(a->texto + a->bytes, texto, n);
    a->bytes += n;
    a->argc++;
    return 1;
}

static int pilha_preparar(tarefa_t *t, const args_t *a, u64 *rsp, u64 *argv)
{
    u64 topo = USUARIO_PILHA_TOPO;
    if (!mapear_faixa(t->diretorio, topo - QUADRO, topo)) return 0;
    t->pilha_baixo = topo - QUADRO;

    u64 textos = (topo - a->bytes) & ~7ull;
    if (!copiar_para_espaco(t->diretorio, textos, a->texto, a->bytes)) return 0;

    u64 ptrs[ARGV_MAX + 1];
    u32 pos = 0;
    for (u32 i = 0; i < a->argc; i++) {
        ptrs[i] = textos + pos;
        pos += tamanho_str(a->texto + pos) + 1;
    }
    ptrs[a->argc] = 0;

    u64 vetor = (textos - (a->argc + 1) * 8) & ~15ull;   /* alinhado em 16 */
    if (!copiar_para_espaco(t->diretorio, vetor, ptrs, (a->argc + 1) * 8)) return 0;
    *argv = vetor;
    *rsp  = vetor;
    return 1;
}

/* A pilha cresce sozinha: um acesso logo abaixo dela ganha páginas novas. */
static int pilha_crescer(tarefa_t *t, u64 endereco)
{
    if (!t->diretorio || endereco < USUARIO_PILHA_TOPO - USUARIO_PILHA_TAM ||
        endereco >= t->pilha_baixo)
        return 0;
    u64 pagina = endereco & ~0xFFFull;
    if (!mapear_faixa(t->diretorio, pagina, t->pilha_baixo)) return 0;
    t->pilha_baixo = pagina;
    return 1;
}

/*
 * Prepara um processo para rodar do zero: espaço de endereçamento novo,
 * o ELF carregado, a pilha com os argumentos e um quadro_t "falso" que,
 * quando retomado com IRETQ, cai no anel 3 no ponto de entrada do programa.
 */
static long processo_carregar(u32 pid, const args_t *a)
{
    tarefa_t *t = &tarefas[pid];

    t->diretorio = espaco_criar();
    if (!t->diretorio) return ERRO_SEM_MEMORIA;

    u64 entrada = elf_carregar(t), rsp = 0, argv = 0;
    long erro = entrada ? 0 : ERRO_FORMATO;
    if (!erro && !pilha_preparar(t, a, &rsp, &argv)) erro = ERRO_SEM_MEMORIA;
    if (!erro && (t->flags & T_VGA) &&
        !mapear(t->diretorio, USUARIO_VGA, 0xB8000, PG_PRESENTE | PG_ESCRITA | PG_USUARIO))
        erro = ERRO_SEM_MEMORIA;
    /* O initrd vai inteiro para o servidor de arquivos, só para leitura.
     * Ele pode não começar no início de uma página: SYS_INITRD diz onde. */
    u64 initrd_pag = (u64)initrd & ~0xFFFull;
    for (u64 f = initrd_pag; !erro && (t->flags & T_INITRD) && f < (u64)initrd + initrd_tam;
         f += QUADRO)
        if (!mapear(t->diretorio, USUARIO_INITRD + (f - initrd_pag), f, PG_PRESENTE | PG_USUARIO))
            erro = ERRO_SEM_MEMORIA;

    /* A pilha do núcleo é do PID para sempre: assim um reinício pode
     * acontecer enquanto o núcleo ainda está rodando sobre ela. */
    if (!erro && !t->pilha_nucleo) {
        t->pilha_nucleo = (u8 *)quadro_alocar();
        if (!t->pilha_nucleo) erro = ERRO_SEM_MEMORIA;
    }
    if (erro) {
        espaco_destruir(t->diretorio);
        t->diretorio = 0;
        return erro;
    }

    quadro_t *q = (quadro_t *)(t->pilha_nucleo + PILHA_NUCLEO_TAM - sizeof(quadro_t));
    mem_zera(q, sizeof(*q));
    q->rdi    = a->argc;
    q->rsi    = argv;
    q->rip    = entrada;
    q->cs     = SEL_CODIGO_USUARIO;
    q->rflags = 0x202 | ((t->flags & T_DRIVER) ? 0x3000u : 0);  /* IF + IOPL */
    q->rsp    = rsp;
    q->ss     = SEL_DADOS_USUARIO;

    t->quadro         = q;
    t->estado         = ESTADO_PRONTO;
    t->chamando       = 0;
    t->entregar       = 0;
    t->irqs           = 0;
    t->irqs_pendentes = 0;
    return 0;
}

/* Um servidor do boot (programas.lista), embutido no kernel.bin. */
static int processo_criar(u32 pid, const char *nome, const u8 *ini, const u8 *fim, u32 flags)
{
    tarefa_t *t = &tarefas[pid];
    nome_copia(t->nome, nome);
    t->flags      = flags | T_REINICIAR;
    t->imagem     = ini;
    t->imagem_tam = (u64)(fim - ini);
    t->pai        = PID_KERNEL;
    t->entrada = t->saida = PID_TERMINAL;   /* o terminal é a tela e o teclado */
    t->pasta[0] = '/';

    mem_zera(&args_tmp, sizeof(args_tmp));
    args_por(&args_tmp, nome);
    return processo_carregar(pid, &args_tmp) == 0;
}

/* O PID 0 é o próprio núcleo: depois do boot, kernel_main() vira o
 * processo "ocioso", que só dorme (HLT) quando ninguém mais quer a CPU. */
static void ocioso_iniciar(void)
{
    nome_copia(tarefas[0].nome, "nucleo");
    tarefas[0].estado    = ESTADO_PRONTO;
    tarefas[0].diretorio = pml4_boot;
    cr3_atual = (u64)pml4_boot;
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
 *  Memória do usuário: o núcleo só toca num endereço que o processo passou
 *  depois de conferir que ele é do anel 3 e está mapeado. Sem isso, um
 *  ponteiro ruim numa chamada de sistema derrubaria o núcleo inteiro.
 * =========================================================================== */

static int usuario_valido(tarefa_t *t, u64 p, u64 n)
{
    if (p < USUARIO_BASE || p >= USUARIO_FIM || n > USUARIO_FIM - p) return 0;
    for (u64 v = p & ~0xFFFull; v < p + n; v += QUADRO)
        if (!traduzir(t->diretorio, v) && !pilha_crescer(t, v)) return 0;
    return 1;
}

/* Copia um texto do usuário (com o '\0'); devolve o tamanho ou -1. */
static long texto_do_usuario(tarefa_t *t, u64 p, char *dst, u32 max)
{
    for (u32 i = 0; i < max; i++, p++) {
        if ((i == 0 || (p & 0xFFF) == 0) && !usuario_valido(t, p, 1)) return -1;
        dst[i] = *(const char *)p;
        if (!dst[i]) return i;
    }
    return -1;
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

/* Existe e pode conversar? (livre, morto e zumbi não podem) */
static int pid_vivo(u64 pid)
{
    if (pid == 0 || pid >= MAX_TAREFAS) return 0;
    u32 e = tarefas[pid].estado;
    return e != ESTADO_LIVRE && e != ESTADO_MORTO && e != ESTADO_ZUMBI;
}

/* Acorda um processo bloqueado, devolvendo 'resultado' no seu RAX. */
static void acordar(tarefa_t *t, long resultado)
{
    t->estado      = ESTADO_PRONTO;
    t->quadro->rax = (u64)resultado;
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
 *  Fim de um processo e supervisão
 *
 *  Um programa termina chamando SYS_SAIR ou quando a CPU gera uma exceção
 *  nele. Num núcleo monolítico, um bug no driver derruba o sistema
 *  inteiro; aqui, o núcleo encerra só o processo culpado e libera a
 *  memória dele. Os servidores do boot são recarregados; os outros
 *  programas viram "zumbis" até o pai ler o código de saída.
 * =========================================================================== */

static void processo_encerrar(u32 pid, long codigo, int falhou, u64 vetor)
{
    tarefa_t *t = &tarefas[pid];

    if (cr3_atual == (u64)t->diretorio) {   /* nunca destrua o CR3 em uso */
        cr3_atual = (u64)pml4_boot;
        cpu_carregar_cr3(cr3_atual);
    }
    espaco_destruir(t->diretorio);
    t->diretorio = 0;
    t->estado = ESTADO_MORTO;
    if (falhou) t->ultima_falha = (u32)vetor;

    if (t->irqs) {                          /* ninguém mais atende essas IRQs */
        pic_mascara |= (u16)t->irqs;
        pic_aplicar_mascara();
    }

    /* Quem estava conversando com ele não pode ficar preso para sempre. */
    for (u32 i = 1; i < MAX_TAREFAS; i++) {
        tarefa_t *o = &tarefas[i];
        if ((o->estado == ESTADO_ENVIANDO || o->estado == ESTADO_RECEBENDO) && o->alvo == pid) {
            o->chamando = 0;
            acordar(o, ERRO_PID);
        }
    }

    /* Os filhos dele ficam órfãos; os zumbis já podem ir embora. (Antes
     * de um reinício: o processo recarregado não conhece esses filhos.) */
    for (u32 i = 1; i < MAX_TAREFAS; i++) {
        if (tarefas[i].pai != pid || i == pid) continue;
        if (tarefas[i].estado == ESTADO_ZUMBI) tarefas[i].estado = ESTADO_LIVRE;
        tarefas[i].pai = PID_KERNEL;
    }

    /* Servidor do boot que falhou: carrega de novo. */
    if ((t->flags & T_REINICIAR) && falhou && t->reinicios < MAX_REINICIOS) {
        t->reinicios++;
        mem_zera(&args_tmp, sizeof(args_tmp));
        args_por(&args_tmp, t->nome);
        if (processo_carregar(pid, &args_tmp) == 0) return;
    }
    if (t->flags & T_REINICIAR) return;     /* servidor desistiu: fica MORTO */

    /* O pai está esperando? Entrega o código. Senão, vira zumbi. */
    t->codigo_saida = codigo;
    tarefa_t *pai = &tarefas[t->pai];
    if (t->pai != PID_KERNEL && pai->estado == ESTADO_ESPERANDO && pai->alvo == pid) {
        acordar(pai, codigo);
        t->estado = ESTADO_LIVRE;
    } else if (pid_vivo(t->pai)) {
        t->estado = ESTADO_ZUMBI;
    } else {
        t->estado = ESTADO_LIVRE;
    }
}

/* SYS_CRIAR: cria um processo a partir de um ELF que está na memória de
 * quem chama (normalmente lido do servidor de arquivos). O núcleo não sabe
 * o que é um arquivo: ele só recebe os bytes do programa. */
static long processo_executar(tarefa_t *eu, u64 pedido)
{
    if (!usuario_valido(eu, pedido, sizeof(criar_t))) return ERRO_INVALIDO;
    criar_t c = *(const criar_t *)pedido;
    if (!c.tamanho || !usuario_valido(eu, c.imagem, c.tamanho) || !c.argv)
        return ERRO_INVALIDO;

    /* Os argumentos: um vetor de ponteiros do usuário, terminado em 0. */
    mem_zera(&args_tmp, sizeof(args_tmp));
    for (u32 i = 0; ; i++) {
        u64 ptr = c.argv + 8ull * i;
        if (!usuario_valido(eu, ptr, 8)) return ERRO_INVALIDO;
        u64 texto = *(const u64 *)ptr;
        if (!texto) break;
        char *dst = args_tmp.texto + args_tmp.bytes;
        long n = texto_do_usuario(eu, texto, dst, ARGV_BYTES - args_tmp.bytes);
        if (n < 0 || args_tmp.argc >= ARGV_MAX) return ERRO_INVALIDO;
        args_tmp.bytes += (u32)n + 1;
        args_tmp.argc++;
    }
    if (args_tmp.argc == 0) return ERRO_INVALIDO;    /* argv[0] é o nome */

    u32 pid = 1;
    while (pid < MAX_TAREFAS && tarefas[pid].estado != ESTADO_LIVRE) pid++;
    if (pid == MAX_TAREFAS) return ERRO_LOTADO;

    tarefa_t *t = &tarefas[pid];
    nome_copia(t->nome, args_tmp.texto);
    t->flags         = 0;
    t->imagem        = (const u8 *)c.imagem;   /* lido agora, com o CR3 de quem chama */
    t->imagem_tam    = c.tamanho;
    t->pai           = atual;
    t->reinicios     = 0;
    t->ultima_falha  = 0;
    t->entrada       = c.entrada ? c.entrada       : eu->entrada;
    t->entrada_canal = c.entrada ? c.entrada_canal : eu->entrada_canal;
    t->saida         = c.saida   ? c.saida         : eu->saida;
    t->saida_canal   = c.saida   ? c.saida_canal   : eu->saida_canal;
    mem_copia(t->pasta, eu->pasta, CAMINHO_MAX);
    long erro = processo_carregar(pid, &args_tmp);
    t->imagem = 0;                              /* a imagem era emprestada */
    if (erro) {
        t->estado = ESTADO_LIVRE;
        return erro;
    }
    return pid;
}

/* SYS_COPIAR_PARA / SYS_COPIAR_DE: só vale com um cliente que está
 * esperando a MINHA resposta. Os dois lados são conferidos página a
 * página e a cópia vai de endereço físico para endereço físico. */
static long copiar_cliente(tarefa_t *eu, u64 pid, u64 pedido, int para_ele)
{
    if (!pid_vivo(pid) || tarefas[pid].estado != ESTADO_RECEBENDO ||
        tarefas[pid].alvo != atual)
        return ERRO_PID;
    if (!usuario_valido(eu, pedido, sizeof(copia_t))) return ERRO_INVALIDO;
    copia_t c = *(const copia_t *)pedido;
    tarefa_t *ele = &tarefas[pid];
    if (!usuario_valido(eu, c.meu, c.bytes) || !usuario_valido(ele, c.dele, c.bytes))
        return ERRO_INVALIDO;

    u64 *orig_esp = para_ele ? eu->diretorio : ele->diretorio;
    u64 *dest_esp = para_ele ? ele->diretorio : eu->diretorio;
    u64 orig = para_ele ? c.meu : c.dele, dest = para_ele ? c.dele : c.meu;
    for (u64 n = c.bytes; n; ) {
        u64 cabe = QUADRO - (orig & 0xFFF);
        if (QUADRO - (dest & 0xFFF) < cabe) cabe = QUADRO - (dest & 0xFFF);
        if (cabe > n) cabe = n;
        mem_copia((void *)traduzir(dest_esp, dest), (const void *)traduzir(orig_esp, orig), cabe);
        orig += cabe;
        dest += cabe;
        n    -= cabe;
    }
    return (long)c.bytes;
}

/* ===========================================================================
 *  Chamadas de sistema (int 0x80)
 * =========================================================================== */

static long syscall_executar(quadro_t *q)
{
    tarefa_t *eu = &tarefas[atual];
    u64 a = q->rbx, b = q->rcx;

    switch (q->rax) {
    case SYS_ENVIAR:
    case SYS_CHAMAR:
        if (!pid_vivo(a) || a == atual)              return ERRO_PID;
        if (!usuario_valido(eu, b, sizeof(msg_t)))   return ERRO_INVALIDO;
        mem_copia(&eu->caixa, (const void *)b, sizeof(msg_t));
        eu->msg_usuario = b;               /* a resposta do CHAMAR volta aqui */
        eu->chamando    = (q->rax == SYS_CHAMAR);
        ipc_enviar((u32)a);
        return 0;

    case SYS_RECEBER:
        if (!usuario_valido(eu, b, sizeof(msg_t))) return ERRO_INVALIDO;
        if (a != PID_QUALQUER && a != PID_KERNEL && !pid_vivo(a)) return ERRO_PID;
        eu->msg_usuario = b;
        ipc_receber((u32)a);
        return 0;

    case SYS_RESPONDER:
        /* Nunca bloqueia: um servidor não pode ficar refém de um cliente. */
        if (!pid_vivo(a) || !usuario_valido(eu, b, sizeof(msg_t))) return ERRO_PID;
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
        return (long)ticks;

    case SYS_TAREFA_INFO: {
        if (a >= MAX_TAREFAS)                        return ERRO_PID;
        if (!usuario_valido(eu, b, sizeof(msg_t)))   return ERRO_INVALIDO;
        msg_t *m = (msg_t *)b;
        mem_zera(m, sizeof(*m));
        m->arg[0] = tarefas[a].estado;
        m->arg[1] = tarefas[a].reinicios;
        m->arg[2] = tarefas[a].ultima_falha;
        m->arg[3] = tarefas[a].pai;
        nome_copia(m->texto, tarefas[a].nome);
        return 0;
    }

    case SYS_MEMORIA:
        return (long)(((quadros_livres * 4) << 32) | (quadros_total * 4));

    case SYS_SAIR:
        processo_encerrar(atual, (long)a, 0, 0);
        return 0;

    case SYS_CRIAR:
        return processo_executar(eu, a);

    case SYS_ESPERAR: {
        if (a == 0 || a >= MAX_TAREFAS || tarefas[a].pai != atual ||
            tarefas[a].estado == ESTADO_LIVRE)
            return ERRO_PID;                         /* não é meu filho */
        if (tarefas[a].estado == ESTADO_ZUMBI) {     /* já terminou */
            tarefas[a].estado = ESTADO_LIVRE;
            return tarefas[a].codigo_saida;
        }
        eu->estado = ESTADO_ESPERANDO;               /* acorda no fim do filho */
        eu->alvo   = (u32)a;
        return 0;
    }

    case SYS_SBRK: {
        long incremento = (long)a;
        u64 antigo = eu->heap_fim;
        if (incremento < 0 || (u64)incremento > USUARIO_HEAP_FIM - antigo)
            return ERRO_SEM_MEMORIA;
        if (!mapear_faixa(eu->diretorio, antigo, antigo + (u64)incremento))
            return ERRO_SEM_MEMORIA;
        eu->heap_fim = antigo + (u64)incremento;
        return (long)antigo;
    }

    case SYS_PID:
        return atual;

    case SYS_INITRD: {
        if (!(eu->flags & T_INITRD))                 return ERRO_SEM_PERMISSAO;
        if (!usuario_valido(eu, a, sizeof(msg_t)))   return ERRO_INVALIDO;
        msg_t *m = (msg_t *)a;
        mem_zera(m, sizeof(*m));
        m->arg[0] = (u32)(USUARIO_INITRD + ((u64)initrd & 0xFFF));
        m->arg[1] = (u32)initrd_tam;
        return 0;
    }

    case SYS_COPIAR_PARA:
        return copiar_cliente(eu, a, b, 1);

    case SYS_COPIAR_DE:
        return copiar_cliente(eu, a, b, 0);

    case SYS_AMBIENTE: {
        if (!usuario_valido(eu, a, sizeof(msg_t))) return ERRO_INVALIDO;
        msg_t *m = (msg_t *)a;
        mem_zera(m, sizeof(*m));
        m->arg[0] = eu->entrada;
        m->arg[1] = eu->entrada_canal;
        m->arg[2] = eu->saida;
        m->arg[3] = eu->saida_canal;
        mem_copia(m->texto, eu->pasta, CAMINHO_MAX);
        return 0;
    }

    case SYS_PASTA: {
        char nova[CAMINHO_MAX];
        if (texto_do_usuario(eu, a, nova, CAMINHO_MAX) <= 0 || nova[0] != '/')
            return ERRO_INVALIDO;               /* caminho absoluto; quem confere é o shell */
        mem_copia(eu->pasta, nova, CAMINHO_MAX);
        return 0;
    }

    case SYS_NUCLEO: {
        if (!usuario_valido(eu, a, sizeof(msg_t))) return ERRO_INVALIDO;
        msg_t *m = (msg_t *)a;
        mem_zera(m, sizeof(*m));
        const char *v = "NovaEra " NOVAERA_VERSAO " " NOVAERA_ARQUITETURA;
        mem_copia(m->texto, v, tamanho_str(v) + 1);
        return 0;
    }
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

static _Noreturn void kernel_panico(const quadro_t *q)
{
    u64 cr2 = cpu_ler_cr2();

    const u8 a = ATTR(VGA_BRANCO, VGA_VERMELHO);
    for (u32 i = 0; i < VGA_COLUNAS * 3; i++)
        mem_escreve16(VGA_MEMORIA + 2 * i, vga_celula(' ', a));

    u32 c = vga_print(0, 1, "*** PANICO DO NUCLEO: ", a);
    vga_print(0, c, q->vetor < 22 ? EXC_NOMES[q->vetor] : "Reservado", a);

    c = vga_print(1, 1, "VETOR=", a);   c = vga_hex(1, c, q->vetor, a);
    c = vga_print(1, c, " ERRO=", a);   vga_hex(1, c, q->erro, a);
    c = vga_print(2, 1, "RIP=", a);     c = vga_hex(2, c, q->rip, a);
    c = vga_print(2, c, " CR2=", a);    vga_hex(2, c, cr2, a);

    cpu_parar();
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
        if (q->vetor == 14 && pilha_crescer(&tarefas[atual], cpu_ler_cr2())) {
            /* só faltava pilha: a página nova já está lá, tenta de novo */
        } else {                                 /* o usuário falhou: isola */
            processo_encerrar(atual, SAIDA_FALHA_BASE + (long)q->vetor, 1, q->vetor);
        }
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
        u64 numero = q->rax;
        long r = syscall_executar(q);
        if (tarefas[atual].estado == ESTADO_PRONTO)
            q->rax = (u64)r;                     /* bloqueado: recebe depois */
        if (numero == SYS_CEDER)
            trocar = 1;
    }

    if (trocar || tarefas[atual].estado != ESTADO_PRONTO)
        escalonar();

    tarefa_t *t = &tarefas[atual];
    if ((u64)t->diretorio != cr3_atual) {
        cr3_atual = (u64)t->diretorio;
        cpu_carregar_cr3(cr3_atual);
    }
    if (t->pilha_nucleo)
        tss_definir_rsp0((u64)t->pilha_nucleo + PILHA_NUCLEO_TAM);

    if (t->entregar) {                           /* última etapa do IPC */
        mem_copia((void *)t->msg_usuario, &t->caixa, sizeof(msg_t));
        t->entregar = 0;
    }
    return t->quadro;
}

/* ------------------------------- Mensagem ------------------------------- */

/* Nome, versão e lema vêm de sistema.conf: cada sistema tem a sua cara.
 * No NovaEra puro (sem distribuição), a versão mostrada é a do núcleo. */
#if SISTEMA_PURO
#define VERSAO_BANNER NOVAERA_VERSAO " " NOVAERA_ARQUITETURA
#else
#define VERSAO_BANNER SISTEMA_VERSAO
#endif

static void banner(void)
{
    const char *sep = tamanho_str(SISTEMA_LEMA) ? " - " : "";
    const u32 larg = tamanho_str(SISTEMA_NOME) + 2 + tamanho_str(VERSAO_BANNER)
                   + tamanho_str(sep) + tamanho_str(SISTEMA_LEMA);
    u32 col = 0;
    if (larg + 6 <= VGA_COLUNAS) {          /* cabe a moldura? */
        col = (VGA_COLUNAS - larg) / 2;
        vga_moldura(0, col - 3, larg + 6, 3, ATTR(VGA_CIANO_CLARO, FUNDO));
    }

    u32 c = col;
    c = vga_print(1, c, SISTEMA_NOME,   ATTR(VGA_BRANCO, FUNDO));
    c = vga_print(1, c, " v",           ATTR(VGA_BRANCO, FUNDO));
    c = vga_print(1, c, VERSAO_BANNER,  ATTR(VGA_BRANCO, FUNDO));
    c = vga_print(1, c, sep,            ATTR(VGA_CINZA_CLARO, FUNDO));
    vga_print(1, c, SISTEMA_LEMA,       ATTR(VGA_AMARELO, FUNDO));
}

/* --------------------------- Ponto de entrada C -------------------------- */

void kernel_main(u64 magic, u64 mbi_endereco)
{
    vga_ativar_cursor();
    vga_limpar(ATTR_FUNDO);
    banner();
    t_lin = 4; t_col = 0;

    term_status(1, "Modo longo 64 bits (GDT com aneis 0 e 3)");
    term_status(magic == MULTIBOOT_MAGIC_BOOTLOADER, "Bootloader Multiboot detectado");

    memoria_iniciar(magic == MULTIBOOT_MAGIC_BOOTLOADER ? mbi_endereco : 0);
    term_escreve("[",    ATTR(VGA_CINZA_CLARO, FUNDO));
    term_escreve(" OK ", ATTR(VGA_VERDE_CLARO, FUNDO));
    term_escreve("] Memoria fisica: ", ATTR(VGA_CINZA_CLARO, FUNDO));
    term_numero((u32)(quadros_livres * 4), ATTR(VGA_BRANCO, FUNDO));
    term_escreve(" KiB livres em quadros de 4 KiB\n", ATTR(VGA_BRANCO, FUNDO));

    term_status(1, "Paginacao de 4 niveis: nucleo protegido do anel 3");

    if (initrd) {                            /* quem lê o tar é o servidor de arquivos */
        term_escreve("[",    ATTR(VGA_CINZA_CLARO, FUNDO));
        term_escreve(" OK ", ATTR(VGA_VERDE_CLARO, FUNDO));
        term_escreve("] initrd: ", ATTR(VGA_CINZA_CLARO, FUNDO));
        term_numero((u32)(initrd_tam / 1024), ATTR(VGA_BRANCO, FUNDO));
        term_escreve(" KiB, entregue ao servidor de arquivos\n", ATTR(VGA_BRANCO, FUNDO));
    } else {
        term_escreve("[ -- ] sem initrd: so os servidores do boot\n", ATTR(VGA_CINZA_CLARO, FUNDO));
    }

    tss_iniciar();
    idt_iniciar();
    term_status(1, "IDT: 32 excecoes + 16 IRQs + int 0x80 (syscalls)");

    pic_iniciar();
    timer_iniciar();
    term_status(1, "Timer PIT a 100 Hz: escalonador preemptivo");

    ocioso_iniciar();
    /* Cada linha de programas.lista vira um processo; o PID é a linha. */
    int ok = NUM_PROGRAMAS < MAX_TAREFAS;
    for (u32 i = 0; ok && i < NUM_PROGRAMAS; i++)
        ok = processo_criar(i + 1, PROGRAMAS[i].nome, PROGRAMAS[i].ini,
                            PROGRAMAS[i].fim, PROGRAMAS[i].flags);
    term_status(ok, "Processos no anel 3 (programas.lista):");
    term_escreve("      ", ATTR_FUNDO);
    for (u32 i = 0; i < NUM_PROGRAMAS; i++) {
        term_escreve(PROGRAMAS[i].nome, ATTR(VGA_AMARELO, FUNDO));
        term_escreve(" ", ATTR_FUNDO);
    }
    term_putc('\n');

    term_escreve("\nEntregando a CPU aos processos do usuario...\n\n", ATTR(VGA_CINZA_CLARO, FUNDO));
    vga_mover_cursor(t_lin, t_col);         /* o servidor de vídeo continua daqui */

    /* A partir daqui este código é o processo 0 ("nucleo", ocioso).
     * A próxima IRQ do timer chama o escalonador, e a festa começa. */
    cpu_ocioso();
}
