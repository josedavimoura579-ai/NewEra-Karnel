# ===========================================================================
#  NovaEra Microkernel (x86_64) - boot.s
#
#  O bootloader Multiboot sempre entrega a CPU em modo protegido de 32 bits.
#  Este arquivo é a ponte até os 64 bits ("modo longo"):
#    1. confere se a CPU suporta 64 bits (CPUID)
#    2. monta tabelas de página que mapeiam o 1º GiB (páginas de 2 MiB)
#    3. liga PAE, o modo longo (EFER.LME) e a paginação
#    4. salta para o código de 64 bits e chama kernel_main() em C
#
#  Também ficam aqui: a GDT (anéis 0 e 3 + TSS), os stubs de interrupção,
#  a porta das chamadas de sistema (int 0x80) e a troca de contexto.
#
#  Sintaxe AT&T, a mesma que o QBE escreve: "instrução origem, destino",
#  registradores com %, números com $. Montar com:
#      nefs-as -o boot.o boot.s        (ou: as --64 -o boot.o boot.s)
# ===========================================================================

# --- Cabeçalho Multiboot 1 com endereços explícitos ------------------------
# O bit 16 das flags diz ao carregador: "não leia o ELF, copie o arquivo
# para estes endereços". Assim um kernel de 64 bits carrega pelo Multiboot
# (que só entende ELF de 32 bits), tanto no QEMU quanto no GRUB.
.set MB_MAGIC,    0x1BADB002
.set MB_FLAGS,    0x00010003
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.set CODE_SEG,    0x08
.set DATA_SEG,    0x10
.set STACK_SIZE,  16384

# _carga_fim (fim de .text/.rodata/.data) e _kernel_fim (fim do .bss) vêm
# do ligador.

.section .multiboot,"a",@progbits
.balign 4
mb_cabecalho:
    .long MB_MAGIC
    .long MB_FLAGS
    .long MB_CHECKSUM
    .long mb_cabecalho               # header_addr
    .long mb_cabecalho               # load_addr  (o arquivo começa aqui)
    .long _carga_fim                 # load_end_addr
    .long _kernel_fim                # bss_end_addr (o carregador zera o .bss)
    .long _start                     # entry_addr

# --- Pilha do núcleo e tabelas de página do boot ---------------------------
.bss
.balign 4096
.globl pml4_boot
.globl pd_boot
pml4_boot:  .skip 4096               # nível 4: cada entrada cobre 512 GiB
pdpt_boot:  .skip 4096               # nível 3: cada entrada cobre 1 GiB
pd_boot:    .skip 4096               # nível 2: 512 páginas de 2 MiB = 1 GiB
stack_bottom:
    .skip STACK_SIZE
stack_top:

# --- GDT de 64 bits --------------------------------------------------------
# No modo longo a segmentação quase some: base e limite são ignorados.
# Os descritores servem para dizer o ANEL (0 ou 3) e que o código é 64 bits
# (bit L). A proteção de memória de verdade vem da paginação.
.data
.balign 16
.globl gdt_tss
gdt_start:
    .quad 0x0000000000000000         # 0x00: nulo
    .quad 0x00AF9A000000FFFF         # 0x08: código do núcleo  (anel 0, 64 bits)
    .quad 0x00CF92000000FFFF         # 0x10: dados do núcleo   (anel 0)
    .quad 0x00AFFA000000FFFF         # 0x18: código do usuário (anel 3, 64 bits) -> 0x1B
    .quad 0x00CFF2000000FFFF         # 0x20: dados do usuário  (anel 3) -> 0x23
gdt_tss:
    .quad 0, 0                       # 0x28: TSS de 64 bits ocupa 16 bytes (tss_iniciar)
gdt_end:

gdt_descriptor:
    .short gdt_end - gdt_start - 1
    .quad gdt_start

# --- Ponto de entrada (ainda em 32 bits) -----------------------------------
.text
.code32
.globl _start

_start:
    cli
    cld
    movl $stack_top, %esp
    movl %eax, %edi                  # magic Multiboot
    movl %ebx, %esi                  # ponteiro para a info Multiboot

    # A CPU fala 64 bits? CPUID 0x80000001, EDX bit 29 = "Long Mode".
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb .Lsem_64_bits
    movl $0x80000001, %eax
    cpuid
    testl $(1 << 29), %edx
    jz .Lsem_64_bits

    # Tabelas de página: PML4[0] -> PDPT[0] -> PD com 512 páginas de 2 MiB.
    # Endereço virtual = físico no 1º GiB. Só o anel 0 enxerga (bit U=0).
    movl $pdpt_boot, %eax
    orl $0x3, %eax                   # presente + escrita
    movl %eax, pml4_boot
    movl $pd_boot, %eax
    orl $0x3, %eax
    movl %eax, pdpt_boot

    xorl %ecx, %ecx
.Lmapeia_2mb:
    movl %ecx, %eax
    shll $21, %eax                   # ecx * 2 MiB
    orl $0x83, %eax                  # presente + escrita + página grande (PS)
    movl %eax, pd_boot(,%ecx,8)
    incl %ecx
    cmpl $512, %ecx
    jne .Lmapeia_2mb

    movl $pml4_boot, %eax
    movl %eax, %cr3

    movl %cr4, %eax
    orl $(1 << 5), %eax              # PAE: obrigatório no modo longo
    movl %eax, %cr4

    movl $0xC0000080, %ecx           # MSR EFER
    rdmsr
    orl $(1 << 8), %eax              # LME: Long Mode Enable
    wrmsr

    movl %cr0, %eax
    orl $(1 << 31), %eax             # PG: liga a paginação -> modo longo ativo
    movl %eax, %cr0

    lgdt gdt_descriptor
    ljmp $CODE_SEG, $inicio_64       # recarrega CS com um segmento de 64 bits

.Lsem_64_bits:
    # Escreve direto na VGA (ainda não existe nada mais) e para.
    movl $msg_sem_64, %esi
    movl $0xB8000, %edi
.Lescreve:
    lodsb
    testb %al, %al
    jz .Lparar32
    movb $0x4F, %ah                  # branco sobre vermelho
    stosw
    jmp .Lescreve
.Lparar32:
    cli
    hlt
    jmp .Lparar32

.section .rodata
msg_sem_64: .asciz "NovaEra: esta CPU nao suporta 64 bits. Use qemu-system-x86_64."

# --- Agora em 64 bits ------------------------------------------------------
.text
.code64
inicio_64:
    movw $DATA_SEG, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    pushq $0                         # zera RFLAGS (IF=0)
    popfq

    movl %edi, %edi                  # a metade de cima dos registradores fica
    movl %esi, %esi                  # indefinida na troca de modo: zera
    movq $stack_top, %rsp            # pilha alinhada em 16 bytes
    call kernel_main                 # kernel_main(magic, mbi) - RDI, RSI

.Lparar64:
    cli
    hlt
    jmp .Lparar64

# ===========================================================================
#  Stubs de interrupção
#
#  No modo longo a CPU SEMPRE empilha SS, RSP, RFLAGS, CS e RIP (mesmo sem
#  troca de anel). Exceções SEM código de erro empilham um 0 falso, para
#  que o quadro seja idêntico para todos os vetores (quadro_t em kernel.c).
#  Depois, todo stub empilha o número do vetor e salta para isr_comum.
# ===========================================================================

# --- Exceções da CPU (vetores 0-31) ---
isr0:   pushq $0; pushq $0;  jmp isr_comum     # divisão por zero
isr1:   pushq $0; pushq $1;  jmp isr_comum     # debug
isr2:   pushq $0; pushq $2;  jmp isr_comum     # NMI
isr3:   pushq $0; pushq $3;  jmp isr_comum     # breakpoint
isr4:   pushq $0; pushq $4;  jmp isr_comum     # overflow
isr5:   pushq $0; pushq $5;  jmp isr_comum     # limite excedido (BOUND)
isr6:   pushq $0; pushq $6;  jmp isr_comum     # opcode inválido
isr7:   pushq $0; pushq $7;  jmp isr_comum     # dispositivo indisponível (FPU)
isr8:             pushq $8;  jmp isr_comum     # falta dupla (a CPU já empilhou o erro)
isr9:   pushq $0; pushq $9;  jmp isr_comum     # (legado) segmento do coprocessador
isr10:            pushq $10; jmp isr_comum     # TSS inválido
isr11:            pushq $11; jmp isr_comum     # segmento ausente
isr12:            pushq $12; jmp isr_comum     # falha de pilha
isr13:            pushq $13; jmp isr_comum     # proteção geral (GPF)
isr14:            pushq $14; jmp isr_comum     # falha de página
isr15:  pushq $0; pushq $15; jmp isr_comum     # reservado
isr16:  pushq $0; pushq $16; jmp isr_comum     # erro x87
isr17:            pushq $17; jmp isr_comum     # verificação de alinhamento
isr18:  pushq $0; pushq $18; jmp isr_comum     # machine check
isr19:  pushq $0; pushq $19; jmp isr_comum     # erro SIMD
isr20:  pushq $0; pushq $20; jmp isr_comum     # virtualização
isr21:            pushq $21; jmp isr_comum     # proteção de fluxo de controle
isr22:  pushq $0; pushq $22; jmp isr_comum
isr23:  pushq $0; pushq $23; jmp isr_comum
isr24:  pushq $0; pushq $24; jmp isr_comum
isr25:  pushq $0; pushq $25; jmp isr_comum
isr26:  pushq $0; pushq $26; jmp isr_comum
isr27:  pushq $0; pushq $27; jmp isr_comum
isr28:  pushq $0; pushq $28; jmp isr_comum
isr29:            pushq $29; jmp isr_comum
isr30:            pushq $30; jmp isr_comum
isr31:  pushq $0; pushq $31; jmp isr_comum

