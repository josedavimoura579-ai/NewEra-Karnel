; ===========================================================================
;  NovaEra Microkernel - cpu.asm
;  As instruções especiais da CPU que o C não sabe escrever, como funções
;  comuns (ABI System V: argumentos em RDI, RSI; resultado em RAX).
;
;  Por que não usar __asm__ dentro do C? Porque o compilador do NEFS
;  (cproc + QBE) não aceita assembly embutido. Assim o núcleo compila
;  tanto com o clang quanto com o cproc.
;
;  Montar com:  nasm -f elf64 cpu.asm -o cpu.o
; ===========================================================================

bits 64
section .text

; void outb(u16 porta, u8 valor)
global outb
outb:
    mov dx, di
    mov al, sil
    out dx, al
    ret

; u8 inb(u16 porta)
global inb
inb:
    mov dx, di
    xor eax, eax
    in al, dx
    ret

; void cpu_carregar_cr3(u64 pml4)   - troca o espaço de endereçamento
global cpu_carregar_cr3
cpu_carregar_cr3:
    mov cr3, rdi
    ret

; u64 cpu_ler_cr2(void)              - endereço que causou a falha de página
global cpu_ler_cr2
cpu_ler_cr2:
    mov rax, cr2
    ret

; void cpu_carregar_idt(u64 base, u16 limite)
global cpu_carregar_idt
cpu_carregar_idt:
    sub rsp, 16
    mov [rsp], si                    ; o ponteiro da IDT tem 10 bytes:
    mov [rsp + 2], rdi               ; limite (2) + base (8), sem alinhamento
    lidt [rsp]
    add rsp, 16
    ret

; void cpu_carregar_tr(u16 seletor) - diz à CPU onde está a TSS
global cpu_carregar_tr
cpu_carregar_tr:
    ltr di
    ret

; void cpu_ocioso(void)              - liga as interrupções e dorme para sempre
global cpu_ocioso
cpu_ocioso:
    sti
.dorme:
    hlt
    jmp .dorme

; void cpu_parar(void)               - desliga as interrupções e para de vez
global cpu_parar
cpu_parar:
    cli
    hlt
    jmp cpu_parar

section .note.GNU-stack noalloc noexec nowrite progbits
