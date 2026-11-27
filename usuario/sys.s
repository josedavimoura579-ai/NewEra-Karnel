# ===========================================================================
#  NovaEra - usuario/sys.s
#  As poucas instruções que o C não sabe escrever, para os programas do
#  usuário: a chamada de sistema (int 0x80), as portas de E/S e o acesso a
#  memória de dispositivo. Em assembly para compilar tanto com o clang
#  quanto com o compilador do NEFS (cproc + QBE), que não aceita __asm__.
#
#  Montar com:  nefs-as -o sys.o usuario/sys.s
# ===========================================================================

.text

# long syscall2(u64 numero, u64 a, u64 b)
# Convenção do NovaEra: RAX = número, RBX = a, RCX = b; resultado em RAX.
.globl syscall2
syscall2:
    pushq %rbx                       # RBX pertence a quem chamou: preserva
    movq %rdi, %rax
    movq %rsi, %rbx
    movq %rdx, %rcx
    int $0x80
    popq %rbx
    ret

# void __sinal_volta(void)
# O "restaurador": o tratador de sinal volta (ret) para cá, e o núcleo
# devolve os registradores de antes do sinal (SYS_SINAL_VOLTAR = 22).
# Não retorna.
.globl __sinal_volta
__sinal_volta:
    movl $22, %eax
    int $0x80
    hlt                              # nunca chega aqui (e daria GPF no anel 3)

# Operações atômicas (para as threads): ninguém consegue ver o número no
# meio da troca, nem outra thread, nem uma interrupção. Em bytes porque o
# nefs-as ainda não conhece cmpxchg e xadd.
#
# int atomico_troca(int *p, int v)        guarda v, devolve o que havia
.globl atomico_troca
atomico_troca:
    .byte 0x87, 0x37                 # xchgl %esi, (%rdi)
    movl %esi, %eax
    ret

# int atomico_cmpxchg(int *p, int esperado, int novo)
# se *p == esperado, guarda novo; devolve o que havia em *p
.globl atomico_cmpxchg
atomico_cmpxchg:
    movl %esi, %eax
    .byte 0xf0, 0x0f, 0xb1, 0x17     # lock cmpxchgl %edx, (%rdi)
    ret

# int atomico_soma(int *p, int v)         soma v, devolve o valor de antes
.globl atomico_soma
atomico_soma:
    .byte 0xf0, 0x0f, 0xc1, 0x37     # lock xaddl %esi, (%rdi)
    movl %esi, %eax
    ret

# u64 rdtsc(void) - o contador de ciclos da CPU (para sortear um "sal")
.globl rdtsc
rdtsc:
    .byte 0x0f, 0x31                 # rdtsc: EDX:EAX = ciclos desde que ligou
    shlq $32, %rdx
    orq %rdx, %rax
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

# u32 mem_le32(u64 endereco)
.globl mem_le32
mem_le32:
    movl (%rdi), %eax
    ret

# void outb(u16 porta, u8 valor)    - só drivers (IOPL 3)
.globl outb
outb:
    movw %di, %dx
    movb %sil, %al
    outb %al, %dx
    ret

# void ins16(u16 porta, void *buf, u64 n)   - lê n palavras de 16 bits da porta
# void outw(u16 porta, u16 valor)   - só drivers (IOPL 3)
.globl outw
outw:
    movw %di, %dx
    movw %si, %ax
    outw %ax, %dx
    ret

# u16 inw(u16 porta) / void outl(u16 porta, u32 v) / u32 inl(u16 porta)
# (a placa de vídeo BGA usa portas de 16 bits; o PCI, de 32)
.globl inw
inw:
    movw %di, %dx
    xorl %eax, %eax
    inw %dx, %ax
    ret

.globl outl
outl:
    movw %di, %dx
    movl %esi, %eax
    outl %eax, %dx
    ret

.globl inl
inl:
    movw %di, %dx
    inl %dx, %eax
    ret

# void outs16(u16 porta, const void *buf, u64 n) - escreve n palavras na porta
# (o disco ATA entrega e recebe os setores assim: 256 palavras cada)
.globl ins16
ins16:
    movq %rdx, %rcx
    movw %di, %dx
    movq %rsi, %rdi
    rep insw
    ret

.globl outs16
outs16:
    movq %rdx, %rcx
    movw %di, %dx
    rep outsw
    ret

# u8 inb(u16 porta)
.globl inb
inb:
    movw %di, %dx
    xorl %eax, %eax
    inb %dx, %al
    ret

.section .note.GNU-stack,"",@progbits
