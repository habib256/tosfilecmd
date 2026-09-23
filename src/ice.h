/*
 * ice.h -- decompression ICE! 2.4 (et "Ice!" 2.35), le compresseur de la
 * plupart des fichiers SNDH. Portable, teste sur l'hote contre le
 * compresseur d'unice68 (tests/ext/unice68). Un fichier malforme donne une
 * erreur sans jamais lire ni ecrire hors des tampons.
 */
#ifndef ICE_H
#define ICE_H

/* Taille decompressee si data commence par un en-tete ICE!, -1 sinon. */
long ice_size(const unsigned char *data, long size);
/* Decompresse data dans out[outsize] (outsize = ice_size). 0 ou -1. */
int ice_unpack(const unsigned char *data, long size, unsigned char *out, long outsize);

#endif
