; ===========================================================================
;  NovaEra - usuario/crt0.asm
;  O começo de todo programa do usuário e as poucas instruções que o C não
;  sabe escrever. Fica em assembly para que os programas compilem tanto
;  com o clang quanto com o compilador do NEFS (cproc + QBE).
;
;  Montar com:  nasm -f elf64 usuario/crt0.asm -o crt0.o
; ===========================================================================

bits 64

; O núcleo começa a executar no primeiro byte do programa (0x40000000),
; que é este _start, com RBX e RCX como argumentos. Se main() voltar,
; o processo termina. A pilha já chega alinhada em 16 bytes.
section .text.entrada progbits alloc exec nowrite
global _start
extern main
_start:
    mov rdi, rbx                     ; 1º argumento de main (ABI System V)
    mov rsi, rcx                     ; 2º argumento
    call main
    mov eax, 7                       ; SYS_SAIR
    int 0x80
.fim:
    jmp .fim

section .text

; long syscall2(u64 numero, u64 a, u64 b)
; Convenção do NovaEra: RAX = número, RBX = a, RCX = b; resultado em RAX.
global syscall2
syscall2:
    push rbx                         ; RBX pertence a quem chamou: preserva
    mov rax, rdi
    mov rbx, rsi
    mov rcx, rdx
    int 0x80
    pop rbx
    ret

; void outb(u16 porta, u8 valor)    - só drivers (IOPL 3)
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

section .note.GNU-stack noalloc noexec nowrite progbits
