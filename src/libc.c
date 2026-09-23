/*
 * libc.c -- fonctions de chaine et de memoire. GCC en emet aussi lui-meme
 * (copie de structures, mise a zero de tableaux) : elles doivent exister.
 */
#include "libc.h"

#ifndef TOSFC_HOST
void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;
    /* Copie par longs quand les deux adresses sont paires : le 68000
     * refuse un acces mot/long a une adresse impaire. */
    if ((((unsigned long)dp | (unsigned long)sp) & 1) == 0) {
        while (n >= 4) {
            *(unsigned long *)dp = *(const unsigned long *)sp;
            dp += 4; sp += 4; n -= 4;
        }
    }
    while (n--) *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;
    if (dp <= sp || dp >= sp + n) return memcpy(d, s, n);
    dp += n; sp += n;
    while (n--) *--dp = *--sp;
    return d;
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *dp = d;
    while (n--) *dp++ = (unsigned char)c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y) return *x - *y;
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++) != 0) {}
    return r;
}

char *strcat(char *d, const char *s)
{
    strcpy(d + strlen(d), s);
    return d;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (!*a) return 0;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return 0;
    }
}

char *strrchr(const char *s, int c)
{
    const char *r = 0;
    for (;; s++) {
        if (*s == (char)c) r = s;
        if (!*s) return (char *)r;
    }
}

int toupper(int c)
{
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}
#endif

int str_copy(char *d, const char *s, int cap)
{
    int i;
    for (i = 0; i < cap - 1 && s[i]; i++) d[i] = s[i];
    d[i] = 0;
    return s[i] ? -1 : 0;
}

int str_add(char *d, const char *s, int cap)
{
    int l = (int)strlen(d);
    if (l >= cap) return -1;
    return str_copy(d + l, s, cap - l);
}

int fmt_ulong(char *out, unsigned long v, char sep)
{
    /* Par soustractions : le 68000 n'a pas de division 32 bits, et la
     * version logicielle de libgcc coute des centaines de cycles. */
    static const unsigned long pow10[] = {
        1000000000UL, 100000000UL, 10000000UL, 1000000UL, 100000UL,
        10000UL, 1000UL, 100UL, 10UL, 1UL };
    int i, n = 0, started = 0;
    for (i = 0; i < 10; i++) {
        char d = '0';
        while (v >= pow10[i]) { v -= pow10[i]; d++; }
        if (d != '0' || started || i == 9) {
            if (started && sep && (i == 1 || i == 4 || i == 7)) out[n++] = sep;
            out[n++] = d;
            started = 1;
        }
    }
    out[n] = 0;
    return n;
}

void pad_left(char *out, const char *s, int w)
{
    int l = (int)strlen(s), i;
    int p = w - l;
    if (p < 0) p = 0;
    for (i = 0; i < p; i++) out[i] = ' ';
    strcpy(out + p, s);
}
