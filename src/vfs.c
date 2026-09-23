/*
 * vfs.c -- images disque et archives vues comme des dossiers.
 *
 * Tout est lu avec des bornes : BPB verifie, chaines de clusters limitees
 * au nombre de clusters (un cycle est une erreur), pistes MSA decompressees
 * dans leur taille exacte, en-tetes d'archives relus dans le fichier. Le
 * conteneur n'est ouvert que le temps d'une lecture : l'utilisateur peut
 * changer de disquette entre deux sans que TOSFC garde un fichier ouvert.
 */
#include "vfs.h"
#include "lzh.h"
#include "inflate.h"
#include "arc.h"
#include "libc.h"

#define MAX_DEPTH 16

struct VFS {
    int kind;
    char path[PATH_MAX_TOSFC];          /* le fichier conteneur */
    char root[PATH_MAX_TOSFC];          /* "A:\DIR\DISK.ST\" */
    char desc[48];
    VENT *ent;
    int n;
    SOURCE src;
    /* parcours d'un dossier */
    int it_parent, it_i;
    /* fichier ouvert (un seul a la fois) */
    int of;                             /* indice de l'entree, -1 si aucun */
    long fh;                            /* conteneur pendant la lecture */
    long csize;                         /* taille et date du conteneur a */
    unsigned short ctime, cdate;        /* l'ouverture : tout changement */
                                        /* rend la lecture impossible */
    unsigned char *buf;                 /* archive : fichier decompresse */
    long bufpos;
    long remaining;
    long cl, cloff, clsteps;
    long cached_sector;
    /* image */
    int msa, spt, sides, t0, t1;
    long *trk;
    unsigned char *trkbuf;
    long trk_cached;
    long nsects;
    int spc, res, nfats, ndirs, spf;
    long root_start, data_start, ncl;
    unsigned short *fat;
    unsigned char sec[512];
};

/* ---- noms ---- */

static int valid83(int c)
{
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    return c && strchr("_-!#$%&'()@^`{}~", c) != 0;
}

void vfs_name83(const char *in, char *out, int (*taken)(void *ctx, const char *n), void *ctx)
{
    char base[9], ext[4], cand[13], num[4];
    const char *s = in, *p, *dot = 0;
    int bl = 0, el = 0, k;

    for (p = in; *p; p++)
        if (*p == '/' || *p == '\\') s = p + 1;
    while (*s == '.') s++;                      /* ".profile" -> "PROFILE" */
    for (p = s; *p; p++)
        if (*p == '.') dot = p;
    for (p = s; *p && p != dot && bl < 8; p++) {
        int c = toupper((unsigned char)*p);
        if (c == ' ') continue;
        base[bl++] = (char)(valid83(c) ? c : '_');
    }
    if (dot)
        for (p = dot + 1; *p && el < 3; p++) {
            int c = toupper((unsigned char)*p);
            if (c == ' ') continue;
            ext[el++] = (char)(valid83(c) ? c : '_');
        }
    if (bl == 0) base[bl++] = '_';
    base[bl] = 0;
    ext[el] = 0;
    for (k = 0; k < 100; k++) {
        if (k == 0) str_copy(cand, base, sizeof cand);
        else {
            int keep, nl = fmt_ulong(num, (unsigned long)k, 0);
            keep = 8 - 1 - nl;
            if (keep > bl) keep = bl;
            memcpy(cand, base, keep);
            cand[keep] = '~';
            strcpy(cand + keep + 1, num);
        }
        if (el) {
            str_add(cand, ".", sizeof cand);
            str_add(cand, ext, sizeof cand);
        }
        if (!taken || !taken(ctx, cand)) break;
    }
    strcpy(out, cand);
}

typedef struct { VFS *v; int parent; } TAKEN;

static int taken_in(void *ctx, const char *n)
{
    TAKEN *t = ctx;
    int i;
    for (i = 0; i < t->v->n; i++)
        if (t->v->ent[i].parent == t->parent && !strcmp(t->v->ent[i].name, n)) return 1;
    return 0;
}

/* Nouvelle entree dans parent, nom converti et unique. -1 si c'est plein. */
static int add_entry(VFS *v, int parent, const char *rawname, int attr)
{
    TAKEN t;
    VENT *e;
    if (v->n >= VFS_MAX) return -1;
    e = &v->ent[v->n];
    memset(e, 0, sizeof *e);
    t.v = v;
    t.parent = parent;
    vfs_name83(rawname, e->name, taken_in, &t);
    e->parent = (short)parent;
    e->attr = (unsigned char)attr;
    return v->n++;
}

static int depth_of(VFS *v, int i)
{
    int d = 0;
    while (i >= 0 && d <= MAX_DEPTH + 1) { i = v->ent[i].parent; d++; }
    return d;
}

/* Dossier path (relatif, separateurs '\' ou '/') sous parent : cree les
 * dossiers manquants. -1 si plein ou trop profond. */
static int ensure_dirs(VFS *v, int parent, const char *path)
{
    char comp[64], plain[13];
    const char *p = path;
    int i;
    while (*p) {
        int n = 0;
        while (*p == '\\' || *p == '/') p++;
        if (!*p) break;
        while (*p && *p != '\\' && *p != '/' && n < 63) comp[n++] = *p++;
        while (*p && *p != '\\' && *p != '/') p++;
        comp[n] = 0;
        vfs_name83(comp, plain, 0, 0);
        for (i = 0; i < v->n; i++)
            if (v->ent[i].parent == parent && (v->ent[i].attr & FA_DIR) &&
                !strcmp(v->ent[i].name, plain))
                break;
        if (i == v->n) {
            if (depth_of(v, parent) > MAX_DEPTH) return -1;
            i = add_entry(v, parent, comp, FA_DIR);
            if (i < 0) return -1;
        }
        parent = i;
    }
    return parent;
}

/* ---- chemins ---- */

static int prefix_ci(const char *s, const char *pre)
{
    while (*pre) {
        if (toupper((unsigned char)*s) != toupper((unsigned char)*pre)) return 0;
        s++;
        pre++;
    }
    return 1;
}

int vfs_contains(const VFS *v, const char *path)
{
    return v && prefix_ci(path, v->root);
}

/* Entree designee par path (dossier si want_dir) ; -1 pour la racine,
 * -2 si introuvable. */
static int resolve(VFS *v, const char *path)
{
    const char *p;
    int parent = -1, i;
    char comp[13];
    if (!prefix_ci(path, v->root)) return -2;
    p = path + strlen(v->root);
    while (*p) {
        int n = 0;
        while (*p && *p != '\\' && n < 12) comp[n++] = (char)toupper((unsigned char)*p++);
        comp[n] = 0;
        if (*p && *p != '\\') return -2;
        if (*p == '\\') p++;
        for (i = 0; i < v->n; i++)
            if (v->ent[i].parent == parent && !strcmp(v->ent[i].name, comp)) break;
        if (i == v->n) return -2;
        parent = i;
    }
    return parent;
}

static void to_finfo(const VENT *e, FINFO *f)
{
    memset(f, 0, sizeof *f);
    strcpy(f->name, e->name);
    f->attr = e->attr;
    f->time = e->time;
    f->date = e->date;
    f->size = (e->attr & FA_DIR) ? 0 : e->size;
}

static long s_next(void *ctx, FINFO *out)
{
    VFS *v = ctx;
    while (v->it_i < v->n) {
        VENT *e = &v->ent[v->it_i++];
        if (e->parent == v->it_parent) {
            to_finfo(e, out);
            return 0;
        }
    }
    return ENMFIL;
}

static long s_first(void *ctx, const char *dir, FINFO *out)
{
    VFS *v = ctx;
    int d = resolve(v, dir);
    if (d == -2 || (d >= 0 && !(v->ent[d].attr & FA_DIR))) return EPTHNF;
    v->it_parent = d;
    v->it_i = 0;
    return s_next(ctx, out) == 0 ? 0 : EFILNF;
}

/* ---- lecture du conteneur ---- */

static long read_at(long h, long off, void *buf, long n)
{
    long r = sys_seek((int)h, off, 0);
    if (r < 0) return r;
    if (r != off) return TE_BADARC;
    r = sys_read((int)h, n, buf);
    if (r < 0) return r;
    return r == n ? 0 : TE_BADARC;
}

