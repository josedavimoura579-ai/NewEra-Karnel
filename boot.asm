; ===========================================================================
;  NovaEra Microkernel v2.0 - boot.asm
;  - Cabeçalho Multiboot 1 (GRUB / QEMU -kernel)
;  - GDT com segmentos do núcleo (anel 0), do usuário (anel 3) e a TSS
;  - Pilha e salto para kernel_main() em C
;  - 48 "stubs" de interrupção (32 exceções da CPU + 16 IRQs do PIC) e a
;    porta das chamadas de sistema (int 0x80), todos convergindo para
;    isr_comum -> interrupt_dispatch() em C, que pode TROCAR de processo
;  - Os programas do usuário (servidores), embutidos com incbin
;
;  Montar com:  nasm -f elf32 boot.asm -o boot.o
; ===========================================================================

bits 32

; --- Cabeçalho Multiboot (especificação 0.6.96) ----------------------------
MB_MAGIC     equ 0x1BADB002
MB_FLAGS     equ 0x00000003
MB_CHECKSUM  equ -(MB_MAGIC + MB_FLAGS)

CODE_SEG     equ 0x08
DATA_SEG     equ 0x10
STACK_SIZE   equ 16384

section .multiboot progbits alloc noexec nowrite align=4
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

; --- Pilha do kernel -------------------------------------------------------
section .bss align=16
stack_bottom:
    resb STACK_SIZE
stack_top:

; --- GDT: modelo "flat" de 4 GiB -------------------------------------------
; A proteção de memória de verdade vem da paginação (kernel.c); os segmentos
; servem para dizer à CPU em que ANEL de privilégio o código está rodando.
section .data
align 8
global gdt_tss
gdt_start:
    dq 0x0000000000000000            ; 0x00: nulo
    dq 0x00CF9A000000FFFF            ; 0x08: código do núcleo  (anel 0)
    dq 0x00CF92000000FFFF            ; 0x10: dados do núcleo   (anel 0)
    dq 0x00CFFA000000FFFF            ; 0x18: código do usuário (anel 3) -> seletor 0x1B
    dq 0x00CFF2000000FFFF            ; 0x20: dados do usuário  (anel 3) -> seletor 0x23
gdt_tss:
    dq 0                             ; 0x28: TSS (preenchida por tss_iniciar() em C)
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; --- Ponto de entrada ------------------------------------------------------
section .text
global _start
extern kernel_main
extern interrupt_dispatch

_start:
    cli                              ; sem interrupções até a IDT existir
    cld
    mov esp, stack_top

    mov edi, eax                     ; magic Multiboot
    mov esi, ebx                     ; ponteiro para a info Multiboot

    lgdt [gdt_descriptor]
    jmp CODE_SEG:.recarrega_cs

.recarrega_cs:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    push dword 0                     ; zera EFLAGS (IF=0)
    popfd

    sub esp, 8                       ; alinha a pilha em 16 bytes na chamada
    push esi                         ; 2º argumento: info Multiboot
    push edi                         ; 1º argumento: magic
    call kernel_main

.parar:
    cli
    hlt
    jmp .parar

; ===========================================================================
;  Stubs de interrupção
;
;  Layout da pilha quando isr_comum roda (do topo para baixo):
;      [vetor][código de erro][EIP][CS][EFLAGS]
;  Exceções SEM código de erro da CPU empilham um 0 falso, para que o quadro
;  seja idêntico para todos os vetores (struct quadro_t em kernel.c).
; ===========================================================================

; Tabela com o endereço de cada stub (lida pelo C para montar a IDT).
; Cada stub acrescenta uma entrada aqui, em ordem: vetor 0, 1, 2 ... 47.
section .rodata
global isr_tabela
isr_tabela:

%macro ISR_SEM_ERRO 1
section .text
isr%1:
    push dword 0                     ; código de erro falso
    push dword %1                    ; número do vetor
    jmp isr_comum
section .rodata
    dd isr%1
%endmacro

%macro ISR_COM_ERRO 1
section .text
isr%1:
    push dword %1                    ; a CPU já empilhou o código de erro
    jmp isr_comum
section .rodata
    dd isr%1
%endmacro

