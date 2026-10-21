; ===========================================================================
;  NovaEra Microkernel v3.0 (x86_64) - boot.asm
;
;  O bootloader Multiboot sempre entrega a CPU em modo protegido de 32 bits.
;  Este arquivo é a ponte até os 64 bits ("modo longo"):
;    1. confere se a CPU suporta 64 bits (CPUID)
;    2. monta tabelas de página que mapeiam o 1º GiB (páginas de 2 MiB)
;    3. liga PAE, o modo longo (EFER.LME) e a paginação
;    4. salta para o código de 64 bits e chama kernel_main() em C
;
;  Também ficam aqui: a GDT (anéis 0 e 3 + TSS), os stubs de interrupção,
;  a porta das chamadas de sistema (int 0x80) e a troca de contexto.
;
;  Montar com:  nasm -f elf64 boot.asm -o boot.o
; ===========================================================================

; --- Cabeçalho Multiboot 1 com endereços explícitos ------------------------
; O bit 16 das flags diz ao carregador: "não leia o ELF, copie o arquivo
; para estes endereços". Assim um kernel de 64 bits carrega pelo Multiboot
; (que só entende ELF de 32 bits), tanto no QEMU quanto no GRUB.
MB_MAGIC     equ 0x1BADB002
MB_FLAGS     equ 0x00010003
MB_CHECKSUM  equ -(MB_MAGIC + MB_FLAGS)

CODE_SEG     equ 0x08
DATA_SEG     equ 0x10
STACK_SIZE   equ 16384

extern _carga_fim                    ; linker.ld: fim de .text/.rodata/.data
extern _kernel_fim                   ; linker.ld: fim do .bss

section .multiboot progbits alloc noexec nowrite align=4
bits 32
align 4
mb_cabecalho:
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM
    dd mb_cabecalho                  ; header_addr
    dd mb_cabecalho                  ; load_addr  (o arquivo começa aqui)
    dd _carga_fim                    ; load_end_addr
    dd _kernel_fim                   ; bss_end_addr (o carregador zera o .bss)
    dd _start                        ; entry_addr

; --- Pilha do núcleo e tabelas de página do boot ---------------------------
section .bss nobits alloc noexec write align=4096
global pml4_boot
global pd_boot
pml4_boot:  resb 4096                ; nível 4: cada entrada cobre 512 GiB
pdpt_boot:  resb 4096                ; nível 3: cada entrada cobre 1 GiB
pd_boot:    resb 4096                ; nível 2: 512 páginas de 2 MiB = 1 GiB
stack_bottom:
    resb STACK_SIZE
stack_top:

; --- GDT de 64 bits --------------------------------------------------------
; No modo longo a segmentação quase some: base e limite são ignorados.
; Os descritores servem para dizer o ANEL (0 ou 3) e que o código é 64 bits
; (bit L). A proteção de memória de verdade vem da paginação.
section .data
align 16
global gdt_tss
gdt_start:
    dq 0x0000000000000000            ; 0x00: nulo
    dq 0x00AF9A000000FFFF            ; 0x08: código do núcleo  (anel 0, 64 bits)
    dq 0x00CF92000000FFFF            ; 0x10: dados do núcleo   (anel 0)
    dq 0x00AFFA000000FFFF            ; 0x18: código do usuário (anel 3, 64 bits) -> 0x1B
    dq 0x00CFF2000000FFFF            ; 0x20: dados do usuário  (anel 3) -> 0x23
gdt_tss:
    dq 0, 0                          ; 0x28: TSS de 64 bits ocupa 16 bytes (tss_iniciar)
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dq gdt_start

; --- Ponto de entrada (ainda em 32 bits) -----------------------------------
section .text
bits 32
global _start
extern kernel_main
extern interrupt_dispatch

_start:
    cli
    cld
    mov esp, stack_top
    mov edi, eax                     ; magic Multiboot
    mov esi, ebx                     ; ponteiro para a info Multiboot

    ; A CPU fala 64 bits? CPUID 0x80000001, EDX bit 29 = "Long Mode".
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .sem_64_bits
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .sem_64_bits

    ; Tabelas de página: PML4[0] -> PDPT[0] -> PD com 512 páginas de 2 MiB.
    ; Endereço virtual = físico no 1º GiB. Só o anel 0 enxerga (bit U=0).
    mov eax, pdpt_boot
    or eax, 0x3                      ; presente + escrita
    mov [pml4_boot], eax
    mov eax, pd_boot
    or eax, 0x3
    mov [pdpt_boot], eax

    xor ecx, ecx
