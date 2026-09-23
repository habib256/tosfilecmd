/*
 * edbuf.h -- le tampon de l'editeur de texte (portable, teste sur l'hote).
 *
 * Tampon a trou : le texte est buf[0..gap0) puis buf[gap1..cap). Les fins
 * de ligne sont ramenees a LF a l'ouverture et reconstituees dans le style
 * du fichier (CRLF, LF ou CR) a l'enregistrement : un fichier que l'on n'a
 * pas touche se reecrit donc octet pour octet. Un fichier qui melange les
 * styles, ou qui contient des octets nuls, n'est pas ouvert : l'editer
 * changerait des octets que l'utilisateur n'a pas modifies.
 */
#ifndef EDBUF_H
#define EDBUF_H

enum { EOL_LF, EOL_CRLF, EOL_CR };

/* Erreurs d'ouverture et d'insertion. */
#define EB_OK      0
#define EB_BINARY -1        /* octet nul : fichier binaire */
#define EB_MIXED  -2        /* fins de ligne de plusieurs styles */
#define EB_NOROOM -3        /* pas assez de memoire */
#define EB_FULL   -4        /* tampon plein */

typedef struct {
    char *buf;
    long cap;
    long gap0, gap1;
    int eol;
    int modified;
    long nlines;                /* nombre de LF : tenu a jour, jamais recompte */
} EDBUF;

/* Charge data dans mem[memcap] ; EB_OK ou une erreur (rien n'est garde). */
int  eb_load(EDBUF *b, const unsigned char *data, long size, char *mem, long memcap);
/* En ligne : appeles pour chaque caractere affiche ou parcouru. */
static inline long eb_len(const EDBUF *b)
{
    return b->cap - (b->gap1 - b->gap0);
}

static inline int eb_at(const EDBUF *b, long pos)   /* octet, ou -1 hors texte */
{
    if (pos < 0 || pos >= eb_len(b)) return -1;
    return (unsigned char)b->buf[pos < b->gap0 ? pos : pos + (b->gap1 - b->gap0)];
}
int  eb_insert(EDBUF *b, long pos, const char *s, long n);
void eb_delete(EDBUF *b, long pos, long n);

long eb_line_start(const EDBUF *b, long pos);
long eb_line_end(const EDBUF *b, long pos);      /* position du LF ou de la fin */
long eb_next_line(const EDBUF *b, long pos);     /* debut de la suivante, ou -1 */
long eb_prev_line(const EDBUF *b, long pos);     /* debut de la precedente, ou -1 */
long eb_line_number(const EDBUF *b, long pos);   /* 1 pour la premiere */

/* Colonne d'affichage de pos (tabulations tous les 8), et position de la
 * colonne col dans la ligne qui commence a start. */
int  eb_col(const EDBUF *b, long pos);
long eb_pos_at_col(const EDBUF *b, long start, int col);

/* Taille du fichier tel qu'il sera ecrit, et ses octets : export de out[cap]
 * a partir de l'octet at du fichier ; renvoie le nombre d'octets produits. */
long eb_file_size(const EDBUF *b);
long eb_export(const EDBUF *b, long at, char *out, long cap);

#endif
