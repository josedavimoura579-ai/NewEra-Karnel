; ===========================================================================
;  NovaEra - usuario/sys.asm
;  As poucas instruções que o C não sabe escrever, para os programas do
;  usuário: a chamada de sistema (int 0x80), as portas de E/S e o acesso a
;  memória de dispositivo. Em assembly para compilar tanto com o clang
;  quanto com o compilador do NEFS (cproc + QBE), que não aceita __asm__.
;
;  Montar com:  nasm -f elf64 usuario/sys.asm -o sys.o
; ===========================================================================

bits 64

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

; u32 mem_le32(u64 endereco)
global mem_le32
mem_le32:
    mov eax, [rdi]
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
