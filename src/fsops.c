/*
 * fsops.c -- les operations sur les fichiers, et leurs protections.
 *
 * Les regles (docs/DATA-SAFETY.md) :
 *  - une destination existante n'est jamais tronquee : elle est renommee en
 *    TOSFC.BAK et ne disparait qu'une fois la nouvelle copie ecrite, fermee
 *    et relue ; en cas d'echec on la remet en place ;
 *  - un TOSFC.BAK deja present n'est jamais ecrase : l'operation est refusee ;
 *  - une sonde qui echoue autrement que par "fichier introuvable" interdit
 *    l'ecriture ;
 *  - on ne supprime que les fichiers crees par l'operation en cours ;
 *  - une source deplacee n'est supprimee qu'apres comparaison complete, et un
 *    dossier seulement si tout son contenu a ete deplace ;
 *  - tout un niveau de repertoire est lu avant d'agir : une erreur de lecture
 *    n'est jamais prise pour la fin du repertoire.
 */
#include "fsops.h"
#include "libc.h"

#define ATTR_ALL   (FA_DIR | FA_HIDDEN | FA_SYSTEM | FA_RDONLY)
#define MAX_DEPTH  16
#define ARENA_ENTRIES 640
#define BAK_NAME   "TOSFC.BAK"
/* Un bloc par lecture : environ une seconde de disquette, pour que ESC et
 * la barre de progression repondent pendant une longue copie. */
#define CHUNK      32768L

/* ---- Noms et chemins ---- */

static int name_char_ok(int c)
{
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    return strchr("_-!#$%&'()@^`{}~", c) != 0 && c != 0;
}

long name_normalize(const char *in, char *out)
{
    int base = 0, ext = 0, dot = 0, i = 0;
    const char *p;
    for (p = in; *p == ' '; p++) {}
    for (; *p && *p != ' '; p++) {
        int c = toupper((unsigned char)*p);
        if (c == '.') {
            if (dot || base == 0) return TE_BADNAME;
            dot = 1;
            out[i++] = '.';
            continue;
        }
        if (!name_char_ok(c)) return TE_BADNAME;
        if (dot) { if (++ext > 3) return TE_BADNAME; }
        else if (++base > 8) return TE_BADNAME;
        out[i++] = (char)c;
    }
    for (; *p == ' '; p++) {}
    if (*p || base == 0) return TE_BADNAME;
    if (dot && ext == 0) i--;       /* "NOM." devient "NOM" */
    out[i] = 0;
    return 0;
}

long path_join(char *out, const char *dir, const char *name, int slash)
{
    if (str_copy(out, dir, PATH_MAX_TOSFC)) return TE_TOOLONG;
    if (str_add(out, name, PATH_MAX_TOSFC)) return TE_TOOLONG;
    if (slash && str_add(out, "\\", PATH_MAX_TOSFC)) return TE_TOOLONG;
    return 0;
}

/* Dossier parent d'un chemin de fichier, '\' final compris. */
static void parent_of(char *out, const char *path)
{
    const char *s = strrchr(path, '\\');
    int n = s ? (int)(s - path) + 1 : 0;
    memcpy(out, path, n);
    out[n] = 0;
}

static int is_dotname(const char *n)
{
    return n[0] == '.' && (n[1] == 0 || (n[1] == '.' && n[2] == 0));
}

/* 1 si le fichier existe (f rempli), 0 s'il est absent, une erreur sinon. */
static long probe(const char *path, FINFO *f)
{
    long r = sys_first(path, ATTR_ALL, f);
    if (r == 0) return 1;
    if (r == EFILNF || r == ENMFIL) return 0;
    return r;
}

