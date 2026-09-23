/*
 * libc.h -- le strict necessaire de la bibliotheque C, sans dependance.
 * Sur l'hote (tests), ces noms sont ceux de la vraie libc.
 */
#ifndef LIBC_H
#define LIBC_H

#ifdef TOSFC_HOST
#include <string.h>
#include <ctype.h>
#else
typedef unsigned long size_t;
void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
void  *memset(void *d, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
char  *strcpy(char *d, const char *s);
char  *strcat(char *d, const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
int    toupper(int c);
#endif

/* Copie bornee, toujours terminee par 0 ; renvoie 0 si tout a tenu. */
int str_copy(char *d, const char *s, int cap);
/* Ajout borne ; renvoie 0 si tout a tenu. */
int str_add(char *d, const char *s, int cap);

/* Nombre decimal, avec separateur de milliers si sep != 0. Renvoie la longueur. */
int fmt_ulong(char *out, unsigned long v, char sep);
/* Complete a gauche par des espaces jusqu'a w caracteres. */
void pad_left(char *out, const char *s, int w);

#endif
