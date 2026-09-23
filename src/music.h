/*
 * music.h -- musique en tache de fond : fichiers YM (LHA -lh5- ou non) et
 * SNDH (compresses ICE! ou non). Le morceau continue pendant que l'on
 * travaille dans les panneaux.
 */
#ifndef MUSIC_H
#define MUSIC_H

enum { MUS_NONE, MUS_PLAYING, MUS_PAUSED };

/* 1 si le nom ressemble a un morceau (.YM, .SND, .SNDH). */
int music_is_name(const char *name);
/* Charge et joue dir+name (arrete le morceau precedent). 0 ou erreur. */
long music_play(const char *dir, const char *name);
void music_stop(void);
void music_pause(void);                 /* bascule pause / lecture */
int  music_state(void);
/* 1 si l'accroche au timer n'a pas pu etre retiree : quitter planterait. */
int  music_hooked(void);
/* SNDH : sous-morceau suivant (+1) ou precedent (-1). */
void music_tune(int delta);

/* Pour la boite Musique. */
const char *music_title(void);
const char *music_author(void);
const char *music_format(void);
int music_tune_number(void), music_tunes(void);
unsigned long music_seconds(void);

#endif
