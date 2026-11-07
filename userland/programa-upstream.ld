/* programa-upstream.ld - ligacao de programas contra o mlibc upstream.
 * Mesmo layout de programa.ld, mas o ponto de entrada e' _start (crt1.S do
 * mlibc), nao _inicio (inicio.asm da libsagui). */
ENTRY(_start)
SECTIONS
{
    . = 0x40000000;
    .text : { *(.text.inicio) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    . = ALIGN(4096);
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.*) *(COMMON) }
    __init_array_start = .;
    .init.array : { *(.init.array) }
    __init_array_end = .;
    __fini_array_start = .;
    .fini.array : { *(.fini.array) }
    __fini_array_end = .;
    __preinit_array_start = .;
    __preinit_array_end = .;
    __ehdr_start = 0x40000000;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
