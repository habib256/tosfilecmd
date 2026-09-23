/*
 * disk.c -- outils de disquette (voir disk.h pour les regles de securite).
 *
 * Une piste de l'image = (cylindre, face) ; dans un fichier .ST elles se
 * suivent cylindre par cylindre, face 0 puis face 1. Les ecritures se font
 * par cylindre, dans l'ordre 1, 2, ..., n-1 puis 0.
 */
#include "disk.h"
#include "vfs.h"
#include "libc.h"

#define RETRIES 3

static unsigned int le16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned int be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static void put16(unsigned char *p, unsigned int v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }

long disk_bytes(const GEOM *g) { return (long)g->spt * g->sides * g->tracks * 512; }

long disk_geom_boot(const unsigned char *b, GEOM *g)
{
    long nsects = le16(b + 19), per;
    g->spt = (int)le16(b + 24);
    g->sides = (int)le16(b + 26);
    if (le16(b + 11) != 512 || g->spt < 1 || g->spt > DISK_SPT_MAX || g->sides < 1 || g->sides > 2)
        return TE_NOFLOPPY;
    per = (long)g->spt * g->sides;
    if (nsects == 0 || nsects % per) return TE_NOFLOPPY;
    g->tracks = (int)(nsects / per);
    if (g->tracks < 1 || g->tracks > DISK_TRACK_MAX) return TE_NOFLOPPY;
    return 0;
}

long disk_geom_size(long size, GEOM *g)
{
    static const signed char sides[] = { 2, 1 };
    long n = size / 512;
    int i, spt;
    if (size <= 0 || size % 512) return TE_NOFLOPPY;
    for (i = 0; i < 2; i++)
        for (spt = 9; spt <= DISK_SPT_MAX; spt++) {
            long per = (long)spt * sides[i];
            if (n % per == 0 && n / per >= 78 && n / per <= DISK_TRACK_MAX) {
                g->spt = spt;
                g->sides = sides[i];
                g->tracks = (int)(n / per);
                return 0;
            }
        }
    return TE_NOFLOPPY;
}

void disk_describe(char *out, const GEOM *g)
{
    char num[12];
    fmt_ulong(num, (unsigned long)(disk_bytes(g) / 1024), 0);
    str_copy(out, num, 40);
    str_add(out, " KB: ", 40);
    fmt_ulong(num, (unsigned long)g->tracks, 0);
    str_add(out, num, 40);
    str_add(out, " tracks, ", 40);
    fmt_ulong(num, (unsigned long)g->spt, 0);
    str_add(out, num, 40);
    str_add(out, g->sides == 2 ? " sectors, 2 sides" : " sectors, 1 side", 40);
}

/* ---- acces aux secteurs, avec reprises ---- */

static long rd(int dev, void *buf, int sect, int track, int side, int count)
{
    long r = 0;
    int k;
    for (k = 0; k < RETRIES; k++) {
        r = sys_floprd(buf, dev, sect, track, side, count);
        if (r == 0) return 0;
        if (r == E_CHNG || r == EDRVNR) break;
    }
    return r ? r : EREADF;
}

static long wr(int dev, const void *buf, int sect, int track, int side, int count)
{
    long r = 0;
    int k;
    for (k = 0; k < RETRIES; k++) {
        r = sys_flopwr(buf, dev, sect, track, side, count);
        if (r == 0) return 0;
        if (r == EWRPRO || r == E_CHNG || r == EDRVNR) break;
    }
    return r ? r : EWRITF;
}

long disk_probe(DISK *d, int dev, unsigned char *boot, GEOM *g)
{
    long r = rd(dev, boot, 1, 0, 0, 1);
    (void)d;
    if (r) return r;
    return g ? disk_geom_boot(boot, g) : 0;
}

void disk_note_program(DISK *d, int dev)
{
    d->prog_dev = -1;
    if (dev < 0 || dev > 1) return;
    if (disk_probe(d, dev, d->prog_boot, 0) == 0) d->prog_dev = dev;
}

