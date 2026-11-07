/* setjmp.h - saltos nao locais i386 do SaguiOS (ver setjmp.asm). */
#ifndef SAGUI_SETJMP_H
#define SAGUI_SETJMP_H

typedef unsigned long jmp_buf[6];

int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int valor) __attribute__((noreturn));

#endif
