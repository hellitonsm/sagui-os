; Exemplo para /bin/asagui: syscall de escrita e encerramento do SaguiOS.
bits 32
global _start
section .text

_start:
    mov eax, 12          ; SIS_ESCREVER
    mov ebx, 1           ; descritor SAIDA
    mov ecx, mensagem    ; endereco absoluto do rotulo
    mov edx, 15          ; tamanho em bytes, incluindo LF
    int 0x80

    mov ebx, 0           ; codigo de saida
    mov eax, 3            ; SIS_SAIR
    int 0x80

section .rodata
mensagem: db "[ASM] montado!", 10
