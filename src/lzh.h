/*
 * lzh.h -- archives LHA : en-tetes (niveaux 0, 1, 2) et methodes -lh0- et
 * -lh5- (celle des fichiers YM). Portable, teste sur l'hote contre
 * tools/lha.py et lhasa. Un fichier malforme donne une erreur, jamais un
 * acces hors des tampons ; le CRC-16 de chaque fichier est verifie.
 */
#ifndef LZH_H
#define LZH_H

typedef struct {
    char name[64];
    char method[6];             /* "-lh5-" ... */
    long packed, size;
    long data;                  /* offset des donnees dans l'archive */
    long next;                  /* offset de l'en-tete suivant */
    unsigned short crc;
} LZHENTRY;

#define LZH_OK       0
#define LZH_END      1          /* plus d'en-tete */
#define LZH_BAD     -1          /* archive malformee ou donnees corrompues */
#define LZH_METHOD  -2          /* methode non geree */
#define LZH_CRC     -3          /* CRC faux */

/* Memoire de travail du decompresseur (tables de Huffman). */
#define LZH_WORK_BYTES 14336

/* 1 si les octets ressemblent a une archive LHA (en-tete "-lh?-"). */
int lzh_is_archive(const unsigned char *a, long n);
/* En-tete a l'offset off : LZH_OK (e rempli), LZH_END ou LZH_BAD. */
int lzh_entry(const unsigned char *a, long n, long off, LZHENTRY *e);
/* Decompresse e dans out[e->size] ; work : LZH_WORK_BYTES octets. */
int lzh_extract(const unsigned char *a, long n, const LZHENTRY *e,
                unsigned char *out, void *work);

#endif