# --- IRQs do PIC remapeado (vetores 32-47 = IRQ 0-15) ---
isr32:  pushq $0; pushq $32; jmp isr_comum     # IRQ 0  - timer
isr33:  pushq $0; pushq $33; jmp isr_comum     # IRQ 1  - TECLADO
isr34:  pushq $0; pushq $34; jmp isr_comum     # IRQ 2  - cascata do PIC escravo
isr35:  pushq $0; pushq $35; jmp isr_comum     # IRQ 3
isr36:  pushq $0; pushq $36; jmp isr_comum     # IRQ 4
isr37:  pushq $0; pushq $37; jmp isr_comum     # IRQ 5
isr38:  pushq $0; pushq $38; jmp isr_comum     # IRQ 6
isr39:  pushq $0; pushq $39; jmp isr_comum     # IRQ 7
isr40:  pushq $0; pushq $40; jmp isr_comum     # IRQ 8
isr41:  pushq $0; pushq $41; jmp isr_comum     # IRQ 9
isr42:  pushq $0; pushq $42; jmp isr_comum     # IRQ 10
isr43:  pushq $0; pushq $43; jmp isr_comum     # IRQ 11
isr44:  pushq $0; pushq $44; jmp isr_comum     # IRQ 12
isr45:  pushq $0; pushq $45; jmp isr_comum     # IRQ 13
isr46:  pushq $0; pushq $46; jmp isr_comum     # IRQ 14
isr47:  pushq $0; pushq $47; jmp isr_comum     # IRQ 15

# Tabela com o endereço de cada stub (lida pelo C para montar a IDT).
.section .rodata
.balign 8
.globl isr_tabela
isr_tabela:
    .quad isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7
    .quad isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15
    .quad isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    .quad isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    .quad isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39
    .quad isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47

# --- Chamadas de sistema: int 0x80 (a única porta aberta ao anel 3) ---
.text
.globl isr_syscall
isr_syscall:
    pushq $0
    pushq $0x80
    jmp isr_comum

# --- Corpo comum: salva o processo, chama o C, retoma (talvez OUTRO) processo
#
# Quadro completo na pilha do núcleo (struct quadro_t em kernel.c):
#   [R15 ... RAX][vetor][erro][RIP CS RFLAGS RSP SS]
# São 22 palavras de 8 bytes: como a CPU alinha a pilha em 16 antes de
# empilhar, a chamada ao C também fica alinhada.
#
# interrupt_dispatch() devolve o endereço do quadro a ser retomado. Se for
# o de outro processo, a troca de contexto acontece aqui: basta mudar RSP.
isr_comum:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    cld                              # ABI C exige DF=0
    movq %rsp, %rdi                  # argumento: ponteiro para o quadro
    call interrupt_dispatch
    movq %rax, %rsp                  # <- TROCA DE CONTEXTO
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp                   # descarta vetor + código de erro
    iretq                            # volta ao anel do processo escolhido

# Os programas do usuário são embutidos por build/programas.s, gerado
# pelo build.sh a partir de programas.lista.

.section .note.GNU-stack,"",@progbits
