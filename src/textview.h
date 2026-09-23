/*
 * textview.h -- mise en page du lecteur de texte (portable, testee sur
 * l'hote). Une "ligne d'ecran" fait au plus TV_COLS colonnes ; une ligne du
 * fichier trop longue est coupee au dernier espace, ou net a defaut.
 */
#ifndef TEXTVIEW_H
#define TEXTVIEW_H

#define TV_COLS 80

typedef struct {
    const unsigned char *data;
    long size;
    int firstword;      /* 1st Word / 1st Word Plus : codes de mise en forme */
    int hex;            /* vue hexadecimale : 16 octets par ligne */
} TEXTDOC;

/* 1 si le fichier ressemble a un document 1st Word (lignes de format $1F). */
int tv_is_firstword(const char *name, const unsigned char *data, long size);

/* Met en page la ligne d'ecran qui commence a off : texte dans out (TV_COLS
 * caracteres + 0, complete par des espaces) si out != 0, et renvoie le debut
 * de la ligne suivante (size a la fin). */
long tv_layout(const TEXTDOC *d, long off, char *out);

/* Debut de la ligne d'ecran precedente (0 au debut). */
long tv_prev(const TEXTDOC *d, long off);

/* Debut de la ligne d'ecran qui contient l'octet pos. */
long tv_line_of(const TEXTDOC *d, long pos);

/* Premiere occurrence de needle (sans tenir compte de la casse) a partir de
 * from ; -1 si absente. */
long tv_find(const TEXTDOC *d, long from, const char *needle);

#endif
