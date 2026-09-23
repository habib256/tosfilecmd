/*
 * picture.h -- images ST : Degas (PI1-3), Degas Elite compresse (PC1-3),
 * NEOchrome (NEO). Decodage et conversions entre moniteurs.
 *
 * Code portable (teste sur l'hote, tests/test_view.c) : il ne lit que le
 * tampon qu'on lui donne et n'ecrit que dans le bitmap de 32000 octets de
 * sortie. Un fichier malforme donne une erreur, jamais un debordement.
 */
#ifndef PICTURE_H
#define PICTURE_H

#define PIC_BYTES 32000L

enum { PIC_LOW = 0, PIC_MED = 1, PIC_HIGH = 2 };

typedef struct {
    int res;                    /* PIC_LOW, PIC_MED ou PIC_HIGH */
    unsigned short pal[16];     /* registres de couleur ST/STE */
    const char *format;         /* "Degas", "Degas Elite", "NEOchrome" */
} PICINFO;

/* Resultats de pic_decode. */
#define PIC_OK        0
#define PIC_UNKNOWN  -1         /* pas une image reconnue */
#define PIC_BAD      -2         /* reconnue mais tronquee ou incoherente */

/* 1 si le nom (8.3) porte une extension d'image connue. */
int pic_is_picture_name(const char *name);

/* Decode data (size octets, nom pour l'extension) dans out[PIC_BYTES]. */
int pic_decode(const char *name, const unsigned char *data, long size,
               PICINFO *info, unsigned char *out);

/* Une image couleur (basse ou moyenne) pour un moniteur monochrome :
 * 640 x 400, tramage ordonne sur la luminance de chaque couleur. */
void pic_to_mono(const PICINFO *info, const unsigned char *in, unsigned char *out);

/* Une image monochrome pour un moniteur couleur : moyenne resolution,
 * chaque paire de lignes donne blanc, gris ou noir (palette pic_gray_pal). */
void pic_mono_to_medium(const unsigned char *in, unsigned char *out);
extern const unsigned short pic_gray_pal[4];

/* Indice de couleur du pixel (x, y) d'un bitmap planaire. */
int pic_pixel(int res, const unsigned char *bm, int x, int y);

#endif