/* La disquette de dev peut-elle etre ecrasee ? Illisible (vierge) : oui. */
static long guard(DISK *d, int dev, unsigned char *boot)
{
    if (disk_probe(d, dev, boot, 0)) return 0;
    if (d->prog_dev >= 0 && !memcmp(boot, d->prog_boot, 512)) return TE_PROGDISK;
    return 0;
}

/* Formate, ecrit et relit une piste. work : DISK_WORK + spt * 512 octets. */
static long put_track(int dev, const GEOM *g, int track, int side, const unsigned char *data,
                      unsigned char *work)
{
    long r;
    unsigned char *check = work + DISK_WORK;
    long n = (long)g->spt * 512;
    r = sys_flopfmt(work, dev, g->spt, track, side);
    if (r) return r;
    r = wr(dev, data, 1, track, side, g->spt);
    if (r) return r;
    r = rd(dev, check, 1, track, side, g->spt);
    if (r) return r;
    return memcmp(check, data, n) ? TE_VERIFY : 0;
}

/* Cylindre de rang i dans l'ordre d'ecriture : 1, 2, ..., n-1, puis 0. */
static int cyl_at(const GEOM *g, long i) { return i + 1 < g->tracks ? (int)(i + 1) : 0; }

/* ---- disquette -> fichier : une SOURCE pour fsops ---- */

static long f_first(void *ctx, const char *dir, FINFO *out)
{
    FLOPSRC *f = ctx;
    (void)dir;
    memset(out, 0, sizeof *out);
    str_copy(out->name, f->name, sizeof out->name);
    out->size = (unsigned long)f->size;
    out->attr = 0;
    out->time = f->time;
    out->date = f->date;
    f->listed = 1;
    return 0;
}

static long f_next(void *ctx, FINFO *out) { (void)ctx; (void)out; return ENMFIL; }

static long f_open(void *ctx, const char *path)
{
    FLOPSRC *f = ctx;
    const char *s = strrchr(path, '\\');
    if (!s || strcmp(s + 1, f->name)) return EFILNF;
    f->pos = 0;
    f->cached = -1;
    return 1;
}

static long f_read(void *ctx, long h, long n, void *buf)
{
    FLOPSRC *f = ctx;
    long tb = (long)f->g.spt * 512, done = 0;
    (void)h;
    if (n > f->size - f->pos) n = f->size - f->pos;
    while (done < n) {
        long ts = f->pos / tb, off = f->pos % tb, k = tb - off;
        if (ts != f->cached) {
            long r = rd(f->dev, f->trk, 1, (int)(ts / f->g.sides), (int)(ts % f->g.sides), f->g.spt);
            if (r) { f->cached = -1; return r; }
            f->cached = ts;
        }
        if (k > n - done) k = n - done;
        memcpy((unsigned char *)buf + done, f->trk + off, k);
        done += k;
        f->pos += k;
    }
    return done;
}

static long f_close(void *ctx, long h) { (void)ctx; (void)h; return 0; }

long disk_source(FLOPSRC *f, DISK *d, int dev, const char *name)
{
    unsigned char boot[512];
    long r;
    memset(f, 0, sizeof *f);
    f->d = d;
    f->dev = dev;
    r = disk_probe(d, dev, boot, &f->g);
    if (r) return r;
    if (str_copy(f->name, name, sizeof f->name)) return TE_BADNAME;
    f->trk = sys_alloc((long)DISK_SPT_MAX * 512);
    if (!f->trk) return ENSMEM;
    f->size = disk_bytes(&f->g);
    f->cached = -1;
    sys_now(&f->time, &f->date);
    f->src.ctx = f;
    f->src.first = f_first;
    f->src.next = f_next;
    f->src.open = f_open;
    f->src.read = f_read;
    f->src.close = f_close;
    return 0;
}

void disk_source_end(FLOPSRC *f)
{
    sys_free(f->trk);
    f->trk = 0;
}

/* ---- image -> disquette ---- */

typedef struct {
    long fh;
    int msa;
    GEOM g;
    long *idx;                          /* MSA : position de chaque piste */
} IMGR;

