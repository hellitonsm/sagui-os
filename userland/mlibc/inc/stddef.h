/* stddef.h - espelho mlibc adaptado ao SaguiOS.
 * O mlibc declara size_t/NULL/wchar_t; no SaguiOS os tipos base ja estao em
 * sagui.h, entao este cabecalho so repassa. */
#ifndef MLIBC_STDDEF_H
#define MLIBC_STDDEF_H
#include "sagui.h"
typedef char wchar_t;
#define offsetof(t, m) __builtin_offsetof(t, m)
#endif
