/*
 * fakedos.h -- un GEMDOS en memoire pour executer fsops.c sur l'hote.
 *
 * Les fichiers sont des chemins complets en majuscules ("C:\DIR\F.TXT").
 * Chaque appel peut echouer sur commande : fd_fail[OP] = N fait echouer le
 * Nieme appel de cette operation (1 = le prochain) avec fd_fail_code[OP].
 */
#ifndef FAKEDOS_H
#define FAKEDOS_H

#include "../src/sys.h"

enum { OP_FIRST, OP_NEXT, OP_OPEN, OP_CREATE, OP_CLOSE, OP_READ, OP_WRITE,
       OP_DELETE, OP_RENAME, OP_MKDIR, OP_RMDIR, OP_ATTRIB, OP_COUNT };

extern int  fd_fail[OP_COUNT];
extern long fd_fail_code[OP_COUNT];
extern long fd_calls[OP_COUNT];
/* Octets libres par lecteur ('A'..'P') ; -1 = illimite. */
extern long fd_capacity[16];
/* Corrompt le premier octet ecrit par le Nieme Fwrite (0 = jamais). */
extern int  fd_corrupt_write;
/* Declenche l'annulation apres N rappels de progression (0 = jamais). */
extern int  fd_cancel_after;

void fd_reset(void);
void fd_mkdir(const char *path);
void fd_put(const char *path, const void *data, long size, int attr);
/* Contenu d'un fichier (NULL s'il n'existe pas) ; size recoit sa taille. */
const unsigned char *fd_get(const char *path, long *size);
int  fd_exists(const char *path);
int  fd_isdir(const char *path);
int  fd_attr(const char *path);
/* Nombre d'entrees (fichiers et dossiers) sous ce lecteur. */
int  fd_count(char drive);
int  fd_open_handles(void);

/* ---- Fausses disquettes ----
 * FD_DISKS disquettes numerotees, placees dans les lecteurs A: et B:
 * (fd_drive[dev] = numero, -1 vide). Chaque piste (cylindre, face) a son
 * nombre de secteurs formates (0 = jamais formatee). */
#define FD_DISKS  6
#define FD_CYLS   86
#define FD_SPT    11
extern int fd_drive[2];
extern int fd_nflops;                   /* sys_nflops() */
extern long fd_mediach[2];              /* appels de sys_mediach par lecteur */
extern long fd_flop_reads, fd_flop_writes, fd_flop_formats;
/* Pannes : la piste (cyl * 2 + face) est illisible fd_bad_reads fois de suite
 * (-1 : toujours) ; une ecriture sur fd_weak_cyl stocke un octet faux ;
 * le formatage n'accepte que fd_fmt_only_spt secteurs (0 : tous) ; la Nieme
 * lecture d'une disquette rend un octet faux (disquette qui "bouge"). */
extern int fd_bad_track, fd_bad_reads, fd_weak_cyl, fd_fmt_only_spt, fd_flaky_read;
/* Journal des cylindres ecrits, dans l'ordre. */
extern int fd_write_log[FD_CYLS * 2 * 4], fd_write_log_n;

void fd_disk_blank(int k);
/* Disquette k formatee (spt, sides, cylindres d'apres la taille), remplie
 * avec image[size]. */
void fd_disk_load(int k, const unsigned char *image, long size, int spt, int sides);
/* Contenu lineaire (cyls x sides x spt) ; 0 si une piste n'a pas ce format. */
int  fd_disk_dump(int k, unsigned char *out, int spt, int sides, int cyls);
void fd_disk_wprot(int k, int on);

#endif
