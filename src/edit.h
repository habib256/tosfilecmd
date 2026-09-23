/*
 * edit.h -- l'editeur de texte plein ecran.
 */
#ifndef EDIT_H
#define EDIT_H

#include "panel.h"

/* Edite le fichier courant du panneau. Sur un dossier ou "..", demande le
 * nom d'un nouveau fichier a creer dans le dossier du panneau. Renvoie 1 si
 * un fichier a ete enregistre (le panneau est a relire). */
int edit_file(PANEL *p);

#endif
