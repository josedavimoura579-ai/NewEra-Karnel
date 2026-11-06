# ===========================================================================
#  NovaEra Microkernel - cpu.s
#  As instruções especiais da CPU que o C não sabe escrever, como funções
#  comuns (ABI System V: argumentos em RDI, RSI; resultado em RAX).
#
#  Por que não usar __asm__ dentro do C? Porque o compilador do NEFS
#  (cproc + QBE) não aceita assembly embutido. Assim o núcleo compila
#  tanto com o clang quanto com o cproc.
#
#  Montar com:  nefs-as -o cpu.o cpu.s
# ===========================================================================

.text

# void outb(u16 porta, u8 valor)
.globl outb
outb:
    movw %di, %dx
    movb %sil, %al
    outb %al, %dx
    ret

# u8 inb(u16 porta)
.globl inb
inb:
    movw %di, %dx
    xorl %eax, %eax
    inb %dx, %al
    ret

# void mem_escreve16(u64 endereco, u16 valor)
# u16  mem_le16(u64 endereco)
# Acesso a memória de dispositivo (como a VGA em 0xB8000). Em C isso exigiria
# "volatile", que o cproc ainda não suporta; numa função em assembly nenhum
# compilador consegue apagar ou reordenar o acesso.
.globl mem_escreve16
mem_escreve16:
    movw %si, (%rdi)
    ret

.globl mem_le16
mem_le16:
    movzwl (%rdi), %eax
    ret

# void cpu_fpu_ligar(void)
# Liga a FPU e o SSE (os registradores de ponto flutuante dos programas):
# CR0.EM = 0 (sem emulação), CR0.MP = 1, CR0.TS = 0; CR4.OSFXSR e
# CR4.OSXMMEXCPT = 1 (o sistema sabe salvar o SSE com FXSAVE).
.globl cpu_fpu_ligar
cpu_fpu_ligar:
    movq %cr0, %rax
    andq $~((1 << 2) | (1 << 3)), %rax
    orq $(1 << 1), %rax
    movq %rax, %cr0
    movq %cr4, %rax
    orq $((1 << 9) | (1 << 10)), %rax
    movq %rax, %cr4
    fninit
    ret

# void cpu_fpu_salvar(u64 area) / cpu_fpu_restaurar(u64 area)
# Os 512 bytes de estado da FPU + SSE de um processo (área alinhada em 16).
.globl cpu_fpu_salvar
cpu_fpu_salvar:
    fxsave (%rdi)
    ret

.globl cpu_fpu_restaurar
cpu_fpu_restaurar:
    fxrstor (%rdi)
    ret

# void cpu_carregar_cr3(u64 pml4)   - troca o espaço de endereçamento
.globl cpu_carregar_cr3
cpu_carregar_cr3:
    movq %rdi, %cr3
    ret

# u64 cpu_ler_cr2(void)              - endereço que causou a falha de página
.globl cpu_ler_cr2
cpu_ler_cr2:
    movq %cr2, %rax
    ret

# void cpu_carregar_idt(u64 base, u16 limite)
.globl cpu_carregar_idt
cpu_carregar_idt:
    subq $16, %rsp
    movw %si, (%rsp)                 # o ponteiro da IDT tem 10 bytes:
    movq %rdi, 2(%rsp)               # limite (2) + base (8), sem alinhamento
    lidt (%rsp)
    addq $16, %rsp
    ret

# void cpu_carregar_tr(u16 seletor) - diz à CPU onde está a TSS
.globl cpu_carregar_tr
cpu_carregar_tr:
    ltr %di
    ret

# void cpu_ocioso(void)              - liga as interrupções e dorme para sempre
.globl cpu_ocioso
cpu_ocioso:
    sti
.Ldorme:
    hlt
    jmp .Ldorme

# void cpu_parar(void)               - desliga as interrupções e para de vez
.globl cpu_parar
cpu_parar:
    cli
    hlt
    jmp cpu_parar

.section .note.GNU-stack,"",@progbits
