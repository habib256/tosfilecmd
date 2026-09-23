/*
 * test_vfs.c -- images et archives comme dossiers, sur le faux GEMDOS.
 *
 *   test_vfs DIR      (DIR : tests/gen_vfs.py)
 *
 * Pour chaque conteneur : l'arborescence et les tailles attendues, chaque
 * fichier relu octet pour octet, puis une extraction complete par
 * ops_copy (le meme code que la copie : creation exclusive, relecture).
 * Conteneurs abimes et aleatoires : une erreur, jamais un debordement
 * (AddressSanitizer) ni un fichier partiel a l'arrivee.
 */
#include "fakedos.h"
#include "../src/vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests, failures;
static const char *current;
#define CHECK(c) do { tests++; if (!(c)) { failures++; \
    printf("FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, current, #c); } } while (0)

static const char *dir;

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
    d = malloc(*n + 1);
    if (fread(d, 1, *n, f) != (size_t)*n) exit(2);
    fclose(f);
    return d;
}

typedef struct { char path[128]; long size; char file[64]; int isdir; int seen; } EXP;
static EXP expv[64];
static int nexp;

static void load_expect(const char *container)
{
    char name[128], line[512];
    FILE *f;
    snprintf(name, sizeof name, "%s/%s.expect", dir, container);
    f = fopen(name, "r");
    if (!f) { printf("missing %s\n", name); exit(2); }
    nexp = 0;
    while (fgets(line, sizeof line, f)) {
        char *t1 = strchr(line, '\t'), *t2;
        if (!t1) continue;
        *t1 = 0;
        t2 = strchr(t1 + 1, '\t');
        *t2 = 0;
        t2[strcspn(t2 + 1, "\n") + 1] = 0;
        strcpy(expv[nexp].path, line);
        expv[nexp].isdir = t1[1] == '-';
        expv[nexp].size = expv[nexp].isdir ? 0 : atol(t1 + 1);
        strcpy(expv[nexp].file, t2 + 1);
        expv[nexp].seen = 0;
        nexp++;
    }
    fclose(f);
}

static EXP *find_exp(const char *rel, int isdir)
{
    int i;
    for (i = 0; i < nexp; i++) {
        char want[130];
        strcpy(want, rel);
        if (isdir) strcat(want, "\\");
        if (!strcmp(expv[i].path, want)) return &expv[i];
    }
    return NULL;
}

/* Parcours recursif par la source, comparaison avec la liste attendue. */
static int walk(const SOURCE *s, const char *root, const char *rel, int depth)
{
    FINFO list[64];
    int n = 0, i, bad = 0;
    char path[256];
    long r;
    snprintf(path, sizeof path, "%s%s", root, rel);
    r = s->first(s->ctx, path, &list[0]);
    while (r == 0 && n < 63) { n++; r = s->next(s->ctx, &list[n]); }
    for (i = 0; i < n; i++) {
        char sub[200];
        EXP *e;
        int isdir = (list[i].attr & FA_DIR) != 0;
        snprintf(sub, sizeof sub, "%s%s", rel, list[i].name);
        e = find_exp(sub, isdir);
        if (!e) { printf("  unexpected %s\n", sub); bad++; continue; }
        e->seen = 1;
        if (!isdir && (long)list[i].size != e->size) { printf("  size %s\n", sub); bad++; }
        if (isdir && depth < 8) {
            strcat(sub, "\\");
            bad += walk(s, root, sub, depth + 1);
        }
    }
    return bad;
}