static unsigned int le16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned long le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}
static unsigned int be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }

/* ---- images : secteurs ---- */

static long msa_track(VFS *v, long index)
{
    unsigned char hdr[2];
    long len, r, want = (long)v->spt * 512, i = 0, o = 0;
    if (v->trk_cached == index) return 0;
    v->trk_cached = -1;
    r = read_at(v->fh, v->trk[index], hdr, 2);
    if (r) return r;
    len = be16(hdr);
    if (len == want) {
        r = read_at(v->fh, v->trk[index] + 2, v->trkbuf, want);
        if (r) return r;
    } else {
        /* RLE : $E5, octet, compte (mot) ; sinon l'octet lui-meme. La
         * piste compressee est lue a la suite du tampon decompresse. */
        unsigned char *z = v->trkbuf + want;
        if (len > want) return TE_BADARC;
        r = read_at(v->fh, v->trk[index] + 2, z, len);
        if (r) return r;
        while (i < len) {
            unsigned char c = z[i++];
            if (c == 0xe5) {
                long cnt;
                if (i + 3 > len) return TE_BADARC;
                c = z[i];
                cnt = be16(z + i + 1);
                i += 3;
                if (o + cnt > want) return TE_BADARC;
                memset(v->trkbuf + o, c, cnt);
                o += cnt;
            } else {
                if (o >= want) return TE_BADARC;
                v->trkbuf[o++] = c;
            }
        }
        if (o != want) return TE_BADARC;
    }
    v->trk_cached = index;
    return 0;
}

static long read_sector(VFS *v, long lsn, unsigned char *buf)
{
    if (lsn < 0 || lsn >= v->nsects) return TE_BADARC;
    if (!v->msa) return read_at(v->fh, lsn * 512, buf, 512);
    {
        long track = lsn / v->spt, side = track % v->sides, cyl = track / v->sides, r;
        if (cyl < v->t0 || cyl > v->t1) return TE_BADARC;
        r = msa_track(v, (cyl - v->t0) * v->sides + side);
        if (r) return r;
        memcpy(buf, v->trkbuf + (lsn % v->spt) * 512, 512);
        return 0;
    }
}

static long img_geometry(VFS *v, long filesize)
{
    unsigned char h[10];
    long r, off, i, n;
    if (!v->msa) {
        v->nsects = filesize / 512;
        return 0;
    }
    r = read_at(v->fh, 0, h, 10);
    if (r) return r;
    if (be16(h) != 0x0e0f) return TE_BADARC;
    v->spt = (int)be16(h + 2);
    v->sides = (int)be16(h + 4) + 1;
    v->t0 = (int)be16(h + 6);
    v->t1 = (int)be16(h + 8);
    if (v->spt < 1 || v->spt > 24 || v->sides < 1 || v->sides > 2 || v->t0 > v->t1 || v->t1 > 90)
        return TE_BADARC;
    n = (long)(v->t1 - v->t0 + 1) * v->sides;
    v->trk = sys_alloc(n * (long)sizeof(long));
    v->trkbuf = sys_alloc((long)v->spt * 512 * 2);
    if (!v->trk || !v->trkbuf) return ENSMEM;
    off = 10;
    for (i = 0; i < n; i++) {
        unsigned char l[2];
        if (off + 2 > filesize) return TE_BADARC;
        r = read_at(v->fh, off, l, 2);
        if (r) return r;
        v->trk[i] = off;
        off += 2 + be16(l);
    }
    if (off > filesize) return TE_BADARC;
    v->trk_cached = -1;
    v->nsects = (long)(v->t1 + 1) * v->sides * v->spt;
    return 0;
}

/* Liste un dossier de l'image (racine si cluster 0) sous parent. */
static long img_dir(VFS *v, long cluster, int parent)
{
    long sector, count, r, steps = 0;
    int k;
    if (cluster == 0) {
        sector = v->root_start;
        count = v->ndirs / 16;
    } else {
        sector = v->data_start + (cluster - 2) * v->spc;
        count = v->spc;
    }
    for (;;) {
        long s;
        for (s = 0; s < count; s++) {
            r = read_sector(v, sector + s, v->sec);
            if (r) return r;
            for (k = 0; k < 512; k += 32) {
                unsigned char *d = v->sec + k;
                char name[13];
                int i, n = 0, idx;
                if (d[0] == 0) return 0;
                if (d[0] == 0xe5 || d[11] == 0x0f || (d[11] & FA_LABEL)) continue;
                if (d[0] == '.') continue;
                for (i = 0; i < 8 && d[i] != ' '; i++) name[n++] = (char)(i == 0 && d[0] == 5 ? 0xe5 : d[i]);
                if (d[8] != ' ') {
                    name[n++] = '.';
                    for (i = 8; i < 11 && d[i] != ' '; i++) name[n++] = (char)d[i];
                }
                name[n] = 0;
                idx = add_entry(v, parent, name, d[11] & (FA_DIR | FA_RDONLY | FA_HIDDEN | FA_SYSTEM | FA_ARCH));
                if (idx < 0) return TE_TOOMANY;
                v->ent[idx].time = (unsigned short)le16(d + 22);
                v->ent[idx].date = (unsigned short)le16(d + 24);
                v->ent[idx].pos = (long)le16(d + 26);
                v->ent[idx].size = (d[11] & FA_DIR) ? 0 : le32(d + 28);
            }
        }
        if (cluster == 0) return 0;
        if (++steps > v->ncl) return TE_BADARC;
        cluster = v->fat[cluster];
        if (cluster >= 0xff8) return 0;
        if (cluster < 2 || cluster >= v->ncl + 2) return TE_BADARC;
        sector = v->data_start + (cluster - 2) * v->spc;
    }
}

static long open_image(VFS *v, long filesize)
{
    unsigned char *b = v->sec;
    long r, i;
    r = img_geometry(v, filesize);
    if (r) return r;
    r = read_sector(v, 0, b);
    if (r) return r;
    v->spc = b[13];
    v->res = (int)le16(b + 14);
    v->nfats = b[16];
    v->ndirs = (int)le16(b + 17);
    if (le16(b + 11) != 512 || !(v->spc == 1 || v->spc == 2 || v->spc == 4 || v->spc == 8 || v->spc == 16) ||
        v->res < 1 || v->nfats < 1 || v->nfats > 2 || v->ndirs < 16 || v->ndirs > 1024 || (v->ndirs & 15))
        return TE_BADARC;
    v->spf = (int)le16(b + 22);
    i = (long)le16(b + 19);
    if (v->spf < 1 || v->spf > 32 || i < 16 || i > v->nsects) return TE_BADARC;
    v->nsects = i;
    v->root_start = v->res + (long)v->nfats * v->spf;
    v->data_start = v->root_start + v->ndirs / 16;
    if (v->data_start >= v->nsects) return TE_BADARC;
    v->ncl = (v->nsects - v->data_start) / v->spc;
    if (v->ncl > 4084 || (v->ncl + 2) * 3 / 2 > (long)v->spf * 512) return TE_BADARC;
    v->fat = sys_alloc((v->ncl + 2) * 2);
    if (!v->fat) return ENSMEM;
    {
        /* La premiere FAT en entier, puis ses entrees de 12 bits. */
        unsigned char *raw = sys_alloc((long)v->spf * 512);
        if (!raw) return ENSMEM;
        for (i = 0; i < v->spf; i++) {
            r = read_sector(v, v->res + i, raw + i * 512);
            if (r) { sys_free(raw); return r; }
        }
        for (i = 0; i < v->ncl + 2; i++) {
            long byte = i * 3 / 2;
            unsigned int w = raw[byte] | (raw[byte + 1] << 8);
            v->fat[i] = (unsigned short)((i & 1) ? w >> 4 : w & 0xfff);
        }
        sys_free(raw);
    }
    /* Racine, puis chaque dossier trouve, en largeur. */
    r = img_dir(v, 0, -1);
    if (r) return r;
    for (i = 0; i < v->n; i++) {
        VENT *e = &v->ent[i];
        if (!(e->attr & FA_DIR)) continue;
        if (depth_of(v, (int)i) > MAX_DEPTH) return TE_TOODEEP;
        if (e->pos < 2 || e->pos >= v->ncl + 2) return TE_BADARC;
        r = img_dir(v, e->pos, (int)i);
        if (r) return r;
    }
    fmt_ulong(v->desc, (unsigned long)(v->nsects / 2), 0);
    str_add(v->desc, v->msa ? " KB MSA image" : " KB ST image", sizeof v->desc);
    return 0;
}

static long img_read(VFS *v, long n, unsigned char *out)
{
    long done = 0, csize = (long)v->spc * 512;
    while (done < n && v->remaining > 0) {
        long lsn, k, r;
        if (v->cl < 2 || v->cl >= v->ncl + 2) return TE_BADARC;
        lsn = v->data_start + (v->cl - 2) * v->spc + v->cloff / 512;
        if (lsn != v->cached_sector) {
            r = read_sector(v, lsn, v->sec);
            if (r) return r;
            v->cached_sector = lsn;
        }
        k = 512 - v->cloff % 512;
        if (k > n - done) k = n - done;
        if (k > v->remaining) k = v->remaining;
        memcpy(out + done, v->sec + v->cloff % 512, k);
        done += k;
        v->remaining -= k;
        v->cloff += k;
        if (v->cloff == csize) {
            v->cloff = 0;
            if (++v->clsteps > v->ncl) return TE_BADARC;
            v->cl = v->fat[v->cl];
            if (v->remaining > 0 && (v->cl < 2 || v->cl >= v->ncl + 2)) return TE_BADARC;
        }
    }
    return done;
}

/* ---- LZH ---- */

static long open_lzh(VFS *v, long filesize)
{
    unsigned char *hb = sys_alloc(1024);
    long off = 0, r = 0;
    if (!hb) return ENSMEM;
    while (off < filesize) {
        LZHENTRY e;
        long got = filesize - off > 1024 ? 1024 : filesize - off;
        int st, idx, parent, m;
        char full[128];
        r = read_at(v->fh, off, hb, got);
        if (r) break;
        st = lzh_header(hb, got, 0, &e);
        if (st == LZH_END) break;
        if (st != LZH_OK || e.next <= 0 || off + e.next > filesize) { r = TE_BADARC; break; }
        str_copy(full, e.dir, sizeof full);
        str_add(full, e.name, sizeof full);
        if (!memcmp(e.method, "-lhd-", 5)) {
            if (ensure_dirs(v, -1, full) < 0) { r = TE_TOOMANY; break; }
        } else {
            char *last = strrchr(full, '\\');
            char *last2 = strrchr(full, '/');
            if (last2 > last) last = last2;
            parent = -1;
            if (last) {
                *last = 0;
                parent = ensure_dirs(v, -1, full);
                *last = '\\';
                if (parent < 0) { r = TE_TOOMANY; break; }
            }
            idx = add_entry(v, parent, last ? last + 1 : full, FA_ARCH);
            if (idx < 0) { r = TE_TOOMANY; break; }
            m = !memcmp(e.method, "-lh0-", 5) ? 0 : !memcmp(e.method, "-lh5-", 5) ? 5 : 255;
            v->ent[idx].method = (unsigned char)m;
            v->ent[idx].size = (unsigned long)e.size;
            v->ent[idx].packed = e.packed;
            v->ent[idx].pos = off + e.data;
            v->ent[idx].crc = e.crc;
            v->ent[idx].time = e.time;
            v->ent[idx].date = e.date;
        }
        off += e.next;
    }
    sys_free(hb);
    if (!r) {
        fmt_ulong(v->desc, (unsigned long)v->n, 0);
        str_add(v->desc, " entries, LHA archive", sizeof v->desc);
    }
    return r;
}

static long load_lzh(VFS *v, VENT *e, long *err)
{
    unsigned char *packed, *work;
    LZHENTRY le;
    long r;
    if (e->method == 255) { *err = TE_METHOD; return 0; }
    packed = sys_alloc(e->packed + 4);
    work = sys_alloc(LZH_WORK_BYTES);
    v->buf = sys_alloc((long)e->size + 4);
    if (!packed || !work || !v->buf) { *err = TE_BIG; r = 0; goto out; }
    r = read_at(v->fh, e->pos, packed, e->packed);
    if (r) { *err = r; goto out; }
    memset(&le, 0, sizeof le);
    memcpy(le.method, e->method == 0 ? "-lh0-" : "-lh5-", 5);
    le.packed = e->packed;
    le.size = (long)e->size;
    le.crc = (unsigned short)e->crc;
    r = lzh_extract(packed, e->packed, &le, v->buf, work);
    *err = r == LZH_OK ? 0 : r == LZH_METHOD ? TE_METHOD : TE_BADARC;
out:
    sys_free(packed);
    sys_free(work);
    return *err == 0;
}

