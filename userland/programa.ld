/* programa.ld - como os programas do SaguiOS são ligados.
 * Endereço 0x40000000 (PRD 4); dados numa página separada do código. */
ENTRY(_inicio)
SECTIONS
{
    . = 0x40000000;
    .text : { *(.text.inicio) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    . = ALIGN(4096);
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.*) *(COMMON) }
    /* O mlibc upstream (ligação estática, LIBC=upstream) precisa destes
     * símbolos: __init_array_* para rodar os construtores (init_libc, que
     * parseia a pilha de entrada) e __ehdr_start para o rtld estático. */
    __init_array_start = .;
    .init.array : { *(.init.array) }
    __init_array_end = .;
    __preinit_array_start = .;
    __preinit_array_end = .;
    __ehdr_start = 0x40000000;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