const char *err_text(long e)
{
    switch (e) {
    case ERROR:   return "General error";
    case EDRVNR:  return "Drive not ready";
    case E_CRC:   return "CRC error";
    case E_SEEK:  return "Seek error";
    case EMEDIA:  return "Unknown media";
    case ESECNF:  return "Sector not found";
    case EWRITF:  return "Write fault";
    case EREADF:  return "Read fault";
    case EWRPRO:  return "Disk is write-protected";
    case E_CHNG:  return "Disk was changed";
    case EOTHER:  return "Insert the other disk";
    case EFILNF:  return "File not found";
    case EPTHNF:  return "Path not found";
    case ENHNDL:  return "Too many open files";
    case EACCDN:  return "Access denied";
    case EIHNDL:  return "Invalid handle";
    case ENSMEM:  return "Not enough memory";
    case EDRIVE:  return "Invalid drive";
    case ENSAME:  return "Not the same drive";
    case ENMFIL:  return "No more files";
    case TE_SHORTW:   return "Disk full";
    case TE_VERIFY:   return "Verify failed: copy differs";
    case TE_EXISTS:   return "Name already exists";
    case TE_BAKEXIST: return "TOSFC.BAK already exists here";
    case TE_SELF:     return "Source and destination overlap";
    case TE_TOOLONG:  return "Path too long";
    case TE_TOODEEP:  return "Folders nested too deeply";
    case TE_ISDIR:    return "A folder has that name";
    case TE_CANCEL:   return "Cancelled";
    case TE_BADNAME:  return "Invalid 8.3 name";
    case TE_NOTEMPTY: return "Folder not empty: kept";
    case TE_TOOMANY:  return "Too many entries in a folder";
    case TE_CHANGED:  return "Source changed during copy";
    case TE_RESTORE:  return "Old version left as TOSFC.BAK";
    case TE_READONLY: return "File is read-only";
    case TE_BADDIR:   return "Unreadable name in folder";
    case TE_NOTPIC:   return "Not a Degas or NEOchrome picture";
    case TE_BADPIC:   return "Picture file is damaged or truncated";
    }
    return "Unexpected error";
}

/* ---- Memoire de travail ---- */

long ops_begin(OPS *o)
{
    long avail, want, arena_bytes = (long)ARENA_ENTRIES * sizeof(FINFO);

    o->overwrite = 0;
    o->stop = 0;
    o->bytes_done = o->bytes_total = 0;
    o->files_done = o->files_total = o->dirs_total = 0;
    o->errors = o->skipped = o->warnings = 0;
    o->last_err = 0;
    o->warn_path[0] = 0;

    avail = sys_avail();
    /* On laisse 32 Ko au systeme (tampons GEMDOS, accessoires). */
    want = avail - 32768L;
    if (want > 2 * CHUNK + arena_bytes) want = 2 * CHUNK + arena_bytes;
    if (want < 8192L + arena_bytes) return ENSMEM;
    want &= ~15L;
    o->block = sys_alloc(want);
    if (!o->block) return ENSMEM;
    o->arena = (FINFO *)o->block;
    o->arena_cap = ARENA_ENTRIES;
    o->arena_top = 0;
    o->buf = (char *)o->block + arena_bytes;
    o->bufsize = (want - arena_bytes) & ~1023L;
    return 0;
}

void ops_end(OPS *o)
{
    sys_free(o->block);
    o->block = 0;
}

/* Lit tout un niveau (sans . et ..) en haut de l'arene. */
static long read_level(OPS *o, const char *dir, int *first, int *count)
{
    char pat[PATH_MAX_TOSFC];
    FINFO f;
    long r;
    int n = 0;

    *first = o->arena_top;
    *count = 0;
    if (path_join(pat, dir, "*.*", 0)) return TE_TOOLONG;
    r = sys_first(pat, ATTR_ALL, &f);
    while (r == 0) {
        if (!is_dotname(f.name)) {
            char tmp[13];
            /* Un nom qui n'est pas un nom 8.3 valide pourrait fabriquer un
             * autre chemin que celui du fichier : on s'arrete. */
            if (name_normalize(f.name, tmp) || strcmp(tmp, f.name))
                return TE_BADDIR;
            if (o->arena_top + n >= o->arena_cap) return TE_TOOMANY;
            o->arena[o->arena_top + n] = f;
            n++;
        }
        r = sys_next(&f);
    }
    if (r != ENMFIL && r != EFILNF) return r;
    o->arena_top += n;
    *count = n;
    return 0;
}

/* ---- Parcours prealable ---- */