static void t_container(const char *container)
{
    char root[64], cpath[64];
    long n, r;
    unsigned char *data = slurp(container, &n);
    VFS *v = 0;
    const SOURCE *s;
    int i, bad;
    static OPS ops;

    current = container;
    fd_reset();
    snprintf(cpath, sizeof cpath, "C:\\%s", container);
    snprintf(root, sizeof root, "C:\\%s\\", container);
    fd_put(cpath, data, n, 0);
    load_expect(container);
    r = vfs_open(&v, cpath, root);
    CHECK(r == 0 && v);
    if (r) printf("  vfs_open %s: %ld\n", container, r);
    if (!v) { free(data); return; }
    s = vfs_source(v);
    CHECK(fd_open_handles() == 0);                  /* conteneur referme */

    bad = walk(s, root, "", 0);
    CHECK(bad == 0);
    for (i = 0; i < nexp; i++) CHECK(expv[i].seen);

    /* Lecture directe, par morceaux de 1000 octets. */
    for (i = 0; i < nexp; i++) {
        char p[200];
        long en, got = 0, h;
        unsigned char *want, *buf;
        if (expv[i].isdir) continue;
        want = slurp(expv[i].file, &en);
        buf = malloc(en + 1000);
        snprintf(p, sizeof p, "%s%s", root, expv[i].path);
        h = s->open(s->ctx, p);
        CHECK(h >= 0);
        if (h >= 0) {
            for (;;) {
                long k = s->read(s->ctx, h, 1000, buf + got);
                if (k <= 0) { CHECK(k == 0); break; }
                got += k;
            }
            s->close(s->ctx, h);
        }
        CHECK(got == en && !memcmp(buf, want, en));
        free(buf);
        free(want);
    }
    CHECK(fd_open_handles() == 0);

    /* Extraction complete par ops_copy, depuis la racine virtuelle. */
    {
        FINFO items[64];
        int ni = 0;
        memset(&ops, 0, sizeof ops);
        ops.verify = 1;
        ops.src = s;
        CHECK(ops_begin(&ops) == 0);
        ops.bufsize = 2048;
        r = s->first(s->ctx, root, &items[0]);
        while (r == 0 && ni < 63) { ni++; r = s->next(s->ctx, &items[ni]); }
        fd_mkdir("D:\\OUT");
        CHECK(ops_scan(&ops, root, items, ni) == 0);
        CHECK(ops_copy(&ops, root, items, ni, "D:\\OUT\\", 0) == 0);
        CHECK(ops.errors == 0);
        ops_end(&ops);
        for (i = 0; i < nexp; i++) {
            char p[200];
            long en, gn;
            const unsigned char *got;
            unsigned char *want;
            snprintf(p, sizeof p, "D:\\OUT\\%s", expv[i].path);
            if (expv[i].isdir) {
                p[strlen(p) - 1] = 0;
                CHECK(fd_isdir(p));
                continue;
            }
            want = slurp(expv[i].file, &en);
            got = fd_get(p, &gn);
            CHECK(got && gn == en && !memcmp(got, want, en));
            free(want);
        }
    }
    CHECK(fd_open_handles() == 0);
    vfs_close(v);
    free(data);
}

/* Un octet change au milieu des donnees : l'extraction echoue, rien ne
 * reste a l'arrivee. */
static void t_corrupt(const char *container, long at)
{
    char root[64], cpath[64];
    long n;
    unsigned char *data = slurp(container, &n);
    VFS *v = 0;
    static OPS ops;
    FINFO items[64];
    int ni = 0;
    long r;
    current = "corrupted data";
    fd_reset();
    data[at] ^= 0x5a;
    snprintf(cpath, sizeof cpath, "C:\\%s", container);
    snprintf(root, sizeof root, "C:\\%s\\", container);
    fd_put(cpath, data, n, 0);
    r = vfs_open(&v, cpath, root);
    if (r == 0) {
        const SOURCE *s = vfs_source(v);
        memset(&ops, 0, sizeof ops);
        ops.src = s;
        ops.verify = 1;
        ops_begin(&ops);
        r = s->first(s->ctx, root, &items[0]);
        while (r == 0 && ni < 63) { ni++; r = s->next(s->ctx, &items[ni]); }
        fd_mkdir("D:\\OUT");
        ops_copy(&ops, root, items, ni, "D:\\OUT\\", 0);
        CHECK(ops.errors >= 1);
        ops_end(&ops);
        vfs_close(v);
    }
    /* Aucun fichier incomplet : seuls les fichiers justes existent. */
    CHECK(fd_open_handles() == 0);
    free(data);
}

static void t_bad_images(void)
{
    long n, r;
    unsigned char *img = slurp("IMG.ST", &n), *msa;
    VFS *v;
    current = "bad images";
    fd_reset();
    /* BPB faux */
    img[12] = 3;                                     /* 768 octets par secteur */
    fd_put("C:\\B1.ST", img, n, 0);
    CHECK(vfs_open(&v, "C:\\B1.ST", "C:\\B1.ST\\") == TE_BADARC);
    img[11] = 0;
    img[12] = 2;
    /* FAT : le cluster 2 pointe sur lui-meme (le 1er fichier boucle). */
    img[512 + 3] = 2;
    img[512 + 4] = (img[512 + 4] & 0xf0);
    fd_put("C:\\B2.ST", img, n, 0);
    r = vfs_open(&v, "C:\\B2.ST", "C:\\B2.ST\\");
    if (r == 0) {
        const SOURCE *s = vfs_source(v);
        unsigned char buf[4096];
        long h = s->open(s->ctx, "C:\\B2.ST\\README.TXT"), k = 0, got;
        if (h >= 0) {
            while ((got = s->read(s->ctx, h, sizeof buf, buf)) > 0 && k < 100) k++;
            s->close(s->ctx, h);
        }
        CHECK(k < 100);                              /* la boucle est detectee */
        vfs_close(v);
    }
    free(img);
    msa = slurp("IMG.MSA", &n);
    fd_put("C:\\T.MSA", msa, n / 2, 0);              /* tronque */
    CHECK(vfs_open(&v, "C:\\T.MSA", "C:\\T.MSA\\") != 0);
    msa[0] = 0;
    fd_put("C:\\M.MSA", msa, n, 0);
    CHECK(vfs_open(&v, "C:\\M.MSA", "C:\\M.MSA\\") == TE_BADARC);
    free(msa);
    CHECK(fd_open_handles() == 0);
}

static int go_on(OPS *o, const char *path, long err, int more) { return 1; }

/* Methodes inconnues et fichier chiffre : listes, refuses a la lecture,
 * rien d'ecrit par la copie. */
static void t_methods(void)
{
    long n;
    unsigned char *d = slurp("M.ZIP", &n);
    VFS *v;
    const SOURCE *s;
    static OPS ops;
    FINFO items[8];
    int ni = 0;
    long r;
    const unsigned char *got;
    current = "unsupported methods";
    fd_reset();
    fd_put("C:\\M.ZIP", d, n, 0);
    CHECK(vfs_open(&v, "C:\\M.ZIP", "C:\\M.ZIP\\") == 0);
    s = vfs_source(v);
    CHECK(s->open(s->ctx, "C:\\M.ZIP\\BZ.TXT") == TE_METHOD);
    CHECK(s->open(s->ctx, "C:\\M.ZIP\\ENC.TXT") == TE_METHOD);
    CHECK(s->open(s->ctx, "C:\\M.ZIP\\OK.TXT") >= 0);
    s->close(s->ctx, 0);
    memset(&ops, 0, sizeof ops);
    ops.src = s;
    ops.verify = 1;
    ops.error = go_on;
    ops_begin(&ops);
    r = s->first(s->ctx, "C:\\M.ZIP\\", &items[0]);
    while (r == 0 && ni < 7) { ni++; r = s->next(s->ctx, &items[ni]); }
    CHECK(ni == 3);
    fd_mkdir("D:\\OUT");
    ops_copy(&ops, "C:\\M.ZIP\\", items, ni, "D:\\OUT\\", 0);
    CHECK(ops.errors == 2 && ops.files_done == 1);
    ops_end(&ops);
    got = fd_get("D:\\OUT\\OK.TXT", &n);
    CHECK(got && n == 10 && !memcmp(got, "readable\r\n", 10));
    CHECK(!fd_get("D:\\OUT\\BZ.TXT", &n) && !fd_get("D:\\OUT\\ENC.TXT", &n));
    CHECK(fd_open_handles() == 0);
    vfs_close(v);
    free(d);
}

/* Le conteneur change apres l'ouverture : plus aucune lecture. */
static void t_changed(void)
{
    long n;
    unsigned char *d = slurp("T.ZIP", &n);
    VFS *v;
    const SOURCE *s;
    current = "container changed";
    fd_reset();
    fd_put("C:\\T.ZIP", d, n, 0);
    CHECK(vfs_open(&v, "C:\\T.ZIP", "C:\\T.ZIP\\") == 0);
    s = vfs_source(v);
    CHECK(s->open(s->ctx, "C:\\T.ZIP\\RANDOM.BIN") >= 0);
    s->close(s->ctx, 0);
    fd_put("C:\\T.ZIP", d, n - 1, 0);
    CHECK(s->open(s->ctx, "C:\\T.ZIP\\RANDOM.BIN") == TE_CHANGED);
    CHECK(fd_open_handles() == 0);
    vfs_close(v);
    free(d);
}

