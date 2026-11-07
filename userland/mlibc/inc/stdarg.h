/* stdarg.h - espelho mlibc (va_list ja veem de sagui.h). */
#ifndef MLIBC_STDARG_H
#define MLIBC_STDARG_H
#include "sagui.h"
#ifndef va_copy
# define va_copy(destino, origem) __builtin_va_copy(destino, origem)
#endif
#endif
