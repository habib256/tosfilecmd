/*
 * inflate.h -- decompression deflate (RFC 1951), celle des fichiers ZIP.
 * Portable, testee sur l'hote contre zlib. Sortie en un bloc de taille
 * connue ; un flux malforme donne une erreur, jamais un acces hors tampon.
 */
#ifndef INFLATE_H
#define INFLATE_H

#define INFLATE_WORK_BYTES 2800         /* tables de Huffman */

/* 0 si out[outsize] est rempli exactement, -1 sinon. */
int inflate_raw(const unsigned char *in, long insize, unsigned char *out, long outsize,
                void *work);

/* CRC-32 (ZIP), a enchainer : crc = crc32_update(crc, p, n), depart 0. */
unsigned long crc32_update(unsigned long crc, const unsigned char *p, long n);

#endif