/* ---- ZIP ---- */

static long open_zip(VFS *v, long filesize)
{
    long tail = filesize > 65557L ? 65557L : filesize, i, r, cd, count, pos;
    unsigned char *t = sys_alloc(tail);
    unsigned char h[46];
    if (!t) return ENSMEM;
    r = read_at(v->fh, filesize - tail, t, tail);
    if (r) { sys_free(t); return r; }
    for (i = tail - 22; i >= 0; i--)
        if (t[i] == 'P' && t[i + 1] == 'K' && t[i + 2] == 5 && t[i + 3] == 6) break;
    if (i < 0) { sys_free(t); return TE_BADARC; }
    count = (long)le16(t + i + 10);
    cd = (long)le32(t + i + 16);
    sys_free(t);
    if (cd < 0 || cd >= filesize) return TE_BADARC;
    pos = cd;
    for (i = 0; i < count; i++) {
        char name[128];
        long nl, xl, cl, idx, parent;
        int dir, m;
        unsigned int flags;
        r = read_at(v->fh, pos, h, 46);
        if (r) return r;
        if (le32(h) != 0x02014b50UL) return TE_BADARC;
        flags = le16(h + 8);
        m = (int)le16(h + 10);
        nl = (long)le16(h + 28);
        xl = (long)le16(h + 30);
        cl = (long)le16(h + 32);
        if (nl == 0 || nl > 127) return TE_BADARC;
        r = read_at(v->fh, pos + 46, name, nl);
        if (r) return r;
        name[nl] = 0;
        dir = name[nl - 1] == '/';
        if (dir) {
            if (ensure_dirs(v, -1, name) < 0) return TE_TOOMANY;
        } else {
            char *last = strrchr(name, '/');
            char *lb = strrchr(name, '\\');
            if (lb > last) last = lb;
            parent = -1;
            if (last) {
                *last = 0;
                parent = ensure_dirs(v, -1, name);
                *last = '/';
                if (parent < 0) return TE_TOOMANY;
            }
            idx = add_entry(v, (int)parent, last ? last + 1 : name,
                            (h[5] == 0 ? h[38] & (FA_RDONLY | FA_HIDDEN | FA_SYSTEM) : 0) | FA_ARCH);
            if (idx < 0) return TE_TOOMANY;
            v->ent[idx].method = (unsigned char)((flags & 1) ? 255 : (m == 0 || m == 8) ? m : 255);
            v->ent[idx].time = (unsigned short)le16(h + 12);
            v->ent[idx].date = (unsigned short)le16(h + 14);
            v->ent[idx].crc = le32(h + 16);
            v->ent[idx].packed = (long)le32(h + 20);
            v->ent[idx].size = le32(h + 24);
            v->ent[idx].pos = (long)le32(h + 42);         /* en-tete local */
            if (v->ent[idx].packed < 0 || (long)v->ent[idx].size < 0) return TE_BADARC;
        }
        pos += 46 + nl + xl + cl;
    }
    fmt_ulong(v->desc, (unsigned long)v->n, 0);
    str_add(v->desc, " entries, ZIP archive", sizeof v->desc);
    return 0;
}