static long img_open(IMGR *m, const char *path)
{
    unsigned char h[512];
    long size, r, i, n, off;
    const char *dot = strrchr(path, '.');
    memset(m, 0, sizeof *m);
    m->msa = dot && toupper((unsigned char)dot[1]) == 'M';
    m->fh = sys_open(path, 0);
    if (m->fh < 0) return m->fh;
    size = sys_seek((int)m->fh, 0, 2);
    if (size < 0) return size;
    if (sys_seek((int)m->fh, 0, 0) != 0) return ERROR;
    if (!m->msa) {
        if (size < 512) return TE_NOFLOPPY;
        r = sys_read((int)m->fh, 512, h);
        if (r != 512) return r < 0 ? r : TE_NOFLOPPY;
        if (disk_geom_boot(h, &m->g) || disk_bytes(&m->g) != size)
            if (disk_geom_size(size, &m->g)) return TE_NOFLOPPY;
        return 0;
    }
    r = sys_read((int)m->fh, 10, h);
    if (r != 10) return r < 0 ? r : TE_NOFLOPPY;
    if (be16(h) != 0x0e0f || be16(h + 6) != 0) return TE_NOFLOPPY;   /* image partielle : non */
    m->g.spt = (int)be16(h + 2);
    m->g.sides = (int)be16(h + 4) + 1;
    m->g.tracks = (int)be16(h + 8) + 1;
    if (m->g.spt < 1 || m->g.spt > DISK_SPT_MAX || m->g.sides > 2 || m->g.tracks > DISK_TRACK_MAX)
        return TE_NOFLOPPY;
    n = (long)m->g.tracks * m->g.sides;
    m->idx = sys_alloc(n * (long)sizeof(long));
    if (!m->idx) return ENSMEM;
    off = 10;
    for (i = 0; i < n; i++) {
        if (off + 2 > size) return TE_NOFLOPPY;
        if (sys_seek((int)m->fh, off, 0) != off || sys_read((int)m->fh, 2, h) != 2) return TE_NOFLOPPY;
        m->idx[i] = off;
        off += 2 + be16(h);
    }
    return off > size ? TE_NOFLOPPY : 0;
}

/* Piste (cyl, face) de l'image dans out ; z : spt * 512 octets de travail. */
static long img_track(IMGR *m, int cyl, int side, unsigned char *out, unsigned char *z)
{
    long want = (long)m->g.spt * 512, i = (long)cyl * m->g.sides + side, r, len;
    unsigned char h[2];
    if (!m->msa) {
        long off = i * want;
        if (sys_seek((int)m->fh, off, 0) != off) return ERROR;
        r = sys_read((int)m->fh, want, out);
        return r == want ? 0 : r < 0 ? r : TE_NOFLOPPY;
    }
    if (sys_seek((int)m->fh, m->idx[i], 0) != m->idx[i] || sys_read((int)m->fh, 2, h) != 2)
        return TE_NOFLOPPY;
    len = be16(h);
    if (len == want) {
        r = sys_read((int)m->fh, want, out);
        return r == want ? 0 : r < 0 ? r : TE_NOFLOPPY;
    }
    if (len > want) return TE_NOFLOPPY;
    r = sys_read((int)m->fh, len, z);
    if (r != len) return r < 0 ? r : TE_NOFLOPPY;
    return msa_unpack(z, len, out, want) ? TE_NOFLOPPY : 0;
}

static void img_close(IMGR *m)
{
    if (m->fh >= 0) sys_close((int)m->fh);
    sys_free(m->idx);
    m->fh = -1;
    m->idx = 0;
}

/* Le lecteur physique de dev ; avec un seul lecteur, B: est A:. */
static int unit(int dev) { return sys_nflops() < 2 ? 0 : dev; }

long disk_write_image(DISK *d, const char *path, int dev)
{
    IMGR m;
    long r, i, tb;
    unsigned char boot[512], *data, *z, *work;
    int pd = toupper((unsigned char)path[0]) - 'A';

    d->track_err = -1;
    if ((pd == 0 || pd == 1) && unit(pd) == unit(dev)) return TE_SELF;   /* l'image est sur la cible */
    r = img_open(&m, path);
    if (r) { img_close(&m); return r; }
    tb = (long)m.g.spt * 512;
    if (d->bufsize < DISK_WORK + 3 * tb) { img_close(&m); return ENSMEM; }
    work = d->buf;
    data = d->buf + DISK_WORK + tb;
    z = data + tb;
    r = guard(d, dev, boot);
    for (i = 0; !r && i < m.g.tracks; i++) {
        int cyl = cyl_at(&m.g, i), side;
        for (side = 0; !r && side < m.g.sides; side++) {
            r = img_track(&m, cyl, side, data, z);
            if (!r) r = put_track(dev, &m.g, cyl, side, data, work);
            if (r) d->track_err = cyl;
        }
        if (!r && d->progress && d->progress(d, i + 1, m.g.tracks)) r = TE_CANCEL;
    }
    img_close(&m);
    return r;
}

/* ---- copie ---- */

/* La source est-elle dans le lecteur ? Un seul lecteur : on la redemande. */
static long want_source(DISK *d, int dev, const unsigned char *sboot, int single, unsigned char *tmp)
{
    int again = 0;
    for (;;) {
        if (disk_probe(d, dev, tmp, 0) == 0 && !memcmp(tmp, sboot, 512)) return 0;
        if (!single) return TE_CHANGED;
        again = 1;
        if (!d->insert || !d->insert(d, 0, dev, again)) return TE_CANCEL;
    }
}

/* La cible est-elle dans le lecteur : pas la source, pas le programme. */
static long want_target(DISK *d, int dev, const unsigned char *sboot, int single,
                        unsigned char *tmp)
{
    int again = 0;
    if (single && (!d->insert || !d->insert(d, 1, dev, 0))) return TE_CANCEL;
    for (;;) {
        long bad = 0;
        if (disk_probe(d, dev, tmp, 0) == 0) {
            /* Meme secteur de boot que la source : avec un seul lecteur,
             * c'est elle (ou une ancienne copie, indiscernable) ; avec deux,
             * ce ne peut etre qu'une autre disquette, qu'on ecrase. */
            if (single && !memcmp(tmp, sboot, 512)) bad = TE_SAMEDISK;
            else if (d->prog_dev >= 0 && !memcmp(tmp, d->prog_boot, 512)) bad = TE_PROGDISK;
        }
        if (!bad) return 0;
        /* Deux lecteurs : l'utilisateur a choisi, on refuse. Un seul : la
         * source (ou une ancienne copie, indiscernable) est encore la ; on
         * redemande, l'utilisateur peut annuler. */
        if (!single) return bad;
        again = 1;
        if (!d->insert(d, 1, dev, again)) return TE_CANCEL;
    }
}

long disk_copy(DISK *d, int src, int dst)
{
    unsigned char sboot[512], tmp[512];
    GEOM g;
    long r, tb, cb, per, i, done = 0;
    int single = unit(src) == unit(dst), first = 1, side;
    unsigned char *work = d->buf, *store;

    d->track_err = -1;
    if (single && d->insert && !d->insert(d, 0, src, 0)) return TE_CANCEL;
    r = disk_probe(d, src, sboot, &g);
    if (r) return r;
    tb = (long)g.spt * 512;
    cb = tb * g.sides;
    store = d->buf + DISK_WORK + tb;
    per = (d->bufsize - DISK_WORK - tb) / cb;
    if (per < 1) return ENSMEM;
    if (per > g.tracks) per = g.tracks;
    if (!single) {
        r = want_target(d, dst, sboot, 0, tmp);
        if (r) return r;
    }
    while (done < g.tracks) {
        long n = g.tracks - done < per ? g.tracks - done : per;
        /* Lecture d'une tranche de cylindres. */
        if (!first || !single) {
            r = want_source(d, src, sboot, single, tmp);
            if (r) return r;
        }
        for (i = 0; i < n; i++) {
            int cyl = cyl_at(&g, done + i);
            for (side = 0; side < g.sides; side++) {
                r = rd(src, store + i * cb + side * tb, 1, cyl, side, g.spt);
                if (r) { d->track_err = cyl; return r; }
            }
        }
        /* Ecriture. */
        if (single) {
            r = want_target(d, dst, sboot, 1, tmp);
            if (r) return r;
        }
        first = 0;
        for (i = 0; i < n; i++) {
            int cyl = cyl_at(&g, done + i);
            for (side = 0; side < g.sides; side++) {
                r = put_track(dst, &g, cyl, side, store + i * cb + side * tb, work);
                if (r) { d->track_err = cyl; return r; }
            }
            if (d->progress && d->progress(d, done + i + 1, g.tracks)) return TE_CANCEL;
        }
        done += n;
    }
    return 0;
}