static void t_fuzz(void)
{
    static const char *const names[] = { "F.ST", "F.MSA", "F.LZH", "F.ZIP", "F.ARC" };
    unsigned x = 4242;
    int it;
    current = "random containers";
    for (it = 0; it < 2500; it++) {
        long n = 30 + (x % 60000), i;
        unsigned char *d = malloc(n);
        char path[32], root[32];
        VFS *v;
        for (i = 0; i < n; i++) { x = x * 1103515245u + 12345u; d[i] = (unsigned char)(x >> 16); }
        if (it % 5 == 0 && n > 512) { d[11] = 0; d[12] = 2; d[13] = 2; d[14] = 1; d[16] = 2; d[17] = 112; d[18] = 0; }
        if (it % 5 == 1) { d[0] = 0x0e; d[1] = 0x0f; d[2] = 0; d[3] = 9; d[4] = 0; d[5] = 1; d[6] = 0; d[7] = 0; d[8] = 0; d[9] = 3; }
        if (it % 5 == 4) { d[0] = 0x1a; d[1] = (unsigned char)(2 + it % 8); }
        fd_reset();
        snprintf(path, sizeof path, "C:\\%s", names[it % 5]);
        snprintf(root, sizeof root, "C:\\%s\\", names[it % 5]);
        fd_put(path, d, n, 0);
        if (vfs_open(&v, path, root) == 0) {
            const SOURCE *s = vfs_source(v);
            FINFO f;
            long r = s->first(s->ctx, root, &f);
            int k = 0;
            while (r == 0 && k < 20) {
                if (!(f.attr & FA_DIR)) {
                    char p[64];
                    unsigned char buf[2048];
                    long h;
                    snprintf(p, sizeof p, "%s%s", root, f.name);
                    h = s->open(s->ctx, p);
                    if (h >= 0) {
                        int g = 0;
                        while (s->read(s->ctx, h, sizeof buf, buf) > 0 && g < 200) g++;
                        s->close(s->ctx, h);
                    }
                    s->first(s->ctx, root, &f);          /* la lecture a pu bouger l'iterateur */
                    { int j; for (j = 0; j <= k; j++) r = s->next(s->ctx, &f); }
                } else {
                    r = s->next(s->ctx, &f);
                }
                k++;
            }
            vfs_close(v);
        }
        free(d);
    }
    CHECK(fd_open_handles() == 0);
}

static void t_names(void)
{
    char out[13];
    current = "8.3 names";
    vfs_name83("A long name.text", out, 0, 0);
    CHECK(!strcmp(out, "ALONGNAM.TEX"));
    vfs_name83("dir/sub/hello world.c", out, 0, 0);
    CHECK(!strcmp(out, "HELLOWOR.C"));
    vfs_name83(".profile", out, 0, 0);
    CHECK(!strcmp(out, "PROFILE"));
    vfs_name83("weird*name?.x+y", out, 0, 0);
    CHECK(!strcmp(out, "WEIRD_NA.X_Y"));
    vfs_name83("", out, 0, 0);
    CHECK(!strcmp(out, "_"));
    vfs_name83("tar.gz.bak", out, 0, 0);
    CHECK(!strcmp(out, "TAR_GZ.BAK") || !strcmp(out, "TARGZ.BAK") || !strcmp(out, "TARGZ.BA"));
    CHECK(vfs_kind_of_name("A.ST") == VK_IMAGE && vfs_kind_of_name("a.msa") == VK_IMAGE);
    CHECK(vfs_kind_of_name("X.LZH") == VK_LZH && vfs_kind_of_name("X.LHA") == VK_LZH);
    CHECK(vfs_kind_of_name("X.ZIP") == VK_ZIP && vfs_kind_of_name("X.ARC") == VK_ARC);
    CHECK(vfs_kind_of_name("X.TXT") == VK_NONE && vfs_kind_of_name("ST") == VK_NONE);
}

int main(int argc, char **argv)
{
    long n;
    unsigned char *z;
    dir = argc > 1 ? argv[1] : "build/host/data";
    t_names();
    t_container("IMG.ST");
    t_container("IMG.MSA");
    t_container("T.LZH");
    t_container("T.ZIP");
    t_container("T.ARC");
    z = slurp("T.ZIP", &n);
    free(z);
    t_corrupt("T.ZIP", n / 2);
    z = slurp("T.LZH", &n);
    free(z);
    t_corrupt("T.LZH", n / 2);
    z = slurp("T.ARC", &n);
    free(z);
    t_corrupt("T.ARC", n / 2);
    t_bad_images();
    t_changed();
    t_methods();
    t_fuzz();
    printf("test_vfs: %d checks, %d failed\n", tests, failures);
    return failures ? 1 : 0;
}