static long scan_tree(OPS *o, const char *dir, int depth)
{
    int first, n, i;
    long r;
    char sub[PATH_MAX_TOSFC];
    int save = o->arena_top;

    if (depth > MAX_DEPTH) return TE_TOODEEP;
    r = read_level(o, dir, &first, &n);
    if (r) { o->arena_top = save; return r; }
    for (i = 0; i < n && r == 0; i++) {
        FINFO *f = &o->arena[first + i];
        if (f->attr & FA_LABEL) continue;
        if (f->attr & FA_DIR) {
            o->dirs_total++;
            r = path_join(sub, dir, f->name, 1);
            if (r == 0) r = scan_tree(o, sub, depth + 1);
            if (r && !o->last_err) {
                o->last_err = r;
                str_copy(o->warn_path, sub, PATH_MAX_TOSFC);
            }
        } else {
            o->files_total++;
            o->bytes_total += f->size;
            /* Le chemin du fichier doit aussi tenir. */
            r = path_join(sub, dir, f->name, 0);
        }
    }
    o->arena_top = save;
    return r;
}

long ops_scan(OPS *o, const char *dir, const FINFO *items, int n)
{
    int i;
    long r = 0;
    char sub[PATH_MAX_TOSFC];
    o->last_err = 0;
    for (i = 0; i < n && r == 0; i++) {
        if (items[i].attr & FA_DIR) {
            o->dirs_total++;
            r = path_join(sub, dir, items[i].name, 1);
            if (r == 0) r = scan_tree(o, sub, 1);
            if (r && !o->last_err) {
                o->last_err = r;
                str_copy(o->warn_path, sub, PATH_MAX_TOSFC);
            }
        } else {
            o->files_total++;
            o->bytes_total += items[i].size;
        }
    }
    return r;
}

/* ---- Erreurs et questions ---- */

/* Signale une erreur ; renvoie 1 si l'operation doit s'arreter. */
static int fail(OPS *o, const char *path, long err, int more)
{
    o->errors++;
    o->last_err = err;
    if (err == TE_CANCEL) { o->stop = 1; return 1; }
    if (!o->error || !o->error(o, path, err, more)) o->stop = 1;
    return o->stop;
}

static void warn(OPS *o, const char *path, long err)
{
    o->warnings++;
    o->last_err = err;
    str_copy(o->warn_path, path, PATH_MAX_TOSFC);
    if (o->error) o->error(o, path, err, 1);
}

static int decide(OPS *o, int q, const char *path, const FINFO *s, const FINFO *d)
{
    int a;
    if (q == Q_MERGE) {
        /* Fusionner n'ecrase rien par soi-meme : chaque fichier en conflit
         * repassera par Q_OVERWRITE. La reponse ne vaut que pour ce dossier. */
        a = o->ask ? o->ask(o, q, path, s, d) : ANS_NO;
        if (a == ANS_ALL) return ANS_YES;
        if (a == ANS_NONE) return ANS_NO;
        return a;
    }
    if (o->overwrite == ANS_ALL) return ANS_YES;
    if (o->overwrite == ANS_NONE) return ANS_NO;
    a = o->ask ? o->ask(o, q, path, s, d) : ANS_NO;
    if (a == ANS_ALL) { o->overwrite = ANS_ALL; return ANS_YES; }
    if (a == ANS_NONE) { o->overwrite = ANS_NONE; return ANS_NO; }
    return a;
}

/* ---- Copie d'un fichier ---- */

static long compare_files(OPS *o, const char *a, const char *b)
{
    long ha, hb, na, nb, half = o->bufsize / 2, r = 0;
    if (half > CHUNK) half = CHUNK;
    char *ba = o->buf, *bb = o->buf + half;

    ha = sys_open(a, 0);
    if (ha < 0) return ha;
    hb = sys_open(b, 0);
    if (hb < 0) { sys_close((int)ha); return hb; }
    for (;;) {
        na = sys_read((int)ha, half, ba);
        if (na < 0) { r = na; break; }
        nb = sys_read((int)hb, half, bb);
        if (nb < 0) { r = nb; break; }
        if (na != nb || memcmp(ba, bb, na)) { r = TE_VERIFY; break; }
        if (na == 0) break;
        if (o->cancel && o->cancel(o)) { r = TE_CANCEL; break; }
    }
    sys_close((int)ha);
    sys_close((int)hb);
    return r;
}

/* Fait de la place pour dst : 0 si libre, 1 si l'ancienne version est en
 * TOSFC.BAK (bak rempli), 2 si l'utilisateur passe ce fichier, <0 erreur. */
static long make_room(OPS *o, const char *dst, const FINFO *sf, char *bak)
{
    FINFO df, bf;
    long r = probe(dst, &df);
    int a;

    if (r <= 0) return r;
    if (df.attr & FA_DIR) return TE_ISDIR;
    a = decide(o, Q_OVERWRITE, dst, sf, &df);
    if (a == ANS_CANCEL) return TE_CANCEL;
    if (a != ANS_YES) return 2;
    if (df.attr & FA_RDONLY) return TE_READONLY;
    parent_of(bak, dst);
    if (str_add(bak, BAK_NAME, PATH_MAX_TOSFC)) return TE_TOOLONG;
    r = probe(bak, &bf);
    if (r < 0) return r;
    if (r == 1) return TE_BAKEXIST;
    r = sys_rename(dst, bak);
    if (r < 0) return r;
    return 1;
}

/* Remet l'ancienne version en place apres un echec. */
static void restore_bak(OPS *o, const char *dst, const char *bak)
{
    if (sys_rename(bak, dst) < 0) warn(o, bak, TE_RESTORE);
}

static void drop_bak(OPS *o, const char *bak)
{
    if (sys_delete(bak) < 0) warn(o, bak, TE_RESTORE);
}

/* Ecrit src vers dst (absent). 0, ou une erreur apres avoir retire dst. */
static long write_copy(OPS *o, const char *src, const char *dst, const FINFO *sf,
                       int verify)
{
    long hs, hd, n, w, r = 0;
    long chunk = o->bufsize > CHUNK ? CHUNK : o->bufsize;
    FINFO chk;

    /* Creation exclusive : GEMDOS n'en a pas, on sonde juste avant. */
    r = probe(dst, &chk);
    if (r != 0) return r > 0 ? TE_EXISTS : r;
    hs = sys_open(src, 0);
    if (hs < 0) return hs;
    hd = sys_create(dst, 0);
    if (hd < 0) { sys_close((int)hs); return hd; }

    for (;;) {
        n = sys_read((int)hs, chunk, o->buf);
        if (n < 0) { r = n; break; }
        if (n == 0) break;
        w = sys_write((int)hd, n, o->buf);
        if (w < 0) { r = w; break; }
        if (w != n) { r = TE_SHORTW; break; }
        o->bytes_done += n;
        if (o->progress) o->progress(o, sf->name);
        if (o->cancel && o->cancel(o)) { r = TE_CANCEL; break; }
    }
    if (r == 0) sys_settime((int)hd, sf->time, sf->date);
    w = sys_close((int)hd);
    if (r == 0 && w < 0) r = w;
    sys_close((int)hs);
    if (r == 0 && verify) r = compare_files(o, src, dst);
    if (r == 0) {
        int a = sf->attr & (FA_RDONLY | FA_HIDDEN | FA_SYSTEM);
        if (a && sys_attrib(dst, 1, a | (sf->attr & FA_ARCH)) < 0)
            warn(o, dst, EACCDN);
        return 0;
    }
    /* Ce fichier-la, c'est nous qui l'avons cree. */
    sys_delete(dst);
    return r;
}

/* Copie ou deplace un fichier. Renvoie 1 si c'est fait, 0 s'il est passe
 * ou en erreur (deja signalee). */
static int do_file(OPS *o, const char *src, const char *dst, const FINFO *sf,
                   int move, int more)
{
    char bak[PATH_MAX_TOSFC];
    long r, room;
    int same_drive = toupper((unsigned char)src[0]) == toupper((unsigned char)dst[0]);

    if (!strcmp(src, dst)) { fail(o, dst, TE_SELF, more); return 0; }
    room = make_room(o, dst, sf, bak);
    if (room == 2) { o->skipped++; o->bytes_done += sf->size; return 0; }
    if (room < 0) { fail(o, dst, room, more); return 0; }

    if (move && same_drive) {
        /* Sur un meme lecteur, deplacer un fichier est un renommage. */
        FINFO chk;
        r = probe(dst, &chk);
        if (r == 0) r = sys_rename(src, dst);
        else if (r > 0) r = TE_EXISTS;
        if (r < 0) {
            if (room == 1) restore_bak(o, dst, bak);
            fail(o, src, r, more);
            return 0;
        }
        o->bytes_done += sf->size;
    } else {
        r = write_copy(o, src, dst, sf, o->verify || move);
        if (r < 0) {
            if (room == 1) restore_bak(o, dst, bak);
            fail(o, dst, r, more);
            return 0;
        }
        if (move) {
            if (sf->attr & FA_RDONLY) {
                /* La copie est verifiee, mais on ne force pas la suppression
                 * d'un fichier protege. */
                if (room == 1) drop_bak(o, bak);
                fail(o, src, TE_READONLY, more);
                return 0;
            }
            r = sys_delete(src);
            if (r < 0) {
                if (room == 1) drop_bak(o, bak);
                fail(o, src, r, more);
                return 0;
            }
        }
    }
    if (room == 1) drop_bak(o, bak);
    o->files_done++;
    if (o->progress) o->progress(o, sf->name);
    return 1;
}

/* ---- Arborescences ---- */

/* Copie (ou deplace) le dossier src vers dst ; renvoie 1 si tout y est. */
static int do_tree(OPS *o, const char *src, const char *dst, const FINFO *sf,
                   int move, int depth, int more)
{
    FINFO df;
    long r;
    int first, n, i, complete = 1;
    int save = o->arena_top;
    char s2[PATH_MAX_TOSFC], d2[PATH_MAX_TOSFC], dstdir[PATH_MAX_TOSFC];

    if (depth > MAX_DEPTH) { fail(o, src, TE_TOODEEP, more); return 0; }
    /* dst sans '\' final pour la sonde et Dcreate. */
    str_copy(dstdir, dst, PATH_MAX_TOSFC);
    dstdir[strlen(dstdir) - 1] = 0;
    r = probe(dstdir, &df);
    if (r < 0) { fail(o, dstdir, r, more); return 0; }
    if (r == 1 && !(df.attr & FA_DIR)) { fail(o, dstdir, TE_EXISTS, more); return 0; }
    if (r == 0) {
        r = sys_mkdir(dstdir);
        if (r < 0) { fail(o, dstdir, r, more); return 0; }
    } else {
        int a = decide(o, Q_MERGE, dstdir, sf, &df);
        if (a == ANS_CANCEL) { fail(o, dstdir, TE_CANCEL, more); return 0; }
        if (a != ANS_YES) { o->skipped++; return 0; }
    }

    r = read_level(o, src, &first, &n);
    if (r) { o->arena_top = save; fail(o, src, r, more); return 0; }
    for (i = 0; i < n && !o->stop; i++) {
        FINFO *f = &o->arena[first + i];
        int last_more = more || i + 1 < n;
        if (f->attr & FA_LABEL) continue;
        if (f->attr & FA_DIR) {
            if (path_join(s2, src, f->name, 1) || path_join(d2, dst, f->name, 1)) {
                fail(o, s2, TE_TOOLONG, last_more);
                complete = 0;
                continue;
            }
            if (!do_tree(o, s2, d2, f, move, depth + 1, last_more)) complete = 0;
        } else {
            if (path_join(s2, src, f->name, 0) || path_join(d2, dst, f->name, 0)) {
                fail(o, s2, TE_TOOLONG, last_more);
                complete = 0;
                continue;
            }
            if (!do_file(o, s2, d2, f, move, last_more)) complete = 0;
        }
    }
    o->arena_top = save;
    if (o->stop) complete = 0;
    if (move && complete) {
        str_copy(s2, src, PATH_MAX_TOSFC);
        s2[strlen(s2) - 1] = 0;
        r = sys_rmdir(s2);
        if (r < 0) { fail(o, s2, r, more); complete = 0; }
    }
    return complete;
}

long ops_copy(OPS *o, const char *srcdir, const FINFO *items, int n,
              const char *dstdir, int move)
{
    int i;
    char s[PATH_MAX_TOSFC], d[PATH_MAX_TOSFC];

    for (i = 0; i < n && !o->stop; i++) {
        const FINFO *f = &items[i];
        int more = i + 1 < n;
        if (f->attr & FA_DIR) {
            if (path_join(s, srcdir, f->name, 1) || path_join(d, dstdir, f->name, 1)) {
                fail(o, s, TE_TOOLONG, more);
                continue;
            }
            /* Un dossier dans lui-meme, ou dans un de ses descendants. */
            if (!strncmp(d, s, strlen(s))) {
                fail(o, s, TE_SELF, more);
                continue;
            }
            do_tree(o, s, d, f, move, 1, more);
        } else {
            if (path_join(s, srcdir, f->name, 0) || path_join(d, dstdir, f->name, 0)) {
                fail(o, s, TE_TOOLONG, more);
                continue;
            }
            do_file(o, s, d, f, move, more);
        }
    }
    return o->stop ? (o->last_err ? o->last_err : TE_CANCEL) : 0;
}

/* ---- Suppression ---- */

static int del_tree(OPS *o, const char *dir, int depth, int more)
{
    int first, n, i, complete = 1;
    int save = o->arena_top;
    long r;
    char p[PATH_MAX_TOSFC];

    if (depth > MAX_DEPTH) { fail(o, dir, TE_TOODEEP, more); return 0; }
    r = read_level(o, dir, &first, &n);
    if (r) { o->arena_top = save; fail(o, dir, r, more); return 0; }
    for (i = 0; i < n && !o->stop; i++) {
        FINFO *f = &o->arena[first + i];
        int m = more || i + 1 < n;
        if (f->attr & FA_LABEL) continue;
        if (path_join(p, dir, f->name, (f->attr & FA_DIR) != 0)) {
            fail(o, dir, TE_TOOLONG, m);
            complete = 0;
            continue;
        }
        if (f->attr & FA_DIR) {
            if (!del_tree(o, p, depth + 1, m)) complete = 0;
        } else if (f->attr & FA_RDONLY) {
            fail(o, p, TE_READONLY, m);
            complete = 0;
        } else {
            r = sys_delete(p);
            if (r < 0) { fail(o, p, r, m); complete = 0; }
            else {
                o->files_done++;
                o->bytes_done += f->size;
                if (o->progress) o->progress(o, f->name);
            }
        }
        if (o->cancel && o->cancel(o)) { fail(o, dir, TE_CANCEL, m); complete = 0; }
    }
    o->arena_top = save;
    if (o->stop) complete = 0;
    if (complete) {
        str_copy(p, dir, PATH_MAX_TOSFC);
        p[strlen(p) - 1] = 0;
        r = sys_rmdir(p);
        if (r < 0) { fail(o, p, r, more); complete = 0; }
    }
    return complete;
}

long ops_delete(OPS *o, const char *dir, const FINFO *items, int n)
{
    int i;
    long r;
    char p[PATH_MAX_TOSFC];

    for (i = 0; i < n && !o->stop; i++) {
        const FINFO *f = &items[i];
        int more = i + 1 < n;
        if (path_join(p, dir, f->name, (f->attr & FA_DIR) != 0)) {
            fail(o, dir, TE_TOOLONG, more);
            continue;
        }
        if (f->attr & FA_DIR) {
            del_tree(o, p, 1, more);
        } else if (f->attr & FA_RDONLY) {
            fail(o, p, TE_READONLY, more);
        } else {
            r = sys_delete(p);
            if (r < 0) fail(o, p, r, more);
            else {
                o->files_done++;
                o->bytes_done += f->size;
                if (o->progress) o->progress(o, f->name);
            }
        }
    }
    return o->stop ? (o->last_err ? o->last_err : TE_CANCEL) : 0;
}

/* ---- Operations simples ---- */

long ops_rename(const char *dir, const char *oldname, const char *newname)
{
    char from[PATH_MAX_TOSFC], to[PATH_MAX_TOSFC], nn[13];
    FINFO f;
    long r = name_normalize(newname, nn);
    if (r) return r;
    if (!strcmp(nn, oldname)) return 0;
    if (path_join(from, dir, oldname, 0) || path_join(to, dir, nn, 0))
        return TE_TOOLONG;
    r = probe(to, &f);
    if (r < 0) return r;
    if (r == 1) return TE_EXISTS;
    return sys_rename(from, to);
}

long ops_mkdir(const char *dir, const char *name)
{
    char p[PATH_MAX_TOSFC], nn[13];
    FINFO f;
    long r = name_normalize(name, nn);
    if (r) return r;
    if (path_join(p, dir, nn, 0) || strlen(p) + 1 >= PATH_MAX_TOSFC - 13)
        return TE_TOOLONG;
    r = probe(p, &f);
    if (r < 0) return r;
    if (r == 1) return TE_EXISTS;
    return sys_mkdir(p);
}

long ops_setattr(const char *dir, const FINFO *f, int attr)
{
    char p[PATH_MAX_TOSFC];
    long r;
    if (f->attr & FA_DIR) return TE_ISDIR;
    if (path_join(p, dir, f->name, 0)) return TE_TOOLONG;
    r = sys_attrib(p, 1, attr & (FA_RDONLY | FA_HIDDEN | FA_SYSTEM | FA_ARCH));
    return r < 0 ? r : 0;
}
