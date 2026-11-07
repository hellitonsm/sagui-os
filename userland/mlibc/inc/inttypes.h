/* inttypes.h - tipos inteiros de tamanho exato e macros de conversao. */
#ifndef SAGUI_MLIBC_INTTYPES_H
#define SAGUI_MLIBC_INTTYPES_H
#include <stdint.h>
#include <stddef.h>

typedef int64_t  intmax_t;
typedef uint64_t uintmax_t;

#define INT8_MAX   0x7F
#define UINT8_MAX  0xFF
#define INT16_MAX  0x7FFF
#define UINT16_MAX 0xFFFF
#define INT32_MAX  0x7FFFFFFF
#define UINT32_MAX 0xFFFFFFFFU
#define INT64_MAX  0x7FFFFFFFFFFFFFFFLL
#define UINT64_MAX 0xFFFFFFFFFFFFFFFFULL

#define PRId8  "d"
#define PRId16 "d"
#define PRId32 "d"
#define PRId64 "lld"
#define PRIi8  "d"
#define PRIi16 "d"
#define PRIi32 "d"
#define PRIi64 "lld"
#define PRIu8  "u"
#define PRIu16 "u"
#define PRIu32 "u"
#define PRIu64 "llu"
#define PRIx8  "x"
#define PRIx16 "x"
#define PRIx32 "x"
#define PRIx64 "llx"
#define PRIX8  "X"
#define PRIX16 "X"
#define PRIX32 "X"
#define PRIX64 "llX"
#define PRIo8  "o"
#define PRIo16 "o"
#define PRIo32 "o"
#define PRIo64 "llo"
#define PRIxPTR "x"
#define PRIuPTR "u"
#define PRIdPTR "d"
#define PRIiPTR PRIdPTR

#define SCNd8  "hhd"
#define SCNd16 "hd"
#define SCNd32 "d"
#define SCNd64 "lld"
#define SCNu32 "u"
#define SCNu64 "llu"

#define INT8_C(x)    x
#define INT16_C(x)   x
#define INT32_C(x)   x
#define INT64_C(x)   x ## LL
#define INTMAX_C(x)  x ## LL
#define UINT8_C(x)   x
#define UINT16_C(x)  x
#define UINT32_C(x)  x
#define UINT64_C(x)  x ## ULL
#define UINTMAX_C(x) x ## ULL

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#endif
