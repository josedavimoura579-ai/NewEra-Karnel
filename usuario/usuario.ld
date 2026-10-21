/* NovaEra - usuario/usuario.ld
 * Todo programa do usuário mora em 0x40000000 (ver novaera.h) e vira um
 * binário puro: o núcleo copia os bytes e pula para o primeiro deles. */
ENTRY(_start)

SECTIONS
{
    . = 0x40000000;

    .text : {
        KEEP(*(.text.entrada))      /* _start PRECISA ser o primeiro byte */
        *(.text .text.*)
    }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    .bss    : { *(COMMON) *(.bss .bss.*) }   /* o núcleo entrega a memória zerada */

    _fim = .;
    ASSERT(_fim <= 0x40010000, "programa maior que 64 KiB (USUARIO_IMG_TAM)")

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
