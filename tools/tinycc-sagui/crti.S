/* crti.S - crti.o vazio: o SaguiOS nao usa .init/.fini, so .init_array
 * (que o tcc emite com os simbolos __init_array_* corretos). */
.text
.globl _crti
_crti:
