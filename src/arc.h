/*
 * arc.h -- archives ARC (SEA ARC 5.x) : en-tetes et methodes 1 a 4, 8 et 9.
 * Portable, teste sur l'hote contre tools/arcpack.py, lui-meme verifie
 * contre un decompresseur independant (unar). Bornes verifiees, CRC-16.
 */
#ifndef ARC_H
#define ARC_H

typedef struct {
    char name[13];
    int method;
    long packed, size;
    unsigned short date, time, crc;
    int hsize;                  /* 29, ou 25 pour la methode 1 */
} ARCHDR;

#define ARC_OK      0
#define ARC_END     1
#define ARC_BAD    -1
#define ARC_METHOD -2
#define ARC_CRC    -3

#define ARC_WORK_BYTES 43000    /* tables LZW (13 bits) et pile */

/* En-tete dans h[n] (au moins 29 octets s'ils existent). */
int arc_header(const unsigned char *h, long n, ARCHDR *a);
int arc_method_ok(int method);
/* Decompresse in[insize] dans out[outsize] ; CRC-16 verifie. */
int arc_extract(int method, const unsigned char *in, long insize,
                unsigned char *out, long outsize, unsigned short crc, void *work);

#endif