.mapeia_2mb:
    mov eax, ecx
    shl eax, 21                      ; ecx * 2 MiB
    or eax, 0x83                     ; presente + escrita + página grande (PS)
    mov [pd_boot + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .mapeia_2mb

    mov eax, pml4_boot
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5                   ; PAE: obrigatório no modo longo
    mov cr4, eax

    mov ecx, 0xC0000080              ; MSR EFER
    rdmsr
    or eax, 1 << 8                   ; LME: Long Mode Enable
    wrmsr

    mov eax, cr0
    or eax, 1 << 31                  ; PG: liga a paginação -> modo longo ativo
    mov cr0, eax

    lgdt [gdt_descriptor]
    jmp CODE_SEG:inicio_64           ; recarrega CS com um segmento de 64 bits

.sem_64_bits:
    ; Escreve direto na VGA (ainda não existe nada mais) e para.
    mov esi, msg_sem_64
    mov edi, 0xB8000
.escreve:
    lodsb
    test al, al
    jz .parar
    mov ah, 0x4F                     ; branco sobre vermelho
    stosw
    jmp .escreve
.parar:
    cli
    hlt
    jmp .parar

section .rodata
msg_sem_64: db "NovaEra: esta CPU nao suporta 64 bits. Use qemu-system-x86_64.", 0

; --- Agora em 64 bits ------------------------------------------------------
section .text
bits 64
inicio_64:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax

    push qword 0                     ; zera RFLAGS (IF=0)
    popfq

    mov edi, edi                     ; a metade de cima dos registradores fica
    mov esi, esi                     ; indefinida na troca de modo: zera
    mov rsp, stack_top               ; pilha alinhada em 16 bytes
    call kernel_main                 ; kernel_main(magic, mbi) - RDI, RSI

.parar:
    cli
    hlt
    jmp .parar

; ===========================================================================
;  Stubs de interrupção
;
;  No modo longo a CPU SEMPRE empilha SS, RSP, RFLAGS, CS e RIP (mesmo sem
;  troca de anel). Exceções SEM código de erro empilham um 0 falso, para
;  que o quadro seja idêntico para todos os vetores (quadro_t em kernel.c).
; ===========================================================================

; Tabela com o endereço de cada stub (lida pelo C para montar a IDT).
section .rodata
align 8
global isr_tabela
isr_tabela:

%macro ISR_SEM_ERRO 1
section .text
isr%1:
    push qword 0                     ; código de erro falso
    push qword %1                    ; número do vetor
    jmp isr_comum
section .rodata
    dq isr%1
%endmacro

%macro ISR_COM_ERRO 1
section .text
isr%1:
    push qword %1                    ; a CPU já empilhou o código de erro
    jmp isr_comum
section .rodata
    dq isr%1
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
    push qword 0
    push qword 0x80
    jmp isr_comum

; --- Corpo comum: salva o processo, chama o C, retoma (talvez OUTRO) processo
;
; Quadro completo na pilha do núcleo (struct quadro_t em kernel.c):
;   [R15 ... RAX][vetor][erro][RIP CS RFLAGS RSP SS]
; São 22 palavras de 8 bytes: como a CPU alinha a pilha em 16 antes de
; empilhar, a chamada ao C também fica alinhada.
;
; interrupt_dispatch() devolve o endereço do quadro a ser retomado. Se for
; o de outro processo, a troca de contexto acontece aqui: basta mudar RSP.
isr_comum:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    cld                              ; ABI C exige DF=0
    mov rdi, rsp                     ; argumento: ponteiro para o quadro
    call interrupt_dispatch
    mov rsp, rax                     ; <- TROCA DE CONTEXTO
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16                      ; descarta vetor + código de erro
    iretq                            ; volta ao anel do processo escolhido

; Os programas do usuário são embutidos por build/programas.asm, gerado
; pelo build.sh a partir de programas.lista.

section .note.GNU-stack noalloc noexec nowrite progbits
