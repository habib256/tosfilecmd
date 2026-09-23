/*
 * panel.h -- un panneau : un repertoire (ou la liste des lecteurs), trie,
 * avec sa selection et ses fichiers marques.
 */
#ifndef PANEL_H
#define PANEL_H

#include "sys.h"
#include "vfs.h"

#define PANEL_MAX   1024        /* entrees par panneau */
#define PANEL_ROWS  19          /* lignes de fichiers visibles */

enum { SORT_NAME, SORT_EXT, SORT_SIZE, SORT_DATE, SORT_NONE, SORT_COUNT };

/* FINFO.pad : bit 0 = marque, bit 1 = entree ".." ajoutee par TOSFC. */
#define PF_TAG 1
#define PF_UP  2

typedef struct {
    char path[PATH_MAX_TOSFC];  /* "A:\DIR\" ; vide = liste des lecteurs */
    FINFO *ent;
    int n, cur, top;
    int sort;
    int ntag;
    unsigned long tagbytes;
    int truncated;              /* plus de PANEL_MAX entrees */
    long err;                   /* derniere erreur de lecture */
    unsigned long freeb;
    int free_ok;
    VFS *vfs;                   /* image ou archive ouverte : path est dedans */
} PANEL;

extern int opt_show_hidden;

/* Relit le repertoire et remet la selection sur keep (ou garde l'indice). */
long panel_load(PANEL *p, const char *keep);
void panel_drives(PANEL *p, char select_drive);
void panel_sort(PANEL *p);
void panel_update_free(PANEL *p);
void panel_draw(PANEL *p, int x, int active);
void panel_move(PANEL *p, int delta);
void panel_tag(PANEL *p, int idx, int on);
/* Change de repertoire vers le dossier courant, ou remonte (entree ".."). */
long panel_enter(PANEL *p);
long panel_up(PANEL *p);
/* Elements concernes par une operation : marques, sinon la selection. */
int  panel_items(PANEL *p, FINFO *out, int cap);
FINFO *panel_current(PANEL *p);
int  panel_is_drives(const PANEL *p);
/* Ferme l'image ou l'archive ouverte dans le panneau (s'il y en a une). */
void panel_close_vfs(PANEL *p);

/* Nom 8.3 affiche en colonnes "NOM     EXT". */
void fmt_name83(char *out, const char *name);
void fmt_date(char *out, unsigned short date);
void fmt_time(char *out, unsigned short time);

#endif
