/*
 * disk.h -- outils de disquette : lire une disquette en image, ecrire une
 * image sur disquette, copier une disquette, formater.
 *
 * Tout passe par les appels XBIOS de sys.h (Floprd, Flopwr, Flopfmt) ; le
 * code est portable et teste sur l'hote contre de fausses disquettes
 * (tests/fakedos.c), pannes comprises.
 *
 * Regles de securite (docs/DATA-SAFETY.md) :
 *  - la disquette de TOSFC (secteur de boot releve au demarrage) n'est
 *    jamais la cible d'une ecriture ;
 *  - chaque piste ecrite est relue et comparee ;
 *  - la piste 0 (secteur de boot, FAT) est ecrite en DERNIER : une copie
 *    interrompue n'a pas l'air d'une disquette complete, et pendant une copie
 *    a un lecteur, la cible garde son numero de serie jusqu'au bout, ce qui
 *    permet de reconnaitre a chaque echange la disquette inseree (une
 *    ancienne copie de la source, indiscernable d'elle, est alors refusee :
 *    la formater d'abord) ;
 *  - lire une disquette en image passe par la copie ordinaire (fsops) :
 *    creation exclusive, relecture, TOSFC.BAK.
 */
#ifndef DISK_H
#define DISK_H

#include "fsops.h"

#define DISK_SPT_MAX   11               /* double densite */
#define DISK_TRACK_MAX 86
#define DISK_WORK      (8L * 1024)      /* tampon de Flopfmt */

typedef struct {
    int spt, sides, tracks;
} GEOM;

typedef struct DISK DISK;
struct DISK {
    void *ctx;
    /* Progression : done / total pistes ; 1 = annuler. */
    int  (*progress)(DISK *d, long done, long total);
    /* Un seul lecteur : inserer la source (which 0) ou la cible (1) dans dev.
     * again : la disquette inseree n'etait pas la bonne. 0 = annuler. */
    int  (*insert)(DISK *d, int which, int dev, int again);
    unsigned char *buf;                 /* memoire de travail */
    long bufsize;
    /* Disquette du programme : numero de serie (octets 8 a 10 du boot). */
    int prog_dev;                       /* -1 : TOSFC n'est pas sur disquette */
    unsigned char prog_boot[512];
    /* Resultat */
    long track_err;                     /* piste fautive (-1 aucune) */
};

/* Geometrie d'apres le BPB d'un secteur de boot (0 ou TE_NOFLOPPY). */
long disk_geom_boot(const unsigned char *boot, GEOM *g);
/* Geometrie d'une image .ST d'apres sa taille seule (sans BPB lisible). */
long disk_geom_size(long size, GEOM *g);
long disk_bytes(const GEOM *g);
void disk_describe(char *out, const GEOM *g);   /* "720 KB: 80 x 9 x 2" */

/* Releve la disquette du programme (appele au demarrage). */
void disk_note_program(DISK *d, int dev);
/* Lit le secteur de boot et la geometrie de la disquette de dev. */
long disk_probe(DISK *d, int dev, unsigned char *boot, GEOM *g);

/* Lecture d'une disquette comme un fichier : la SOURCE rend un seul
 * element, name, de la taille de la disquette, lu piste par piste. */
typedef struct {
    DISK *d;
    int dev;
    GEOM g;
    char name[14];
    SOURCE src;
    long pos, size;
    long cached;                        /* piste-face en tampon, -1 aucune */
    unsigned char *trk;                 /* DISK_SPT_MAX * 512 */
    int listed;
    unsigned short time, date;
} FLOPSRC;
long disk_source(FLOPSRC *f, DISK *d, int dev, const char *name);
void disk_source_end(FLOPSRC *f);

/* Image (.ST ou .MSA) sur disquette : formate, ecrit et relit chaque piste. */
long disk_write_image(DISK *d, const char *path, int dev);
/* Copie de disquette, src et dst eventuellement le meme lecteur. */
long disk_copy(DISK *d, int src, int dst);
/* Formatage : spt 9, 10 ou 11, sides 1 ou 2, 80 pistes. */
long disk_format(DISK *d, int dev, int spt, int sides);

#endif
