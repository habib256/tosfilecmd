/*
 * vfs.h -- une image disque ou une archive ouverte comme un dossier, en
 * lecture seule.
 *
 *   images  : .ST (secteurs bruts), .MSA (pistes compressees), FAT12 ;
 *   archives: .LZH/.LHA (-lh0-, -lh5-), .ZIP (stocke, deflate), .ARC.
 *
 * A l'ouverture, toute l'arborescence est lue et bornee (1024 entrees,
 * 16 niveaux) ; les noms sont ramenes en 8.3, sans doublon dans un dossier.
 * Les fichiers se lisent par la SOURCE de fsops.h : une extraction est une
 * copie comme une autre (creation exclusive, TOSFC.BAK, relecture). Les
 * images se lisent secteur par secteur (une 720 Ko ne tiendrait pas en
 * memoire sur un 520 ST) ; un fichier d'archive est decompresse en memoire
 * a son ouverture, CRC verifie. Portable, teste sur l'hote.
 */
#ifndef VFS_H
#define VFS_H

#include "fsops.h"

enum { VK_NONE, VK_IMAGE, VK_LZH, VK_ZIP, VK_ARC };

#define VFS_MAX 1024

typedef struct {
    char name[13];
    unsigned char attr;
    unsigned short time, date;
    unsigned long size;         /* taille decompressee */
    short parent;               /* -1 : racine */
    unsigned char method;
    unsigned char flags;
    long pos;                   /* image : 1er cluster ; archive : offset des donnees */
    long packed;
    unsigned long crc;
} VENT;

typedef struct VFS VFS;

/* 1 si le nom est celui d'une image ou d'une archive connue. */
int vfs_kind_of_name(const char *name);
/* Ouvre path (un fichier GEMDOS) comme dossier virtuel de racine root
 * ("A:\DIR\DISK.ST\") ; 0 ou erreur. La structure est allouee. */
long vfs_open(VFS **out, const char *path, const char *root);
void vfs_close(VFS *v);
const char *vfs_root(const VFS *v);
const char *vfs_describe(const VFS *v);  /* "ST image, 720 KB" ... */
/* La source pour fsops et les visionneuses. */
const SOURCE *vfs_source(VFS *v);
/* 1 si le chemin (complet) est dans ce dossier virtuel. */
int vfs_contains(const VFS *v, const char *path);

/* Conversion d'un nom quelconque en 8.3 (majuscules, caracteres interdits
 * remplaces), unique parmi taken(ctx, nom) ; out[13]. */
void vfs_name83(const char *in, char *out, int (*taken)(void *ctx, const char *n), void *ctx);

/* Piste MSA compressee (RLE $E5) z[len] -> out[want] ; 0 ou TE_BADARC.
 * out et z ne doivent pas se chevaucher. */
long msa_unpack(const unsigned char *z, long len, unsigned char *out, long want);

#endif
