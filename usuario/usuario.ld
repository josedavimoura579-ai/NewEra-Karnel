/* NovaEra - usuario/usuario.ld
 * Todo programa do usuário é um ELF de 64 bits que mora a partir de
 * 0x40000000 (ver novaera.h). O núcleo lê os segmentos do ELF, copia cada
 * um para o seu endereço e começa a executar em _start. */
ENTRY(_start)

SECTIONS
{
    . = 0x40000000;

    .text : {
        KEEP(*(.text.entrada))
        *(.text .text.*)
    }
    .rodata ALIGN(4K) : { *(.rodata .rodata.*) }
    .data   ALIGN(4K) : { *(.data .data.*) *(.got .got.*) }   /* .got: ver linker.ld */
    .bss    : { *(COMMON) *(.bss .bss.*) }   /* o núcleo entrega a memória zerada */

    _fim = .;
    ASSERT(_fim <= 0x50000000, "programa maior que 256 MiB (USUARIO_PROG_FIM)")

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
