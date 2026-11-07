/* ctype.c - funcoes de classificacao ASCII, locale C fixo. */
#include <ctype.h>

static int ascii(int c) { return c == -1 ? -1 : (unsigned char)c; }
int isdigit(int c)  { c = ascii(c); return c >= '0' && c <= '9'; }
int islower(int c)  { c = ascii(c); return c >= 'a' && c <= 'z'; }
int isupper(int c)  { c = ascii(c); return c >= 'A' && c <= 'Z'; }
int isalpha(int c)  { return islower(c) || isupper(c); }
int isalnum(int c)  { return isalpha(c) || isdigit(c); }
int isspace(int c)
{
    c = ascii(c);
    return c == ' ' || (c >= '\t' && c <= '\r');
}
int isblank(int c)  { c = ascii(c); return c == ' ' || c == '\t'; }
int iscntrl(int c)  { c = ascii(c); return c < 32 || c == 127; }
int isprint(int c)  { c = ascii(c); return c >= 32 && c <= 126; }
int isgraph(int c)  { c = ascii(c); return c >= 33 && c <= 126; }
int ispunct(int c)  { return isgraph(c) && !isalnum(c); }
int isascii(int c)  { return (unsigned)c <= 0x7f; }
int isxdigit(int c)
{
    c = ascii(c);
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
int tolower(int c)  { c = ascii(c); return isupper(c) ? c + ('a' - 'A') : c; }
int toupper(int c)  { c = ascii(c); return islower(c) ? c - ('a' - 'A') : c; }
