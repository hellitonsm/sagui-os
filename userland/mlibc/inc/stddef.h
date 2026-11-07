/* stddef.h - tipos fundamentais. size_t/NULL ja sao definidos em sagui.h. */
#ifndef SAGUI_MLIBC_STDDEF_H
#define SAGUI_MLIBC_STDDEF_H
#include "sagui.h"
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __WCHAR_TYPE__ wchar_t;
typedef __INTPTR_TYPE__  intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif
