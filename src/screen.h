/*
 * screen.h -- ecran texte 80 x 25 dessine directement en memoire video.
 *
 * scr_chr/scr_att sont l'ecran voulu ; scr_flush() ne redessine que les
 * cellules qui ont change. Les bancs lisent scr_chr dans la memoire de
 * l'Atari (symbole du lien) : c'est le texte exact que l'utilisateur voit.
 */
#ifndef SCREEN_H
#define SCREEN_H

#define SCR_COLS 80
#define SCR_ROWS 25

/* Attributs : couleur de texte et de fond, et leur equivalent monochrome. */
enum {
    A_NORMAL,   /* texte des panneaux */
    A_FRAME,    /* cadres et en-tetes */
    A_TAG,      /* fichier marque */
    A_CURSOR,   /* barre de selection du panneau actif */
    A_CURTAG,   /* barre de selection sur un fichier marque */
    A_DIALOG,   /* boite de dialogue */
    A_DLGSEL,   /* choix selectionne ou zone de saisie */
    A_BARKEY,   /* touche de la barre du bas */
    A_BARTXT,   /* libelle de la barre du bas */
    A_TITLE,    /* titre de boite ou de panneau actif */
    A_COUNT
};

/* Glyphes propres a TOSFC (remplacent des caracteres de controle). */
#define G_SBT   0x0F    /* simple horizontal, simple vers le haut */
#define G_DH    0x10    /* double horizontal */
#define G_DV    0x11    /* double vertical */
#define G_DTL   0x12
#define G_DTR   0x13
#define G_DBL   0x14
#define G_DBR   0x15
#define G_DTS   0x16    /* double horizontal, simple vers le bas */
#define G_DBS   0x17    /* double horizontal, simple vers le haut */
#define G_DLS   0x18    /* double vertical, simple vers la droite */
#define G_DRS   0x19    /* double vertical, simple vers la gauche */
#define G_SV    0x1A    /* simple vertical */
#define G_SH    0x1B    /* simple horizontal */
#define G_CHECK 0x1C    /* marque */
#define G_SHADE 0x1D    /* barre de progression, partie vide */
#define G_BLOCK 0x1E    /* barre de progression, partie pleine */
#define G_UPDIR 0x1F    /* fleche du dossier parent */

extern unsigned char scr_chr[SCR_ROWS * SCR_COLS];
extern unsigned char scr_att[SCR_ROWS * SCR_COLS];
extern short scr_mono;          /* 1 en haute resolution monochrome */
extern short scr_cell_h;        /* 8 ou 16 lignes par caractere */

/* 0 si l'ecran est pris en main, sinon un message a afficher par le TOS. */
const char *scr_init(void);
void scr_exit(void);

extern unsigned char scr_row_dirty[SCR_ROWS];

/* Ecriture d'une cellule : en ligne, c'est l'operation la plus frequente. */
static inline void scr_putc(int x, int y, int ch, int attr)
{
    if ((unsigned)x < SCR_COLS && (unsigned)y < SCR_ROWS) {
        int i = y * SCR_COLS + x;
        scr_chr[i] = (unsigned char)ch;
        scr_att[i] = (unsigned char)attr;
        scr_row_dirty[y] = 1;
    }
}
void scr_puts(int x, int y, const char *s, int attr);
/* Ecrit s dans un champ de w colonnes, complete par des espaces. */
void scr_field(int x, int y, int w, const char *s, int attr);
void scr_fill(int x, int y, int w, int ch, int attr);
void scr_box(int x, int y, int w, int h, int attr);   /* cadre double + fond */
void scr_attr(int x, int y, int w, int attr);         /* change l'attribut */

/* Pointeur souris : la cellule (x, y) est inversee a l'affichage. */
void scr_pointer(int x, int y);

void scr_flush(void);

/* Visionneuses plein ecran. scr_graphics passe dans la resolution rez (0 ou
 * 1 en couleur ; ignoree en monochrome) avec la palette pal (16 registres,
 * ou 0 pour ne pas y toucher), et rend l'adresse de l'ecran (32000 octets).
 * scr_text revient a l'ecran texte de TOSFC et le fait tout redessiner. */
unsigned char *scr_graphics(int rez, const unsigned short *pal);
void scr_text(void);
void scr_invalidate(void);
/* Le tampon a ete modifie en bloc (restauration d'une boite) : tout comparer. */
void scr_touch(void);

#endif
