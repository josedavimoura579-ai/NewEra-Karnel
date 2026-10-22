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

# u8 inb(u16 porta)
.globl inb
inb:
    movw %di, %dx
    xorl %eax, %eax
    inb %dx, %al
    ret

.section .note.GNU-stack,"",@progbits