static long load_zip(VFS *v, VENT *e, long *err)
{
    unsigned char h[30], *packed = 0, *work = 0;
    long r, data;
    if (e->method == 255) { *err = TE_METHOD; return 0; }
    r = read_at(v->fh, e->pos, h, 30);
    if (r || le32(h) != 0x04034b50UL) { *err = r ? r : TE_BADARC; return 0; }
    data = e->pos + 30 + (long)le16(h + 26) + (long)le16(h + 28);
    v->buf = sys_alloc((long)e->size + 4);
    if (!v->buf) { *err = TE_BIG; return 0; }
    if (e->method == 0) {
        if (e->packed != (long)e->size) { *err = TE_BADARC; return 0; }
        r = read_at(v->fh, data, v->buf, (long)e->size);
        if (r) { *err = r; return 0; }
    } else {
        packed = sys_alloc(e->packed + 4);
        work = sys_alloc(INFLATE_WORK_BYTES);
        if (!packed || !work) { *err = TE_BIG; goto out; }
        r = read_at(v->fh, data, packed, e->packed);
        if (r) { *err = r; goto out; }
        if (inflate_raw(packed, e->packed, v->buf, (long)e->size, work)) { *err = TE_BADARC; goto out; }
    }
    *err = crc32_update(0, v->buf, (long)e->size) == e->crc ? 0 : TE_BADARC;
out:
    sys_free(packed);
    sys_free(work);
    return *err == 0;
}

/* ---- ARC ---- */

static long open_arc(VFS *v, long filesize)
{
    unsigned char h[29];
    long off = 0, r;
    while (off < filesize) {
        ARCHDR a;
        int idx;
        long got = filesize - off > 29 ? 29 : filesize - off;
        r = read_at(v->fh, off, h, got);
        if (r) return r;
        r = arc_header(h, got, &a);
        if (r == ARC_END) break;
        if (r != ARC_OK) return TE_BADARC;
        idx = add_entry(v, -1, a.name, FA_ARCH);
        if (idx < 0) return TE_TOOMANY;
        v->ent[idx].method = (unsigned char)a.method;
        v->ent[idx].size = (unsigned long)a.size;
        v->ent[idx].packed = a.packed;
        v->ent[idx].pos = off + a.hsize;
        v->ent[idx].crc = a.crc;
        v->ent[idx].time = a.time;
        v->ent[idx].date = a.date;
        off += a.hsize + a.packed;
        if (off > filesize) return TE_BADARC;
    }
    fmt_ulong(v->desc, (unsigned long)v->n, 0);
    str_add(v->desc, " entries, ARC archive", sizeof v->desc);
    return 0;
}

static long load_arc(VFS *v, VENT *e, long *err)
{
    unsigned char *packed, *work;
    long r;
    if (!arc_method_ok(e->method)) { *err = TE_METHOD; return 0; }
    packed = sys_alloc(e->packed + 4);
    work = sys_alloc(ARC_WORK_BYTES);
    v->buf = sys_alloc((long)e->size + 4);
    if (!packed || !work || !v->buf) { *err = TE_BIG; goto out; }
    r = read_at(v->fh, e->pos, packed, e->packed);
    if (r) { *err = r; goto out; }
    r = arc_extract(e->method, packed, e->packed, v->buf, (long)e->size, (unsigned short)e->crc, work);
    *err = r == ARC_OK ? 0 : r == ARC_METHOD ? TE_METHOD : TE_BADARC;
out:
    sys_free(packed);
    sys_free(work);
    return *err == 0;
}

/* ---- fichiers ---- */

static long s_close(void *ctx, long h)
{
    VFS *v = ctx;
    (void)h;
    if (v->fh >= 0) sys_close((int)v->fh);
    v->fh = -1;
    sys_free(v->buf);
    v->buf = 0;
    v->of = -1;
    return 0;
}

static long s_open(void *ctx, const char *path)
{
    VFS *v = ctx;
    int i = resolve(v, path);
    VENT *e;
    long err = 0;
    if (i < 0) return EFILNF;
    e = &v->ent[i];
    if (e->attr & FA_DIR) return EACCDN;
    if (v->of >= 0) s_close(v, 0);
    v->fh = sys_open(v->path, 0);
    if (v->fh < 0) { long r = v->fh; v->fh = -1; return r; }
    {
        /* Le conteneur a-t-il change depuis la lecture du catalogue
         * (ecrase depuis l'autre panneau, disquette changee) ? */
        unsigned short t = 0, d = 0;
        long size = sys_seek((int)v->fh, 0, 2);
        if (size != v->csize || sys_gettime((int)v->fh, &t, &d) || t != v->ctime || d != v->cdate
            || sys_seek((int)v->fh, 0, 0) != 0) {
            sys_close((int)v->fh);
            v->fh = -1;
            return TE_CHANGED;
        }
    }
    v->of = i;
    v->remaining = (long)e->size;
    v->bufpos = 0;
    if (v->kind == VK_IMAGE) {
        v->cl = e->pos;
        v->cloff = 0;
        v->clsteps = 0;
        v->cached_sector = -1;
        return 1;
    }
    if (v->kind == VK_LZH) load_lzh(v, e, &err);
    else if (v->kind == VK_ZIP) load_zip(v, e, &err);
    else load_arc(v, e, &err);
    /* Tout est en memoire : le conteneur peut etre referme. */
    sys_close((int)v->fh);
    v->fh = -1;
    if (err) { s_close(v, 0); return err; }
    return 1;
}

