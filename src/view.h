/*
 * view.h -- les visionneuses : texte (et hexadecimal), images plein ecran.
 * Lecture seule : elles n'ecrivent sur aucun disque.
 */
#ifndef VIEW_H
#define VIEW_H

#include "panel.h"

/* Lecteur de texte sur l'entree courante du panneau ; hex = demarrer en
 * vue hexadecimale. */
void view_text(PANEL *p, int hex);

/* Image plein ecran de l'entree courante ; Gauche/Droite parcourent les
 * autres images du dossier, et la selection du panneau suit. */
void view_picture(PANEL *p);

#endif
