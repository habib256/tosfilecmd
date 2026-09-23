/*
 * test_disk.c -- outils de disquette sur de fausses disquettes, pannes
 * comprises.
 *
 *   test_disk DIR      (DIR : IMG.ST et IMG.MSA de tests/gen_vfs.py)
 *
 * A chaque fois : ce qui doit etre ecrit l'est octet pour octet, et ce qui
 * ne doit pas etre touche (source, disquette du programme, cible d'une
 * operation refusee ou annulee avant la fin) est compare a son etat initial.
 */
#include "fakedos.h"
#include "../src/disk.h"
#include "../src/vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests, failures;
static const char *current;
#define CHECK(c) do { tests++; if (!(c)) { failures++; \
    printf("FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, current, #c); } } while (0)

static const char *dir;
static unsigned char *img_st, *img_msa;
static long img_st_n, img_msa_n;
static unsigned char dump[FD_CYLS * 2 * FD_SPT * 512];
static unsigned char work[400000];

static unsigned char *slurp(const char *name, long *n)
{
    char path[512];
    FILE *f;
    unsigned char *d;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (!f) { printf("missing %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = malloc(*n);
    if (fread(d, 1, *n, f) != (size_t)*n) exit(2);
    fclose(f);
    return d;
}

/* Une disquette 720 Ko au contenu propre a seed, avec un BPB valable et un
 * numero de serie different. */
static void make_disk(int k, int seed, int spt, int sides)
{
    static unsigned char buf[FD_CYLS * 2 * FD_SPT * 512];
    long n = 80L * spt * sides * 512, i;
    unsigned x = (unsigned)seed * 2654435761u;
    for (i = 0; i < n; i++) { x = x * 1103515245u + 12345u; buf[i] = (unsigned char)(x >> 16); }
    memcpy(buf, img_st, 512);
    buf[8] = (unsigned char)seed;
    buf[9] = (unsigned char)(seed >> 8);
    buf[10] = 0x5a;
    buf[19] = (unsigned char)(80 * spt * sides);
    buf[20] = (unsigned char)((80 * spt * sides) >> 8);
    buf[24] = (unsigned char)spt;
    buf[26] = (unsigned char)sides;
    fd_disk_load(k, buf, n, spt, sides);
}

static long disk_len(int spt, int sides) { return 80L * spt * sides * 512; }

static int same_disks(int a, int b, int spt, int sides)
{
    static unsigned char other[FD_CYLS * 2 * FD_SPT * 512];
    if (!fd_disk_dump(a, dump, spt, sides, 80) || !fd_disk_dump(b, other, spt, sides, 80)) return 0;
    return !memcmp(dump, other, disk_len(spt, sides));
}

static void snapshot(int k, unsigned char *out) { fd_disk_dump(k, out, 9, 2, 80); }

static int cancel_at, progress_calls;
static int cb_progress(DISK *d, long done, long total)
{
    progress_calls++;
    return cancel_at && progress_calls >= cancel_at;
}

static void setup(DISK *d)
{
    fd_reset();
    memset(d, 0, sizeof *d);
    d->buf = work;
    d->bufsize = sizeof work;
    d->progress = cb_progress;
    d->prog_dev = -1;
    cancel_at = progress_calls = 0;
}

static void t_geometry(void)
{
    GEOM g;
    unsigned char b[512];
    char s[48];
    current = "geometry";
    memcpy(b, img_st, 512);
    CHECK(disk_geom_boot(b, &g) == 0 && g.spt == 9 && g.sides == 2 && g.tracks == 80);
    disk_describe(s, &g);
    CHECK(!strcmp(s, "720 KB: 80 tracks, 9 sectors, 2 sides"));
    b[11] = 0; b[12] = 1;                                   /* 256 octets/secteur */
    CHECK(disk_geom_boot(b, &g) == TE_NOFLOPPY);
    memcpy(b, img_st, 512);
    b[24] = 12;
    CHECK(disk_geom_boot(b, &g) == TE_NOFLOPPY);
    memcpy(b, img_st, 512);
    b[19]++;                                                /* pas un nombre entier de pistes */
    CHECK(disk_geom_boot(b, &g) == TE_NOFLOPPY);
    CHECK(disk_geom_size(737280, &g) == 0 && g.spt == 9 && g.sides == 2 && g.tracks == 80);
    CHECK(disk_geom_size(368640, &g) == 0 && g.spt == 9 && g.sides == 1 && g.tracks == 80);
    CHECK(disk_geom_size(819200, &g) == 0 && g.spt == 10 && g.sides == 2);
    CHECK(disk_geom_size(901120, &g) == 0 && g.spt == 11 && g.sides == 2);
    CHECK(disk_geom_size(839680, &g) == 0 && g.spt == 10 && g.tracks == 82);
    CHECK(disk_geom_size(12345, &g) == TE_NOFLOPPY && disk_geom_size(512 * 7, &g) == TE_NOFLOPPY);
}

/* ---- disquette -> image, par la copie ordinaire ---- */

static long read_to_image(DISK *d, int dev, const char *name, OPS *o)
{
    static FLOPSRC f;
    FINFO it;
    long r = disk_source(&f, d, dev, name);
    if (r) return r;
    memset(o, 0, sizeof *o);
    o->src = &f.src;
    o->verify = 1;
    r = ops_begin(o);
    if (!r) r = f.src.first(f.src.ctx, "A:\\", &it);
    if (!r) r = ops_scan(o, "A:\\", &it, 1);
    if (!r) r = ops_copy(o, "A:\\", &it, 1, "C:\\IMAGES\\", 0);
    ops_end(o);
    disk_source_end(&f);
    return r ? r : o->errors ? o->last_err : 0;
}

static void t_read(void)
{
    DISK d;
    static OPS o;
    const unsigned char *got;
    long n;

    current = "read floppy to image";
    setup(&d);
    fd_mkdir("C:\\IMAGES");
    fd_disk_load(0, img_st, img_st_n, 9, 2);
    fd_drive[0] = 0;
    CHECK(read_to_image(&d, 0, "DISK.ST", &o) == 0);
    got = fd_get("C:\\IMAGES\\DISK.ST", &n);
    CHECK(got && n == img_st_n && !memcmp(got, img_st, n));
    CHECK(o.files_done == 1);

    current = "read: bad sector";
    fd_bad_track = 40 * 2 + 1;
    fd_bad_reads = -1;
    CHECK(read_to_image(&d, 0, "BAD.ST", &o) == EREADF);
    CHECK(!fd_exists("C:\\IMAGES\\BAD.ST"));                  /* pas d'image partielle */
    CHECK(fd_open_handles() == 0);

    current = "read: sector read on the third try";
    fd_bad_reads = 2;
    CHECK(read_to_image(&d, 0, "RETRY.ST", &o) == 0);
    got = fd_get("C:\\IMAGES\\RETRY.ST", &n);
    CHECK(got && n == img_st_n && !memcmp(got, img_st, n));
    fd_bad_track = -1;

    current = "read: floppy changes between copy and verify";
    fd_flaky_read = 200;                                      /* pendant la relecture */
    CHECK(read_to_image(&d, 0, "FLAKY.ST", &o) == TE_VERIFY);
    CHECK(!fd_exists("C:\\IMAGES\\FLAKY.ST"));

    current = "read: existing image is not replaced without asking";
    CHECK(read_to_image(&d, 0, "DISK.ST", &o) != 0 || o.skipped == 1);
    got = fd_get("C:\\IMAGES\\DISK.ST", &n);
    CHECK(got && n == img_st_n && !memcmp(got, img_st, n));

    current = "read: unknown format and empty drive";
    fd_disk_blank(1);
    fd_drive[0] = 1;
    CHECK(read_to_image(&d, 0, "X.ST", &o) == ESECNF);
    fd_drive[0] = -1;
    CHECK(read_to_image(&d, 0, "X.ST", &o) == EDRVNR);
    CHECK(!fd_exists("C:\\IMAGES\\X.ST"));
}

/* ---- image -> disquette ---- */

static void t_write(void)
{
    DISK d;
    static unsigned char before[FD_CYLS * 2 * FD_SPT * 512];
    int i, zero_last;

    current = "write .ST image";
    setup(&d);
    fd_put("C:\\IMG.ST", img_st, img_st_n, 0);
    fd_put("C:\\IMG.MSA", img_msa, img_msa_n, 0);
    fd_disk_blank(0);
    fd_drive[0] = 0;
    CHECK(disk_write_image(&d, "C:\\IMG.ST", 0) == 0);
    CHECK(fd_disk_dump(0, dump, 9, 2, 80) && !memcmp(dump, img_st, img_st_n));
    CHECK(progress_calls == 80);
    for (i = 0, zero_last = 1; i < fd_write_log_n - 2; i++) if (fd_write_log[i] == 0) zero_last = 0;
    CHECK(zero_last && fd_write_log[fd_write_log_n - 1] == 0);  /* piste 0 en dernier */
    CHECK(fd_open_handles() == 0);

    current = "write .MSA image over another floppy";
    setup(&d);
    fd_put("C:\\IMG.MSA", img_msa, img_msa_n, 0);
    make_disk(0, 7, 9, 2);
    fd_drive[0] = 0;
    CHECK(disk_write_image(&d, "C:\\IMG.MSA", 0) == 0);
    CHECK(fd_disk_dump(0, dump, 9, 2, 80) && !memcmp(dump, img_st, img_st_n));

    current = "write: 10-sector floppy reformatted to 9";
    setup(&d);
    fd_put("C:\\IMG.ST", img_st, img_st_n, 0);
    make_disk(0, 8, 10, 2);
    fd_drive[0] = 0;
    CHECK(disk_write_image(&d, "C:\\IMG.ST", 0) == 0);
    CHECK(fd_disk_dump(0, dump, 9, 2, 80) && !memcmp(dump, img_st, img_st_n));

    current = "write: program disk is refused, in either drive";
    setup(&d);
    fd_put("C:\\IMG.ST", img_st, img_st_n, 0);
    make_disk(2, 99, 9, 2);
    fd_drive[0] = 2;
    disk_note_program(&d, 0);
    CHECK(d.prog_dev == 0);
    snapshot(2, before);
    CHECK(disk_write_image(&d, "C:\\IMG.ST", 0) == TE_PROGDISK);
    fd_drive[0] = -1;
    fd_drive[1] = 2;
    CHECK(disk_write_image(&d, "C:\\IMG.ST", 1) == TE_PROGDISK);
    CHECK(fd_disk_dump(2, dump, 9, 2, 80) && !memcmp(dump, before, img_st_n));
    CHECK(fd_flop_writes == 0 && fd_flop_formats == 0);

    current = "write: image on the target drive";
    setup(&d);
    fd_put("A:\\IMG.ST", img_st, img_st_n, 0);
    fd_put("B:\\IMG.ST", img_st, img_st_n, 0);
    make_disk(0, 3, 9, 2);
    fd_drive[0] = 0;
    CHECK(disk_write_image(&d, "A:\\IMG.ST", 0) == TE_SELF);
    fd_nflops = 1;                                            /* B: est A: */
    CHECK(disk_write_image(&d, "B:\\IMG.ST", 0) == TE_SELF);
    CHECK(fd_flop_writes == 0);

    current = "write: write-protected";
    setup(&d);
    fd_put("C:\\IMG.ST", img_st, img_st_n, 0);
    make_disk(0, 4, 9, 2);
    fd_disk_wprot(0, 1);
    fd_drive[0] = 0;
    snapshot(0, before);
    CHECK(disk_write_image(&d, "C:\\IMG.ST", 0) == EWRPRO);
    CHECK(fd_disk_dump(0, dump, 9, 2, 80) && !memcmp(dump, before, img_st_n));

    current = "write: bad copy on cylinder 5 is caught";
    setup(&d);
    fd_put("C:\\IMG.ST", img_st, img_st_n, 0);
    fd_disk_blank(0);
    fd_drive[0] = 0;
    fd_weak_cyl = 5;
    CHECK(disk_write_image(&d, "C:\\IMG.ST", 0) == TE_VERIFY && d.track_err == 5);

    current = "write: cancelled, track 0 untouched";
    setup(&d);
    fd_put("C:\\IMG.ST", img_st, img_st_n, 0);
    make_disk(0, 5, 9, 2);
    fd_drive[0] = 0;
    snapshot(0, before);
    cancel_at = 10;
    CHECK(disk_write_image(&d, "C:\\IMG.ST", 0) == TE_CANCEL);
    CHECK(fd_disk_dump(0, dump, 9, 2, 80) && !memcmp(dump, before, 9 * 2 * 512));
    CHECK(fd_open_handles() == 0);

    current = "write: damaged images touch nothing";
    setup(&d);
    fd_put("C:\\T.MSA", img_msa, img_msa_n / 2, 0);
    fd_put("C:\\ODD.ST", img_st, 5000, 0);
    fd_put("C:\\PART.MSA", img_msa, img_msa_n, 0);
    {
        long n;
        unsigned char *p = (unsigned char *)fd_get("C:\\PART.MSA", &n);
        p[7] = 3;                                             /* commence a la piste 3 */
    }
    make_disk(0, 6, 9, 2);
    fd_drive[0] = 0;
    CHECK(disk_write_image(&d, "C:\\T.MSA", 0) == TE_NOFLOPPY);
    CHECK(disk_write_image(&d, "C:\\ODD.ST", 0) == TE_NOFLOPPY);
    CHECK(disk_write_image(&d, "C:\\PART.MSA", 0) == TE_NOFLOPPY);
    CHECK(fd_flop_writes == 0 && fd_flop_formats == 0);
    CHECK(fd_open_handles() == 0);
}

/* ---- copie ---- */

static int ins_calls, ins_wrong, ins_cancel_at;
static int src_k, dst_k;
static int cb_insert(DISK *d, int which, int dev, int again)
{
    ins_calls++;
    if (ins_cancel_at && ins_calls >= ins_cancel_at) return 0;
    if (which == 1 && ins_wrong > 0) {                        /* l'utilisateur n'a pas change */
        ins_wrong--;
        fd_drive[dev] = src_k;
        return 1;
    }
    fd_drive[dev] = which ? dst_k : src_k;
    return 1;
}

static void t_copy(void)
{
    DISK d;
    static unsigned char before[FD_CYLS * 2 * FD_SPT * 512], sbefore[FD_CYLS * 2 * FD_SPT * 512];

    current = "copy with two drives";
    setup(&d);
    make_disk(0, 11, 9, 2);
    fd_disk_blank(1);
    fd_drive[0] = 0;
    fd_drive[1] = 1;
    CHECK(disk_copy(&d, 0, 1) == 0);
    CHECK(same_disks(0, 1, 9, 2));

    current = "copy 10 sectors, 1 side";
    setup(&d);
    make_disk(0, 12, 10, 1);
    make_disk(1, 13, 9, 2);
    fd_drive[0] = 0;
    fd_drive[1] = 1;
    CHECK(disk_copy(&d, 0, 1) == 0);
    CHECK(same_disks(0, 1, 10, 1));

    current = "copy: two drives, target is an earlier copy: overwritten";
    CHECK(disk_copy(&d, 0, 1) == 0 && same_disks(0, 1, 10, 1));

    current = "copy: target is the program disk";
    setup(&d);
    make_disk(0, 14, 9, 2);
    make_disk(1, 15, 9, 2);
    fd_drive[0] = 0;
    fd_drive[1] = 1;
    disk_note_program(&d, 1);
    snapshot(1, before);
    CHECK(disk_copy(&d, 0, 1) == TE_PROGDISK);
    CHECK(fd_disk_dump(1, dump, 9, 2, 80) && !memcmp(dump, before, disk_len(9, 2)));

    current = "copy with one drive, three passes";
    setup(&d);
    d.insert = cb_insert;
    d.bufsize = DISK_WORK + 9 * 512 + 30L * 9 * 512 * 2;     /* 30 cylindres par passe */
    ins_calls = ins_wrong = ins_cancel_at = 0;
    src_k = 2;
    dst_k = 3;
    make_disk(2, 21, 9, 2);
    make_disk(3, 22, 9, 2);
    fd_nflops = 1;
    fd_drive[0] = -1;
    snapshot(2, sbefore);
    CHECK(disk_copy(&d, 0, 0) == 0);
    CHECK(same_disks(2, 3, 9, 2));
    CHECK(ins_calls == 6);                                    /* source, puis cible x 3 passes */
    CHECK(fd_disk_dump(2, dump, 9, 2, 80) && !memcmp(dump, sbefore, disk_len(9, 2)));

    current = "copy with one drive: source left in, asked again";
    setup(&d);
    d.insert = cb_insert;
    d.bufsize = DISK_WORK + 9 * 512 + 30L * 9 * 512 * 2;
    ins_calls = ins_cancel_at = 0;
    ins_wrong = 2;
    make_disk(2, 23, 9, 2);
    make_disk(3, 24, 9, 2);
    fd_nflops = 1;
    snapshot(2, sbefore);
    CHECK(disk_copy(&d, 0, 0) == 0);
    CHECK(same_disks(2, 3, 9, 2));
    CHECK(ins_calls == 8);
    CHECK(fd_disk_dump(2, dump, 9, 2, 80) && !memcmp(dump, sbefore, disk_len(9, 2)));

    current = "copy with one drive: cancelled on the second swap";
    setup(&d);
    d.insert = cb_insert;
    d.bufsize = DISK_WORK + 9 * 512 + 30L * 9 * 512 * 2;
    ins_calls = ins_wrong = 0;
    ins_cancel_at = 4;
    make_disk(2, 25, 9, 2);
    make_disk(3, 26, 9, 2);
    fd_nflops = 1;
    snapshot(2, sbefore);
    snapshot(3, before);
    CHECK(disk_copy(&d, 0, 0) == TE_CANCEL);
    CHECK(fd_disk_dump(2, dump, 9, 2, 80) && !memcmp(dump, sbefore, disk_len(9, 2)));
    CHECK(fd_disk_dump(3, dump, 9, 2, 80) && !memcmp(dump, before, 9 * 2 * 512));   /* piste 0 intacte */

    current = "copy: unreadable source sector";
    setup(&d);
    make_disk(0, 27, 9, 2);
    fd_disk_blank(1);
    fd_drive[0] = 0;
    fd_drive[1] = 1;
    fd_bad_track = 33 * 2;
    fd_bad_reads = -1;
    CHECK(disk_copy(&d, 0, 1) == EREADF && d.track_err == 33);

    current = "copy: memory too small";
    setup(&d);
    d.bufsize = DISK_WORK + 9 * 512 + 100;
    make_disk(0, 28, 9, 2);
    fd_disk_blank(1);
    fd_drive[0] = 0;
    fd_drive[1] = 1;
    CHECK(disk_copy(&d, 0, 1) == ENSMEM);
    CHECK(fd_flop_writes == 0);
}

/* ---- formatage ---- */

static void t_format(void)
{
    DISK d;
    GEOM g;
    VFS *v;
    FINFO f;
    static unsigned char before[FD_CYLS * 2 * FD_SPT * 512];
    static const int cases[][2] = { { 9, 2 }, { 9, 1 }, { 10, 2 }, { 11, 2 } };
    int c;

    for (c = 0; c < 4; c++) {
        int spt = cases[c][0], sides = cases[c][1];
        long n = disk_len(spt, sides);
        current = "format";
        setup(&d);
        fd_disk_blank(0);
        fd_drive[0] = 0;
        CHECK(disk_format(&d, 0, spt, sides) == 0);
        CHECK(fd_disk_dump(0, dump, spt, sides, 80));
        CHECK(disk_geom_boot(dump, &g) == 0 && g.spt == spt && g.sides == sides && g.tracks == 80);
        CHECK(dump[512] == (sides == 2 ? 0xf9 : 0xf8) && dump[513] == 0xff && dump[514] == 0xff);
        /* Le lecteur d'images de TOSFC l'accepte : une disquette vide. */
        fd_put("C:\\F.ST", dump, n, 0);
        CHECK(vfs_open(&v, "C:\\F.ST", "C:\\F.ST\\") == 0);
        if (v) {
            const SOURCE *s = vfs_source(v);
            CHECK(s->first(s->ctx, "C:\\F.ST\\", &f) != 0);
            vfs_close(v);
        }
    }

    current = "format: two floppies get different serial numbers";
    setup(&d);
    fd_disk_blank(0);
    fd_disk_blank(1);
    fd_drive[0] = 0;
    fd_drive[1] = 1;
    CHECK(disk_format(&d, 0, 9, 2) == 0 && disk_format(&d, 1, 9, 2) == 0);
    {
        static unsigned char a[512], b[512];
        sys_floprd(a, 0, 1, 0, 0, 1);
        sys_floprd(b, 1, 1, 0, 0, 1);
        CHECK(memcmp(a + 8, b + 8, 3) != 0);
    }

    current = "format: program disk refused";
    setup(&d);
    make_disk(0, 31, 9, 2);
    fd_drive[0] = 0;
    disk_note_program(&d, 0);
    snapshot(0, before);
    CHECK(disk_format(&d, 0, 9, 2) == TE_PROGDISK && fd_flop_formats == 0);
    CHECK(fd_disk_dump(0, dump, 9, 2, 80) && !memcmp(dump, before, disk_len(9, 2)));

    current = "format: drive refuses 10 sectors";
    setup(&d);
    fd_disk_blank(0);
    fd_drive[0] = 0;
    fd_fmt_only_spt = 9;
    CHECK(disk_format(&d, 0, 10, 2) == EWRITF && d.track_err == 1);

    current = "format: write-protected, bad values";
    setup(&d);
    make_disk(0, 32, 9, 2);
    fd_disk_wprot(0, 1);
    fd_drive[0] = 0;
    CHECK(disk_format(&d, 0, 9, 2) == EWRPRO);
    CHECK(disk_format(&d, 0, 8, 2) == TE_NOFLOPPY && disk_format(&d, 0, 9, 3) == TE_NOFLOPPY);

    current = "format: cancelled";
    setup(&d);
    make_disk(0, 33, 9, 2);
    fd_drive[0] = 0;
    snapshot(0, before);
    cancel_at = 5;
    CHECK(disk_format(&d, 0, 9, 2) == TE_CANCEL);
    CHECK(fd_disk_dump(0, dump, 9, 2, 80) && !memcmp(dump, before, 9 * 2 * 512));
}

int main(int argc, char **argv)
{
    dir = argc > 1 ? argv[1] : "build/host/data";
    img_st = slurp("IMG.ST", &img_st_n);
    img_msa = slurp("IMG.MSA", &img_msa_n);
    t_geometry();
    t_read();
    t_write();
    t_copy();
    t_format();
    printf("test_disk: %d checks, %d failed\n", tests, failures);
    return failures ? 1 : 0;
}