static long s_read(void *ctx, long h, long n, void *buf)
{
    VFS *v = ctx;
    (void)h;
    if (v->of < 0) return EIHNDL;
    if (v->kind == VK_IMAGE) return img_read(v, n, buf);
    if (n > v->remaining) n = v->remaining;
    memcpy(buf, v->buf + v->bufpos, n);
    v->bufpos += n;
    v->remaining -= n;
    return n;
}

/* ---- ouverture ---- */

static int ext_is(const char *name, const char *ext)
{
    const char *d = strrchr(name, '.');
    if (!d) return 0;
    for (d++; *d && *ext; d++, ext++)
        if (toupper((unsigned char)*d) != *ext) return 0;
    return *d == 0 && *ext == 0;
}

int vfs_kind_of_name(const char *name)
{
    if (ext_is(name, "ST") || ext_is(name, "MSA")) return VK_IMAGE;
    if (ext_is(name, "LZH") || ext_is(name, "LHA")) return VK_LZH;
    if (ext_is(name, "ZIP")) return VK_ZIP;
    if (ext_is(name, "ARC")) return VK_ARC;
    return VK_NONE;
}

void vfs_close(VFS *v)
{
    if (!v) return;
    s_close(v, 0);
    sys_free(v->trk);
    sys_free(v->trkbuf);
    sys_free(v->fat);
    sys_free(v->ent);
    sys_free(v);
}

long vfs_open(VFS **out, const char *path, const char *root)
{
    VFS *v;
    long r, size;
    const char *name = strrchr(path, '\\');
    *out = 0;
    v = sys_alloc(sizeof *v);
    if (!v) return ENSMEM;
    memset(v, 0, sizeof *v);
    v->of = -1;
    v->fh = -1;
    v->kind = vfs_kind_of_name(name ? name + 1 : path);
    v->msa = ext_is(path, "MSA");
    if (v->kind == VK_NONE) { sys_free(v); return TE_BADARC; }
    if (str_copy(v->path, path, sizeof v->path) || str_copy(v->root, root, sizeof v->root)) {
        sys_free(v);
        return TE_TOOLONG;
    }
    v->ent = sys_alloc((long)VFS_MAX * sizeof(VENT));
    if (!v->ent) { sys_free(v); return ENSMEM; }
    v->fh = sys_open(path, 0);
    if (v->fh < 0) { r = v->fh; v->fh = -1; vfs_close(v); return r; }
    size = sys_seek((int)v->fh, 0, 2);
    v->csize = size;
    if (size < 0) r = size;
    else if ((r = sys_gettime((int)v->fh, &v->ctime, &v->cdate)) != 0) {}
    else if (v->kind == VK_IMAGE) r = open_image(v, size);
    else if (v->kind == VK_LZH) r = open_lzh(v, size);
    else if (v->kind == VK_ZIP) r = open_zip(v, size);
    else r = open_arc(v, size);
    sys_close((int)v->fh);
    v->fh = -1;
    if (r) { vfs_close(v); return r; }
    v->src.ctx = v;
    v->src.first = s_first;
    v->src.next = s_next;
    v->src.open = s_open;
    v->src.read = s_read;
    v->src.close = s_close;
    *out = v;
    return 0;
}

const SOURCE *vfs_source(VFS *v) { return &v->src; }
const char *vfs_root(const VFS *v) { return v->root; }
const char *vfs_describe(const VFS *v) { return v->desc; }
