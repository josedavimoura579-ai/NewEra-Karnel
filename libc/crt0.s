# ===========================================================================
#  nlibc - libc/crt0.s
#  O começo de todo programa que usa a libc: prepara o stdio, chama
#  main(argc, argv) e entrega o valor devolvido a exit(), que esvazia os
#  buffers, fecha os arquivos e os pipes e termina o processo.
#
#  Montar com:  nefs-as -o crt0.o libc/crt0.s
# ===========================================================================

.section .text.entrada,"ax",@progbits

.globl _start
_start:
    movq %rdi, %r12                  # argc e argv chegam em RDI e RSI;
    movq %rsi, %r13                  # guarda enquanto a libc se prepara
    call __libc_iniciar
    movq %r12, %rdi
    movq %r13, %rsi
    call main
    movl %eax, %edi                  # exit(o que main devolveu)
    call exit
.Lfim:
    jmp .Lfim

.section .note.GNU-stack,"",@progbits