; --- Exceções da CPU (vetores 0-31) ---
ISR_SEM_ERRO 0      ; divisão por zero
ISR_SEM_ERRO 1      ; debug
ISR_SEM_ERRO 2      ; NMI
ISR_SEM_ERRO 3      ; breakpoint
ISR_SEM_ERRO 4      ; overflow
ISR_SEM_ERRO 5      ; limite excedido (BOUND)
ISR_SEM_ERRO 6      ; opcode inválido
ISR_SEM_ERRO 7      ; dispositivo indisponível (FPU)
ISR_COM_ERRO 8      ; falta dupla
ISR_SEM_ERRO 9      ; (legado) segmento do coprocessador
ISR_COM_ERRO 10     ; TSS inválido
ISR_COM_ERRO 11     ; segmento ausente
ISR_COM_ERRO 12     ; falha de pilha
ISR_COM_ERRO 13     ; proteção geral (GPF)
ISR_COM_ERRO 14     ; falha de página
ISR_SEM_ERRO 15     ; reservado
ISR_SEM_ERRO 16     ; erro x87
ISR_COM_ERRO 17     ; verificação de alinhamento
ISR_SEM_ERRO 18     ; machine check
ISR_SEM_ERRO 19     ; erro SIMD
ISR_SEM_ERRO 20     ; virtualização
ISR_COM_ERRO 21     ; proteção de fluxo de controle
ISR_SEM_ERRO 22
ISR_SEM_ERRO 23
ISR_SEM_ERRO 24
ISR_SEM_ERRO 25
ISR_SEM_ERRO 26
ISR_SEM_ERRO 27
ISR_SEM_ERRO 28
ISR_COM_ERRO 29
ISR_COM_ERRO 30
ISR_SEM_ERRO 31

; --- IRQs do PIC remapeado (vetores 32-47 = IRQ 0-15) ---
ISR_SEM_ERRO 32     ; IRQ 0  - timer
ISR_SEM_ERRO 33     ; IRQ 1  - TECLADO
ISR_SEM_ERRO 34     ; IRQ 2  - cascata do PIC escravo
ISR_SEM_ERRO 35     ; IRQ 3
ISR_SEM_ERRO 36     ; IRQ 4
ISR_SEM_ERRO 37     ; IRQ 5
ISR_SEM_ERRO 38     ; IRQ 6
ISR_SEM_ERRO 39     ; IRQ 7
ISR_SEM_ERRO 40     ; IRQ 8
ISR_SEM_ERRO 41     ; IRQ 9
ISR_SEM_ERRO 42     ; IRQ 10
ISR_SEM_ERRO 43     ; IRQ 11
ISR_SEM_ERRO 44     ; IRQ 12
ISR_SEM_ERRO 45     ; IRQ 13
ISR_SEM_ERRO 46     ; IRQ 14
ISR_SEM_ERRO 47     ; IRQ 15

; --- Chamadas de sistema: int 0x80 (a única porta aberta ao anel 3) ---
section .text
global isr_syscall
isr_syscall:
    push dword 0
    push dword 0x80
    jmp isr_comum

; --- Corpo comum: salva o processo, chama o C, retoma (talvez OUTRO) processo
;
; Quadro completo na pilha do núcleo (struct quadro_t em kernel.c):
;   [GS FS ES DS][PUSHAD][vetor][erro][EIP CS EFLAGS][ESP SS]
; ESP e SS do usuário só existem quando a interrupção veio do anel 3.
;
; interrupt_dispatch() devolve o endereço do quadro a ser retomado. Se for
; o de outro processo, a troca de contexto acontece aqui: basta mudar ESP.
section .text
isr_comum:
    pushad                           ; EAX ECX EDX EBX ESP EBP ESI EDI
    push ds
    push es
    push fs
    push gs
    mov ax, DATA_SEG                 ; segmentos do núcleo
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld                              ; ABI C exige DF=0
    push esp                         ; argumento: ponteiro para o quadro
    call interrupt_dispatch
    mov esp, eax                     ; <- TROCA DE CONTEXTO
    pop gs
    pop fs
    pop es
    pop ds
    popad
    add esp, 8                       ; descarta vetor + código de erro
    iretd                            ; volta ao anel do processo escolhido

; --- Programas do usuário embutidos no kernel.bin --------------------------
; Cada um foi compilado à parte (build.sh) como binário puro para 0x40000000.
section .rodata
%macro PROGRAMA 2
global prog_%1_ini
global prog_%1_fim
align 4
prog_%1_ini:
    incbin %2
prog_%1_fim:
%endmacro

PROGRAMA video,   "build/video.bin"
PROGRAMA teclado, "build/teclado.bin"
PROGRAMA shell,   "build/shell.bin"

section .note.GNU-stack noalloc noexec nowrite progbits
