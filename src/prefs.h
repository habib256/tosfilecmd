/*
 * prefs.h -- TOSFC.INF : panneaux, tris et options, enregistres a la demande.
 */
#ifndef PREFS_H
#define PREFS_H

#include "sys.h"

typedef struct {
    char path[2][PATH_MAX_TOSFC];   /* "" = liste des lecteurs */
    int sort[2];
    int active;
    int verify;
    int show_hidden;
} PREFS;

/* Texte du fichier <-> structure. Portables, testes sur l'hote. */
int  prefs_format(const PREFS *p, char *out, int cap);
void prefs_parse(PREFS *p, const char *text, long len);

/* Lecture et ecriture dans le dossier home ("A:\" ...). */
void prefs_load(PREFS *p, const char *home);
/* Ecrit TOSFC.NEW, le relit, puis remplace TOSFC.INF. 0 ou erreur. */
long prefs_save(const PREFS *p, const char *home);

#endif
