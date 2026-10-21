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

; void mem_escreve16(u64 endereco, u16 valor)
; u16  mem_le16(u64 endereco)
; Acesso a memória de dispositivo (como a VGA em 0xB8000). Em C isso exigiria
; "volatile", que o cproc ainda não suporta; numa função em assembly nenhum
; compilador consegue apagar ou reordenar o acesso.
global mem_escreve16
mem_escreve16:
    mov [rdi], si
    ret

global mem_le16
mem_le16:
    movzx eax, word [rdi]
    ret

; void cpu_fpu_ligar(void)
; Liga a FPU e o SSE (os registradores de ponto flutuante dos programas):
; CR0.EM = 0 (sem emulação), CR0.MP = 1, CR0.TS = 0; CR4.OSFXSR e
; CR4.OSXMMEXCPT = 1 (o sistema sabe salvar o SSE com FXSAVE).
global cpu_fpu_ligar
cpu_fpu_ligar:
    mov rax, cr0
    and rax, ~((1 << 2) | (1 << 3))
    or rax, 1 << 1
    mov cr0, rax
    mov rax, cr4
    or rax, (1 << 9) | (1 << 10)
    mov cr4, rax
    fninit
    ret

; void cpu_fpu_salvar(u64 area) / cpu_fpu_restaurar(u64 area)
; Os 512 bytes de estado da FPU + SSE de um processo (área alinhada em 16).
global cpu_fpu_salvar
cpu_fpu_salvar:
    fxsave [rdi]
    ret

global cpu_fpu_restaurar
cpu_fpu_restaurar:
    fxrstor [rdi]
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
