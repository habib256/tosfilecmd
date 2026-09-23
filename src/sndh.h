/*
 * sndh.h -- en-tete des fichiers SNDH : code 68000 d'un lecteur de musique,
 * trois points d'entree (init +0, exit +4, play +8), puis "SNDH" et des
 * etiquettes jusqu'a "HDNS". Portable, teste sur l'hote.
 */
#ifndef SNDH_H
#define SNDH_H

typedef struct {
    char title[48], composer[48], year[8];
    int tunes;                  /* sous-morceaux (1 par defaut) */
    int hz;                     /* frequence d'appel de play */
    char timer;                 /* 'A', 'B', 'C', 'D' ou 'V' (VBL) */
} SNDHINFO;

#define SNDH_OK   0
#define SNDH_NOT -1

/* data : le fichier SNDH decompresse. */
int sndh_parse(const unsigned char *d, long size, SNDHINFO *s);

#endif
