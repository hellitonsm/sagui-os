/* assert.h - verificacao de condicoes internas do SaguiOS. */
#ifndef SAGUI_MLIBC_ASSERT_H
#define SAGUI_MLIBC_ASSERT_H

void __sagui_assert_fail(const char *expressao, const char *arquivo,
                         int linha) __attribute__((noreturn));

#ifdef NDEBUG
# define assert(expressao) ((void)0)
#else
# define assert(expressao) \
    ((void)((expressao) ? 0 : (__sagui_assert_fail(#expressao, __FILE__, __LINE__), 0)))
#endif

#endif
