; inicio.asm - ponto de entrada de todo programa do SaguiOS.
;
; O kernel começa o programa com o layout ELF padrão: [esp] = argc e
; [esp+4] = o vetor argv inline ([esp+4], [esp+8], ... terminado em NULL),
; seguido de [envp] = NULL e o vetor auxiliar ELF (ver kernel/nucleo/
; carregador.c). Aqui calculamos o ENDEREÇO do vetor (lea), não o valor
; lido em [esp+4] (que é argv[0]).
section .text.inicio
global _inicio
extern principal
extern sair

_inicio:
        xor ebp, ebp                    ; fim da cadeia de quadros
        mov eax, [esp]
        lea ecx, [esp + 4]              ; endereço do vetor argv (inline)
        and esp, ~15                    ; pilha alinhada para o C
        push ecx
        push eax
        call principal
        push eax
        call sair
.nunca: jmp .nunca
