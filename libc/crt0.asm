; ===========================================================================
;  nlibc - libc/crt0.asm
;  O começo de todo programa que usa a libc: prepara o stdio, chama
;  main(argc, argv) e entrega o valor devolvido a exit(), que esvazia os
;  buffers, fecha os arquivos e os pipes e termina o processo.
;
;  Montar com:  nasm -f elf64 libc/crt0.asm -o crt0-libc.o
; ===========================================================================

bits 64

section .text.entrada progbits alloc exec nowrite
global _start
extern main
extern exit
extern __libc_iniciar
_start:
    mov r12, rdi                     ; argc e argv chegam em RDI e RSI;
    mov r13, rsi                     ; guarda enquanto a libc se prepara
    call __libc_iniciar
    mov rdi, r12
    mov rsi, r13
    call main
    mov edi, eax                     ; exit(o que main devolveu)
    call exit
.fim:
    jmp .fim

section .note.GNU-stack noalloc noexec nowrite progbits
