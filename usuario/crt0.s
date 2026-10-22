# ===========================================================================
#  NovaEra - usuario/crt0.s
#  O começo dos servidores do NovaEra (os programas que só usam a ulib).
#  Os programas que usam a libc começam em libc/crt0.s, que chama exit()
#  para esvaziar os buffers do stdio. As chamadas de sistema e as portas
#  de E/S ficam em usuario/sys.s.
#
#  Montar com:  nefs-as -o crt0.o usuario/crt0.s
# ===========================================================================

.section .text.entrada,"ax",@progbits

# O núcleo começa a executar no ponto de entrada do ELF, que é este
# _start, com RDI = argc e RSI = argv (os textos ficam no topo da pilha).
# O valor que main() devolve vira o código de saída do processo.
.globl _start
_start:
    call main                        # main(argc, argv): RDI e RSI já prontos
    movq %rax, %rbx                  # código de saída
    movl $7, %eax                    # SYS_SAIR
    int $0x80
.Lfim:
    jmp .Lfim

.section .note.GNU-stack,"",@progbits
