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

#endif