/* ---- formatage ---- */

#define FMT_SPF   5                     /* comme le TOS, pour toutes les densites */
#define FMT_NDIRS 112

long disk_format(DISK *d, int dev, int spt, int sides)
{
    GEOM g;
    unsigned char boot[512], *sec, *work = d->buf;
    long r, i, nsys, sum;
    unsigned long serial;

    d->track_err = -1;
    if (spt < 9 || spt > DISK_SPT_MAX || sides < 1 || sides > 2) return TE_NOFLOPPY;
    if (d->bufsize < DISK_WORK + 2 * 512L * DISK_SPT_MAX) return ENSMEM;
    g.spt = spt;
    g.sides = sides;
    g.tracks = 80;
    r = guard(d, dev, boot);
    if (r) return r;
    for (i = 0; i < g.tracks; i++) {
        int cyl = cyl_at(&g, i), side;
        for (side = 0; side < sides; side++) {
            r = sys_flopfmt(work, dev, spt, cyl, side);
            if (r) { d->track_err = cyl; return r; }
        }
        if (d->progress && d->progress(d, i + 1, g.tracks + 1)) return TE_CANCEL;
    }
    /* Secteurs systeme : boot, deux FAT, repertoire racine. */
    sec = work + DISK_WORK;
    nsys = 1 + 2 * FMT_SPF + FMT_NDIRS * 32 / 512;
    serial = (unsigned long)sys_random();
    for (i = 0; i < nsys; i++) {
        long lsn = i;
        int cyl = (int)(lsn / (spt * sides)), side = (int)((lsn / spt) % sides), s = (int)(lsn % spt) + 1;
        memset(sec, 0, 512);
        if (i == 0) {
            sec[0] = 0x60;
            sec[1] = 0x38;
            memcpy(sec + 2, "TOSFC ", 6);
            sec[8] = (unsigned char)serial;
            sec[9] = (unsigned char)(serial >> 8);
            sec[10] = (unsigned char)(serial >> 16);
            put16(sec + 11, 512);
            sec[13] = 2;
            put16(sec + 14, 1);
            sec[16] = 2;
            put16(sec + 17, FMT_NDIRS);
            put16(sec + 19, (unsigned int)(80 * spt * sides));
            sec[21] = sides == 2 ? 0xf9 : 0xf8;
            put16(sec + 22, FMT_SPF);
            put16(sec + 24, (unsigned int)spt);
            put16(sec + 26, (unsigned int)sides);
            /* Pas amorcable : la somme des mots ne doit pas valoir $1234. */
            for (sum = 0, r = 0; r < 512; r += 2) sum += (sec[r] << 8) | sec[r + 1];
            if ((sum & 0xffff) == 0x1234) sec[511] ^= 1;
            r = 0;
        } else if (i == 1 || i == 1 + FMT_SPF) {
            sec[0] = sides == 2 ? 0xf9 : 0xf8;
            sec[1] = 0xff;
            sec[2] = 0xff;
        }
        r = wr(dev, sec, s, cyl, side, 1);
        if (!r) r = rd(dev, sec + 512, s, cyl, side, 1);
        if (!r && memcmp(sec, sec + 512, 512)) r = TE_VERIFY;
        if (r) { d->track_err = cyl; return r; }
    }
    if (d->progress) d->progress(d, g.tracks + 1, g.tracks + 1);
    return 0;
}
