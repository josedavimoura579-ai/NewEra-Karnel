; ===========================================================================
;  NovaEra - usuario/crt0.asm
;  O começo dos servidores do NovaEra (os programas que só usam a ulib).
;  Os programas que usam a libc começam em libc/crt0.asm, que chama exit()
;  para esvaziar os buffers do stdio. As chamadas de sistema e as portas
;  de E/S ficam em usuario/sys.asm.
;
;  Montar com:  nasm -f elf64 usuario/crt0.asm -o crt0.o
; ===========================================================================

bits 64

; O núcleo começa a executar no ponto de entrada do ELF, que é este
; _start, com RDI = argc e RSI = argv (os textos ficam no topo da pilha).
; O valor que main() devolve vira o código de saída do processo.
section .text.entrada progbits alloc exec nowrite
global _start
extern main
_start:
    call main                        ; main(argc, argv): RDI e RSI já prontos
    mov rbx, rax                     ; código de saída
    mov eax, 7                       ; SYS_SAIR
    int 0x80
.fim:
    jmp .fim

section .note.GNU-stack noalloc noexec nowrite progbits
