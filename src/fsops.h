/*
 * fsops.h -- les operations sur les fichiers : copier, deplacer, supprimer,
 * renommer, creer un dossier, changer les attributs.
 *
 * Code portable : il ne parle qu'a sys.h, et l'interface passe par les
 * rappels de la structure OPS. Les regles de docs/DATA-SAFETY.md sont
 * appliquees ici ; tests/test_fsops.c les verifie avec des pannes injectees.
 */
#ifndef FSOPS_H
#define FSOPS_H

#include "sys.h"

/* Reponses aux questions. */
enum { ANS_YES, ANS_NO, ANS_ALL, ANS_NONE, ANS_CANCEL };
/* Questions posees par les operations. */
enum { Q_OVERWRITE, Q_MERGE };

typedef struct OPS OPS;
struct OPS {
    /* Rappels fournis par l'interface (ou par les tests). */
    int  (*ask)(OPS *o, int q, const char *path, const FINFO *src, const FINFO *dst);
    void (*progress)(OPS *o, const char *name);
    int  (*cancel)(OPS *o);
    /* Une erreur sur path. Renvoie 1 pour continuer avec la suite, 0 pour
     * arreter. more = 1 s'il reste des elements a traiter. */
    int  (*error)(OPS *o, const char *path, long err, int more);
    void *user;

    int verify;                 /* relire et comparer apres chaque copie */

    /* Etat, remis a zero par ops_begin. */
    int overwrite;              /* 0 demander, ANS_ALL ou ANS_NONE */
    int stop;
    unsigned long bytes_done, bytes_total;
    long files_done, files_total;
    long dirs_total;
    int errors, skipped, warnings;
    long last_err;
    char warn_path[PATH_MAX_TOSFC];

    /* Memoire de travail (ops_begin). */
    char *buf;
    long bufsize;
    FINFO *arena;
    int arena_cap, arena_top;
    void *block;
};

/* Reserve la memoire de travail ; 0 ou ENSMEM. */
long ops_begin(OPS *o);
void ops_end(OPS *o);

/* Parcourt les elements (et leurs arborescences) : totaux, et refus avant
 * toute ecriture si un dossier est illisible, trop profond ou trop long. */
long ops_scan(OPS *o, const char *dir, const FINFO *items, int n);

/* Copie (move = 0) ou deplace (move = 1) les elements de srcdir vers dstdir.
 * Les chemins de dossiers finissent par '\'. */
long ops_copy(OPS *o, const char *srcdir, const FINFO *items, int n,
              const char *dstdir, int move);
long ops_delete(OPS *o, const char *dir, const FINFO *items, int n);

/* Producteur des octets d'un fichier a ecrire : copie dans out[cap] les
 * octets a partir de at ; renvoie leur nombre (0 a la fin). */
typedef long (*STREAMFN)(void *ctx, long at, char *out, long cap);

/* Enregistre dir+name depuis gen : temporaire TOSFC.$ED exclusif, ecrit,
 * ferme, relu et compare ; puis l'ancien fichier devient TOSFC.BAK, le
 * temporaire prend son nom, et TOSFC.BAK est supprime. create = 1 pour un
 * nouveau fichier (le nom doit etre libre). Sur erreur, l'original est
 * intact (ou remis en place) et le temporaire retire. buf : 2 x half octets
 * de travail. Les attributs de l'original (sauf lecture seule) sont gardes. */
long ops_save_stream(const char *dir, const char *name, int create,
                     STREAMFN gen, void *ctx, char *buf, long half);

long ops_rename(const char *dir, const char *oldname, const char *newname);
long ops_mkdir(const char *dir, const char *name);
long ops_setattr(const char *dir, const FINFO *f, int attr);

/* Normalise un nom 8.3 (majuscules) dans out[13] ; 0 ou TE_BADNAME. */
long name_normalize(const char *in, char *out);

/* Chemin dir + name (+ '\' si slash) ; 0 ou TE_TOOLONG. */
long path_join(char *out, const char *dir, const char *name, int slash);

/* Texte court d'une erreur. */
const char *err_text(long err);

#endif
