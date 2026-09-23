/*
 * ym.h -- fichiers YM (StSound) : YM2, YM3, YM3b, YM4, YM5, YM6.
 * Portable, teste sur l'hote. Les effets speciaux Atari des YM4-YM6
 * (digidrums, SID) ne sont pas joues : seuls les 14 registres le sont.
 */
#ifndef YM_H
#define YM_H

typedef struct {
    const unsigned char *regs;  /* donnees des registres */
    long frames, loop;
    int nregs;                  /* 14 (YM2/YM3) ou 16 */
    int interleaved;
    int hz;                     /* frequence de lecture (50 en general) */
    long clock;                 /* horloge du YM2149 (2 MHz sur ST) */
    const char *title, *author, *comment;   /* chaines du fichier ("" sinon) */
    char format[6];             /* "YM5!" ... */
} YMSONG;

#define YM_OK   0
#define YM_BAD -1               /* malforme, tronque */
#define YM_NOT -2               /* pas un YM */

/* data : le fichier YM decompresse (pas l'archive LHA). */
int ym_parse(const unsigned char *data, long size, YMSONG *s);
/* Registre r (0..13) de la trame f. */
int ym_reg(const YMSONG *s, long f, int r);

#endif
