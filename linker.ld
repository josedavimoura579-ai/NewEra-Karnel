/* NovaEra Microkernel - linker.ld
 * Carrega o kernel em 1 MiB; .multiboot precisa ficar nos primeiros 8 KiB. */
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

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss .bss.*)
    }

    /* Daqui para cima, a RAM é do gerenciador de memória física. */
    _kernel_fim = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
