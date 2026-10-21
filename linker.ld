/* NovaEra Microkernel - linker.ld
 * Carrega o kernel em 1 MiB; .multiboot precisa ficar nos primeiros 8 KiB.
 * O kernel.bin é um binário puro: o arquivo é a memória a partir de 1 MiB,
 * e o cabeçalho Multiboot diz onde ele termina (_carga_fim, _kernel_fim). */
ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text ALIGN(4K) : {
        KEEP(*(.multiboot))
        *(.text .text.*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4K) : {
        *(.data .data.*)
    }

    _carga_fim = .;

    .bss ALIGN(4K) : {
        _bss_ini = .;
        *(COMMON)
        *(.bss .bss.*)
    }

    /* Daqui para cima, a RAM é do gerenciador de memória física. */
    _kernel_fim = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
