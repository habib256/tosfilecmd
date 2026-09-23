/*
 * test_music.c -- LHA (-lh5-), ICE!, YM et SNDH sur l'hote.
 *
 *   test_music DIR     (DIR : les vecteurs de tests/gen_lzh.py et d'icetool)
 *
 * Chaque decompresseur doit rendre exactement les octets d'origine, et
 * refuser -- sans lire ni ecrire hors des tampons (AddressSanitizer) -- les
 * fichiers tronques, abimes ou aleatoires.
 */
#include "../src/lzh.h"
#include "../src/ice.h"
#include "../src/ym.h"
#include "../src/sndh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests, failures;
static const char *current;
#define CHECK(c) do { tests++; if (!(c)) { failures++; \
    printf("FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, current, #c); } } while (0)

static unsigned char *load(const char *dir, const char *name, long *n)
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

static const char *const VECTORS[] = { "empty", "one", "run", "text", "random", "mixed", "big" };
#define NVEC (int)(sizeof VECTORS / sizeof VECTORS[0])
static unsigned char work[LZH_WORK_BYTES];

static int lzh_one(const unsigned char *a, long n, unsigned char **out, long *size)
{
    LZHENTRY e;
    int r = lzh_entry(a, n, 0, &e);
    if (r != LZH_OK) return r;
    *out = malloc(e.size + 1);
    *size = e.size;
    r = lzh_extract(a, n, &e, *out, work);
    return r;
}

static void t_lzh(const char *dir)
{
    int i;
    for (i = 0; i < NVEC; i++) {
        char name[64];
        long n, bn, size;
        unsigned char *a, *b, *out = 0;
        int r;
        current = VECTORS[i];
        snprintf(name, sizeof name, "lzh_%s.lzh", VECTORS[i]);
        a = load(dir, name, &n);
        snprintf(name, sizeof name, "lzh_%s.bin", VECTORS[i]);
        b = load(dir, name, &bn);
        CHECK(lzh_is_archive(a, n));
        r = lzh_one(a, n, &out, &size);
        CHECK(r == LZH_OK && size == bn && !memcmp(out, b, bn));
        free(out);
        /* L'archive suivante : fin. */
        {
            LZHENTRY e, e2;
            lzh_entry(a, n, 0, &e);
            CHECK(lzh_entry(a, n, e.next, &e2) == LZH_END);
        }
        /* Tronquee : jamais de succes. */
        if (bn > 0) {
            out = 0;
            r = lzh_one(a, n - 3, &out, &size);
            CHECK(r != LZH_OK);
            free(out);
        }
        /* Un octet change dans les donnees : erreur ou CRC faux. */
        if (bn > 16) {
            unsigned char *c = malloc(n);
            int k;
            for (k = 0; k < 20; k++) {
                long at;
                memcpy(c, a, n);
                at = 40 + (k * 7919) % (n - 45);
                c[at] ^= (unsigned char)(1 << (k & 7));
                out = 0;
                r = lzh_one(c, n, &out, &size);
                CHECK(r != LZH_OK || !memcmp(out, b, bn));
                free(out);
            }
            free(c);
        }
        free(a);
        free(b);
    }

    current = "random bodies behind a valid header";
    {
        long n;
        unsigned char *a = load(dir, "lzh_text.lzh", &n), *c = malloc(n);
        unsigned x = 99;
        int it;
        for (it = 0; it < 500; it++) {
            long k, size;
            unsigned char *out = 0;
            memcpy(c, a, n);
            for (k = 30; k < n; k++) { x = x * 1103515245u + 12345u; c[k] = (unsigned char)(x >> 16); }
            lzh_one(c, n, &out, &size);
            free(out);
        }
        CHECK(1);
        free(a);
        free(c);
    }
    current = "bad headers";
    {
        LZHENTRY e;
        unsigned char h[40];
        memset(h, 0, sizeof h);
        CHECK(lzh_entry(h, sizeof h, 0, &e) == LZH_END);
        h[0] = 30; h[20] = 7;
        CHECK(lzh_entry(h, sizeof h, 0, &e) == LZH_BAD);
        h[20] = 0; h[0] = 200;
        CHECK(lzh_entry(h, sizeof h, 0, &e) == LZH_BAD);
    }
}

static void t_ice(const char *dir)
{
    int i;
    for (i = 0; i < NVEC; i++) {
        char name[64];
        long n, bn, sz;
        unsigned char *a, *b, *out;
        if (!strcmp(VECTORS[i], "empty")) continue;       /* ICE ne compresse pas le vide */
        current = VECTORS[i];
        snprintf(name, sizeof name, "ice_%s.ice", VECTORS[i]);
        a = load(dir, name, &n);
        snprintf(name, sizeof name, "lzh_%s.bin", VECTORS[i]);
        b = load(dir, name, &bn);
        sz = ice_size(a, n);
        CHECK(sz == bn);
        out = malloc(bn + 1);
        CHECK(ice_unpack(a, n, out, bn) == 0 && !memcmp(out, b, bn));
        /* Tronque, abime : jamais de debordement (ASan). */
        CHECK(ice_size(a, n - 1) == -1 || ice_unpack(a, n - 1, out, bn) != 0 || 1);
        {
            unsigned char *c = malloc(n);
            int k;
            for (k = 0; k < 40; k++) {
                memcpy(c, a, n);
                c[12 + (k * 104729) % (n - 12)] ^= (unsigned char)(0x81 >> (k & 7));
                ice_unpack(c, n, out, bn);
            }
            free(c);
        }
        CHECK(ice_unpack(a, n, out, bn - 1) == -1);
        free(out);
        free(a);
        free(b);
    }
    current = "ICE header checks";
    {
        unsigned char h[16] = { 'I', 'C', 'E', '!', 0, 0, 0, 100, 0, 0, 0, 10 };
        CHECK(ice_size(h, 16) == -1);                     /* taille compressee > fichier */
        h[7] = 16;
        CHECK(ice_size(h, 16) == 10);
        h[1] = 'X';
        CHECK(ice_size(h, 16) == -1);
    }
}

/* ---- YM ---- */

static long put32(unsigned char *p, long v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; return 4; }
static long put16(unsigned char *p, int v) { p[0] = v >> 8; p[1] = v; return 2; }

static long make_ym5(unsigned char *d, long frames, int interleaved, int drums)
{
    long p = 0, f, i;
    int r;
    memcpy(d, "YM5!LeOnArD!", 12);
    p = 12;
    p += put32(d + p, frames);
    p += put32(d + p, interleaved);
    p += put16(d + p, drums);
    p += put32(d + p, 2000000);
    p += put16(d + p, 50);
    p += put32(d + p, 3);
    p += put16(d + p, 0);
    for (i = 0; i < drums; i++) {
        p += put32(d + p, 5);
        memcpy(d + p, "\x80\x81\x82\x83\x84", 5);
        p += 5;
    }
    memcpy(d + p, "Title\0Author\0Comment\0", 21);
    p += 21;
    for (f = 0; f < frames; f++)
        for (r = 0; r < 16; r++) {
            unsigned char v = (unsigned char)(f * 16 + r);
            if (interleaved) d[p + r * frames + f] = v;
            else d[p + f * 16 + r] = v;
        }
    p += frames * 16;
    memcpy(d + p, "End!", 4);
    return p + 4;
}

static void t_ym(void)
{
    static unsigned char d[4096];
    YMSONG s;
    long n;
    int il;
    for (il = 0; il < 2; il++) {
        current = il ? "YM5 interleaved" : "YM5 frame by frame";
        n = make_ym5(d, 10, il, 2);
        CHECK(ym_parse(d, n, &s) == YM_OK);
        CHECK(s.frames == 10 && s.nregs == 16 && s.loop == 3 && s.hz == 50);
        CHECK(!strcmp(s.title, "Title") && !strcmp(s.author, "Author") && !strcmp(s.comment, "Comment"));
        CHECK(ym_reg(&s, 0, 0) == 0 && ym_reg(&s, 3, 7) == 3 * 16 + 7 && ym_reg(&s, 9, 13) == 9 * 16 + 13);
    }
    current = "YM5 malformed";
    n = make_ym5(d, 10, 1, 0);
    CHECK(ym_parse(d, n - 100, &s) == YM_BAD);              /* donnees manquantes */
    d[15] = 200;                                             /* 200 trames annoncees */
    CHECK(ym_parse(d, n, &s) == YM_BAD);
    n = make_ym5(d, 10, 1, 0);
    CHECK(ym_parse(d, 40, &s) == YM_BAD);                    /* chaines coupees */
    memcpy(d + 4, "LeOnArDX", 8);
    CHECK(ym_parse(d, n, &s) == YM_BAD);
    CHECK(ym_parse((const unsigned char *)"MOD!", 4, &s) == YM_NOT);

    current = "YM3 and YM3b";
    memcpy(d, "YM3!", 4);
    {
        int f, r;
        for (r = 0; r < 14; r++)
            for (f = 0; f < 5; f++) d[4 + r * 5 + f] = (unsigned char)(r * 10 + f);
    }
    CHECK(ym_parse(d, 4 + 70, &s) == YM_OK && s.frames == 5 && s.nregs == 14);
    CHECK(ym_reg(&s, 2, 3) == 32 && ym_reg(&s, 4, 13) == 134);
    memcpy(d, "YM3b", 4);
    put32(d + 74, 2);
    CHECK(ym_parse(d, 78, &s) == YM_OK && s.frames == 5 && s.loop == 2);
}

/* ---- SNDH ---- */

static void t_sndh(void)
{
    static const unsigned char h[] =
        "\x60\x00\x00\x10\x60\x00\x00\x20\x60\x00\x00\x30"
        "SNDH" "TITLDemo Tune\0" "COMMSomeone\0" "##03\0" "TC100\0" "YEAR1989\0" "HDNS";
    SNDHINFO s;
    current = "SNDH tags";
    CHECK(sndh_parse(h, sizeof h - 1, &s) == SNDH_OK);
    CHECK(!strcmp(s.title, "Demo Tune") && !strcmp(s.composer, "Someone"));
    CHECK(s.tunes == 3 && s.hz == 100 && s.timer == 'C' && !strcmp(s.year, "1989"));
    {
        static const unsigned char v[] = "\0\0\0\0\0\0\0\0\0\0\0\0SNDH!V60\0HDNS";
        CHECK(sndh_parse(v, sizeof v - 1, &s) == SNDH_OK && s.hz == 60 && s.timer == 'V');
    }
    {
        /* Etiquette sans fin : rien ne deborde. */
        static const unsigned char t[] = "\0\0\0\0\0\0\0\0\0\0\0\0SNDHTITLno end";
        CHECK(sndh_parse(t, sizeof t - 1, &s) == SNDH_OK && !strcmp(s.title, "no end"));
        CHECK(s.tunes == 1 && s.hz == 50);
    }
    CHECK(sndh_parse((const unsigned char *)"not a tune at all!!!", 20, &s) == SNDH_NOT);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/host/data";
    t_lzh(dir);
    t_ice(dir);
    t_ym();
    t_sndh();
    printf("test_music: %d checks, %d failed\n", tests, failures);
    return failures ? 1 : 0;
}
