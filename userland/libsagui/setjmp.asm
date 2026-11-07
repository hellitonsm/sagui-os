; setjmp.asm - setjmp/longjmp para i386 no SaguiOS.
;
; O tinycc usa estes saltos para sair da compilacao quando ha um erro (ver
; tcc.h: jmp_buf error_jmp_buf). jmp_buf guarda os registradores callee-saved
; do i686 (EBX, ESI, EDI, EBP) e o estado da pilha (ESP/EIP).

section .text
global setjmp
global longjmp

; int setjmp(jmp_buf env)  -> 0 na vez normal, valor de longjmp na volta
setjmp:
        mov     eax, [esp + 4]      ; env
        mov     [eax + 0],  ebx
        mov     [eax + 4],  esi
        mov     [eax + 8],  edi
        mov     [eax + 12], ebp
        mov     [eax + 16], esp     ; ESP do chamador (estado na volta)
        mov     ecx, [esp]          ; endereco de retorno do chamador
        mov     [eax + 20], ecx
        xor     eax, eax
        ret

; void longjmp(jmp_buf env, int valor)
longjmp:
        mov     edx, [esp + 4]      ; env
        mov     eax, [esp + 8]      ; valor
        test    eax, eax
        jnz     .valor_ok
        mov     eax, 1
.valor_ok:
        mov     ebx, [edx + 0]
        mov     esi, [edx + 4]
        mov     edi, [edx + 8]
        mov     ebp, [edx + 12]
        mov     esp, [edx + 16]
        jmp     [edx + 20]

section .note.GNU-stack noalloc noexec nowrite progbits
