/*
 * sys.h -- l'interface systeme portable du coeur de TOSFC.
 *
 * Tout ce qui touche aux fichiers passe par ces fonctions : sur l'Atari,
 * sys_tos.c les traduit en appels GEMDOS ; sur l'hote, tests/fakedos.c les
 * sert depuis un systeme de fichiers en memoire qui sait injecter des pannes.
 * C'est ce qui permet d'executer le vrai code des operations (fsops.c) dans
 * les tests de regression, pannes comprises.
 */
#ifndef SYS_H
#define SYS_H

/* Codes d'erreur GEMDOS/BIOS (negatifs). */
#define E_OK      0L
#define ERROR    -1L
#define EDRVNR   -2L
#define E_CRC    -4L
#define E_SEEK   -6L
#define EMEDIA   -7L
#define ESECNF   -8L
#define EWRITF  -10L
#define EREADF  -11L
#define EWRPRO  -13L
#define E_CHNG  -14L
#define EOTHER  -17L
#define EFILNF  -33L
#define EPTHNF  -34L
#define ENHNDL  -35L
#define EACCDN  -36L
#define EIHNDL  -37L
#define ENSMEM  -39L
#define EIMBA   -40L
#define EDRIVE  -46L
#define ENSAME  -48L
#define ENMFIL  -49L

/* Erreurs propres a TOSFC (hors de la plage GEMDOS). */
#define TE_SHORTW   -200L   /* ecriture incomplete : disque plein */
#define TE_VERIFY   -201L   /* la relecture differe de la source */
#define TE_EXISTS   -202L   /* la destination existe deja */
#define TE_BAKEXIST -203L   /* TOSFC.BAK existe deja : on n'y touche pas */
#define TE_SELF     -204L   /* source et destination identiques / imbriquees */
#define TE_TOOLONG  -205L   /* chemin trop long */
#define TE_TOODEEP  -206L   /* arborescence trop profonde */
#define TE_ISDIR    -207L   /* un dossier porte deja ce nom */
#define TE_CANCEL   -208L   /* annule par l'utilisateur */
#define TE_BADNAME  -209L   /* nom GEMDOS invalide */
#define TE_NOTEMPTY -210L   /* dossier non vide ou incompletement traite */
#define TE_TOOMANY  -211L   /* trop d'entrees pour la memoire reservee */
#define TE_CHANGED  -212L   /* la source a change pendant l'operation */
#define TE_RESTORE  -213L   /* restauration impossible : voir TOSFC.BAK */
#define TE_READONLY -214L   /* fichier en lecture seule */
#define TE_BADDIR   -215L   /* nom illisible dans un repertoire */
#define TE_NOTPIC   -216L   /* pas une image reconnue */
#define TE_BADPIC   -217L   /* image tronquee ou incoherente */
#define TE_BADMUS   -218L   /* morceau illisible (YM, SNDH) */
#define TE_BADARC   -219L   /* image ou archive malformee, donnees corrompues */
#define TE_METHOD   -220L   /* methode de compression non geree */
#define TE_RDONLYFS -221L   /* dans une image ou une archive : lecture seule */
#define TE_BIG      -222L   /* trop grand pour la memoire libre */

/* Attributs FAT. */
#define FA_RDONLY 0x01
#define FA_HIDDEN 0x02
#define FA_SYSTEM 0x04
#define FA_LABEL  0x08
#define FA_DIR    0x10
#define FA_ARCH   0x20

/* Une entree de repertoire, telle que Fsfirst/Fsnext la decrivent. */
typedef struct {
    char           name[14];   /* 8.3, termine par 0 */
    unsigned char  attr;
    unsigned char  pad;
    unsigned short time;       /* format FAT */
    unsigned short date;
    unsigned long  size;
} FINFO;

#define PATH_MAX_TOSFC 128     /* chemins complets, 0 final compris */

/* Recherche : pattern complet ("A:\DIR\*.*"), attrs GEMDOS. 0, EFILNF/ENMFIL
 * ou une autre erreur. L'etat de recherche est interne a l'appelant (un seul
 * parcours a la fois : les parcours imbriques lisent d'abord tout un niveau). */
long sys_first(const char *pattern, int attr, FINFO *out);
long sys_next(FINFO *out);

long sys_open(const char *path, int mode);          /* handle >= 0 ou erreur */
long sys_create(const char *path, int attr);
long sys_close(int h);
long sys_read(int h, long n, void *buf);
long sys_write(int h, long n, const void *buf);
long sys_seek(int h, long offset, int mode);   /* 0 debut, 1 courant, 2 fin */
long sys_delete(const char *path);
long sys_rename(const char *from, const char *to);
long sys_mkdir(const char *path);
long sys_rmdir(const char *path);
long sys_attrib(const char *path, int set, int attr);   /* renvoie l'attribut */
long sys_settime(int h, unsigned short time, unsigned short date);
long sys_gettime(int h, unsigned short *time, unsigned short *date);
long sys_dfree(int drive, unsigned long *freeb, unsigned long *totalb);

void *sys_alloc(long n);
long  sys_avail(void);
void  sys_free(void *p);

#endif
