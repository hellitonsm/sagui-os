; crt1-sagui.asm - ponto de entrada para programas C ligados contra o
; sysroot que o tinycc instala em /usr (libc Sagui + crt1). O kernel comeca
; o programa com [esp] = argc e [esp+4] = argv (layout ELF padrao); o crt1
; chama main(argc, argv) e encerra com o valor devolvido. A convencao dos
; programas do SaguiOS (principal) e coberta pelo inicio.asm da libsagui.
section .text.inicio
global _start
extern main
extern sair

_start:
        xor ebp, ebp                    ; fim da cadeia de quadros
        mov eax, [esp]                  ; argc
        lea ecx, [esp + 4]              ; endereco do vetor argv
        and esp, ~15                    ; pilha alinhada para o C
        push ecx
        push eax
        call main
        push eax
        call sair
.nunca: jmp .nunca

section .note.GNU-stack noalloc noexec nowrite progbits
