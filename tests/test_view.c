/*
 * test_view.c -- decodeurs d'images et mise en page du texte, sur l'hote.
 *
 * Les images sont fabriquees ici (Degas brut, Degas Elite compresse avec un
 * PackBits de reference, NEOchrome) ; les fichiers malformes et aleatoires
 * doivent donner une erreur sans jamais lire ni ecrire hors des tampons
 * (AddressSanitizer). Le texte : fins de ligne, tabulations, coupures,
 * 1st Word, hexadecimal, et l'aller-retour ligne suivante / precedente.
 */
#include "../src/picture.h"
#include "../src/textview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests, failures;
static const char *current;
#define CHECK(c) do { tests++; if (!(c)) { failures++; \
    printf("FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, current, #c); } } while (0)

static unsigned char bm[PIC_BYTES], ref[PIC_BYTES], file[80000];

static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }

static void make_ref(int seed)
{
    long i;
    unsigned x = (unsigned)seed * 2654435761u;
    for (i = 0; i < PIC_BYTES; i++) {
        /* Des plages pour que PackBits ait des repetitions et des litteraux. */
        if ((i / 37) % 3 == 0) ref[i] = (unsigned char)(i / 37);
        else { x = x * 1103515245u + 12345u; ref[i] = (unsigned char)(x >> 16); }
    }
}

static long packbits(const unsigned char *src, int n, unsigned char *out)
{
    long o = 0;
    int i = 0;
    while (i < n) {
        int run = 1;
        while (i + run < n && run < 128 && src[i + run] == src[i]) run++;
        if (run >= 3) {
            out[o++] = (unsigned char)(257 - run);
            out[o++] = src[i];
            i += run;
        } else {
            int j = i, k;
            while (j < n && j - i < 128 && !(j + 2 < n && src[j] == src[j + 1] && src[j] == src[j + 2])) j++;
            out[o++] = (unsigned char)(j - i - 1);
            for (k = i; k < j; k++) out[o++] = src[k];
            i = j;
        }
    }
    return o;
}

/* Degas Elite : chaque ligne, plan par plan. */
static long make_pc(int res)
{
    int planes = res == 0 ? 4 : res == 1 ? 2 : 1, lines = res == 2 ? 400 : 200;
    int bpp = planes == 4 ? 40 : 80, y, p, g;
    long n = 34;
    unsigned char line[160];
    put16(file, 0x8000 | res);
    for (p = 0; p < 16; p++) put16(file + 2 + p * 2, 0x111 * (p & 7));
    for (y = 0; y < lines; y++) {
        if (planes == 1) memcpy(line, ref + y * 80, 80);
        else
            for (p = 0; p < planes; p++)
                for (g = 0; g < bpp / 2; g++) {
                    line[p * bpp + g * 2] = ref[y * 160 + g * planes * 2 + p * 2];
                    line[p * bpp + g * 2 + 1] = ref[y * 160 + g * planes * 2 + p * 2 + 1];
                }
        n += packbits(line, planes * bpp, file + n);
    }
    return n;
}

static void t_degas(void)
{
    PICINFO info;
    int r;
    current = "Degas PI1/PI2/PI3";
    make_ref(1);
    put16(file, 1);
    put16(file + 2, 0x0777);
    put16(file + 4, 0xf123);            /* bits hauts : ignores */
    memcpy(file + 34, ref, PIC_BYTES);
    r = pic_decode("A.PI2", file, 34 + PIC_BYTES, &info, bm);
    CHECK(r == PIC_OK && info.res == 1 && !memcmp(bm, ref, PIC_BYTES));
    CHECK(info.pal[0] == 0x777 && info.pal[1] == 0x123);
    CHECK(pic_decode("A.PI2", file, 34 + PIC_BYTES - 1, &info, bm) == PIC_BAD);
    CHECK(pic_decode("A.PI2", file, 20, &info, bm) == PIC_UNKNOWN);
    put16(file, 7);
    CHECK(pic_decode("A.PI1", file, 34 + PIC_BYTES, &info, bm) == PIC_UNKNOWN);
    put16(file, 0);
    CHECK(pic_decode("A.TXT", file, 34 + PIC_BYTES, &info, bm) == PIC_UNKNOWN);
    CHECK(pic_is_picture_name("X.pi3") && pic_is_picture_name("TITLE.NEO"));
    CHECK(!pic_is_picture_name("PI1") && !pic_is_picture_name("A.PI") && !pic_is_picture_name("A.PI12"));
}

static void t_elite(void)
{
    PICINFO info;
    int res;
    current = "Degas Elite PC1/PC2/PC3";
    for (res = 0; res < 3; res++) {
        static const char *const names[] = { "A.PC1", "A.PC2", "A.PC3" };
        long n;
        make_ref(res + 10);
        n = make_pc(res);
        memset(bm, 0xee, PIC_BYTES);
        CHECK(pic_decode(names[res], file, n, &info, bm) == PIC_OK);
        CHECK(info.res == res && !memcmp(bm, ref, PIC_BYTES));
        /* Tronque n'importe ou : erreur, jamais de debordement. */
        CHECK(pic_decode(names[res], file, n - 1, &info, bm) == PIC_BAD);
        CHECK(pic_decode(names[res], file, 40, &info, bm) == PIC_BAD);
    }
    /* Une repetition qui deborde la ligne est refusee. */
    put16(file, 0x8000);
    memset(file + 2, 0, 32);
    file[34] = (unsigned char)(257 - 128);
    file[35] = 0xaa;
    file[36] = (unsigned char)(257 - 128);
    file[37] = 0xaa;
    CHECK(pic_decode("A.PC1", file, 38, &info, bm) == PIC_BAD);
}

static void t_neo(void)
{
    PICINFO info;
    current = "NEOchrome";
    make_ref(3);
    memset(file, 0, 128);
    put16(file + 2, 0);
    put16(file + 4, 0x700);
    memcpy(file + 128, ref, PIC_BYTES);
    CHECK(pic_decode("A.NEO", file, 128 + PIC_BYTES, &info, bm) == PIC_OK);
    CHECK(info.res == 0 && info.pal[0] == 0x700 && !memcmp(bm, ref, PIC_BYTES));
    CHECK(pic_decode("A.NEO", file, 128 + PIC_BYTES - 5, &info, bm) == PIC_BAD);
    put16(file, 1);
    CHECK(pic_decode("A.NEO", file, 128 + PIC_BYTES, &info, bm) == PIC_UNKNOWN);
}

static void t_fuzz(void)
{
    PICINFO info;
    unsigned x = 12345;
    int it;
    long i;
    static const char *const names[] = { "F.PC1", "F.PC2", "F.PC3", "F.PI1", "F.NEO" };
    current = "random files";
    for (it = 0; it < 3000; it++) {
        long n = 34 + (x % 40000);
        for (i = 0; i < n; i++) { x = x * 1103515245u + 12345u; file[i] = (unsigned char)(x >> 16); }
        file[0] = (unsigned char)(it & 1 ? 0x80 : 0);
        file[1] = (unsigned char)(it % 3);
        /* Le resultat importe peu : ne pas deborder (ASan). */
        pic_decode(names[it % 5], file, n, &info, bm);
    }
    CHECK(1);
}

static void t_conversions(void)
{
    PICINFO info;
    static unsigned char mono[PIC_BYTES], med[PIC_BYTES];
    int i, x, y, ok = 1;
    current = "conversions";
    memset(&info, 0, sizeof info);
    info.res = PIC_LOW;
    for (i = 0; i < 16; i++) info.pal[i] = 0x777;
    info.pal[0] = 0x000;
    /* Moitie gauche couleur 0 (noir), moitie droite couleur 5 (blanc). */
    memset(bm, 0, PIC_BYTES);
    for (y = 0; y < 200; y++)
        for (x = 160; x < 320; x += 16) {
            put16(bm + y * 160 + (x / 16) * 8, 0xffff);
            put16(bm + y * 160 + (x / 16) * 8 + 4, 0xffff);
        }
    CHECK(pic_pixel(PIC_LOW, bm, 10, 10) == 0 && pic_pixel(PIC_LOW, bm, 200, 10) == 5);
    pic_to_mono(&info, bm, mono);
    for (y = 0; y < 400; y++) {
        for (x = 0; x < 40; x++) if (mono[y * 80 + x] != 0xff) ok = 0;
        for (x = 40; x < 80; x++) if (mono[y * 80 + x] != 0x00) ok = 0;
    }
    CHECK(ok);
    /* Gris moyen : environ la moitie des points noirs. */
    info.pal[0] = 0x333;
    pic_to_mono(&info, bm, mono);
    {
        long blacks = 0;
        for (y = 0; y < 400; y++)
            for (x = 0; x < 40; x++) {
                unsigned char b = mono[y * 80 + x];
                while (b) { blacks += b & 1; b >>= 1; }
            }
        CHECK(blacks > 320L * 400 * 4 / 10 && blacks < 320L * 400 * 7 / 10);
    }
    /* Moyenne resolution : meme resultat par les quartets de plans. */
    info.res = PIC_MED;
    info.pal[0] = 0x000;
    info.pal[1] = 0x777;
    info.pal[2] = 0x777;
    info.pal[3] = 0x000;
    for (i = 0; i < PIC_BYTES; i++) bm[i] = (unsigned char)(i * 7);
    pic_to_mono(&info, bm, mono);
    ok = 1;
    for (y = 0; y < 400 && ok; y++)
        for (x = 0; x < 640; x++) {
            int c = pic_pixel(PIC_MED, bm, x, y / 2);
            int black = (mono[y * 80 + x / 8] >> (7 - x % 8)) & 1;
            if (black != (c == 0 || c == 3)) { ok = 0; break; }
        }
    CHECK(ok);
    /* Monochrome vers moyenne : 0, 1 ou 2 points noirs par paire de lignes. */
    memset(mono, 0, PIC_BYTES);
    mono[0] = 0x80;                 /* ligne 0, point 0 */
    mono[80] = 0x80;                /* ligne 1, point 0 : noir */
    mono[1] = 0x80;                 /* ligne 0, point 8 seul : gris */
    pic_mono_to_medium(mono, med);
    CHECK(pic_pixel(PIC_MED, med, 0, 0) == 3);
    CHECK(pic_pixel(PIC_MED, med, 8, 0) == 1);
    CHECK(pic_pixel(PIC_MED, med, 9, 0) == 0);
    CHECK(pic_gray_pal[0] == 0x777 && pic_gray_pal[3] == 0x000);
}

/* ---- Texte ---- */

static TEXTDOC doc(const char *s)
{
    TEXTDOC d;
    d.data = (const unsigned char *)s;
    d.size = (long)strlen(s);
    d.firstword = 0;
    d.hex = 0;
    return d;
}

static void rstrip(char *s)
{
    int l = (int)strlen(s);
    while (l > 0 && s[l - 1] == ' ') s[--l] = 0;
}

static void t_text_basics(void)
{
    char out[TV_COLS + 1];
    TEXTDOC d = doc("one\r\ntwo\nthree\rfour\n\rfive");
    long off = 0;
    const char *want[] = { "one", "two", "three", "four", "five" };
    int i;
    current = "line endings";
    for (i = 0; i < 5; i++) {
        off = tv_layout(&d, off, out);
        rstrip(out);
        CHECK(!strcmp(out, want[i]));
    }
    CHECK(off == d.size);

    current = "tabs and controls";
    d = doc("a\tb\t\tc\x01\x1f!");
    tv_layout(&d, 0, out);
    rstrip(out);
    CHECK(!strcmp(out, "a       b               c..!"));

    current = "word wrap";
    {
        static char longline[400];
        char *p = longline;
        for (i = 0; i < 30; i++) p += sprintf(p, "word%02d ", i);
        d = doc(longline);
        off = tv_layout(&d, 0, out);
        rstrip(out);
        CHECK(strlen(out) <= 80 && out[strlen(out) - 1] != ' ');
        CHECK(!strncmp(longline + off, "word", 4));      /* coupe entre deux mots */
    }
    current = "hard wrap";
    {
        static char solid[200];
        memset(solid, 'x', 170);
        solid[170] = 0;
        d = doc(solid);
        CHECK(tv_layout(&d, 0, out) == 80);
        CHECK(tv_layout(&d, 80, out) == 160);
        CHECK(tv_layout(&d, 160, out) == 170);
    }
}

static void t_text_firstword(void)
{
    char out[TV_COLS + 1];
    const char *s = "\x1f" "9[....]001\r\nHello \x1b\x81" "bold\x1b\x80" " and\x1e" "space\r\n"
                    "\x1f" "9[..]001\r\nEnd\r\n";
    TEXTDOC d = doc(s);
    long off;
    current = "1st Word";
    CHECK(tv_is_firstword("LETTER.DOC", d.data, d.size));
    CHECK(!tv_is_firstword("LETTER.TXT", d.data, d.size));
    d.firstword = 1;
    off = tv_layout(&d, 0, out);
    rstrip(out);
    CHECK(!strcmp(out, "Hello bold and space"));
    off = tv_layout(&d, off, out);
    rstrip(out);
    CHECK(!strcmp(out, "End"));
    CHECK(off == d.size);
    /* Reculer depuis "End" ramene a "Hello", pas a la ligne de format. */
    {
        long end_line = tv_layout(&d, 0, 0);
        long back = tv_prev(&d, end_line);
        tv_layout(&d, back, out);
        rstrip(out);
        CHECK(!strcmp(out, "Hello bold and space"));
    }
}

static void t_text_hex(void)
{
    char out[TV_COLS + 1];
    TEXTDOC d = doc("ABCDEFGHIJKLMNOPQR\n");
    current = "hex";
    d.hex = 1;
    CHECK(tv_layout(&d, 0, out) == 16);
    CHECK(!strncmp(out, "00000000  41 42 43 44 45 46 47 48  49 4A", 40));
    CHECK(!strncmp(out + 61, "ABCDEFGHIJKLMNOP", 16));
    CHECK(tv_layout(&d, 16, out) == d.size);
    CHECK(!strncmp(out, "00000010  51 52 0A", 18));
    CHECK(out[63] == '.');
    CHECK(tv_prev(&d, 16) == 0 && tv_line_of(&d, 17) == 16);
}

/* Toutes les lignes en avancant, puis en reculant : les memes debuts. */
static void round_trip(const char *label, TEXTDOC *d)
{
    static long starts[20000];
    long off = 0, back;
    int n = 0, i, ok = 1;
    current = label;
    while (off < d->size && n < 20000) {
        long next;
        starts[n++] = off;
        next = tv_layout(d, off, 0);
        if (next <= off) { ok = 0; break; }
        off = next;
    }
    CHECK(ok);
    back = starts[n - 1];
    for (i = n - 2; i >= 0; i--) {
        back = tv_prev(d, back);
        if (back != starts[i]) { ok = 0; break; }
    }
    CHECK(ok);
    for (i = 0; i < n; i++)
        if (tv_line_of(d, starts[i]) != starts[i]) { ok = 0; break; }
    CHECK(ok);
}

static void t_text_round_trip(void)
{
    static char buf[60000];
    unsigned x = 7;
    int i;
    TEXTDOC d;
    char *p = buf;
    /* Paragraphes de longueurs variees, CRLF, lignes vides, tabulations. */
    for (i = 0; i < 400; i++) {
        int words = (int)(x % 40), k;
        for (k = 0; k < words; k++) {
            x = x * 1103515245u + 12345u;
            p += sprintf(p, "%.*s%s", (int)(1 + (x >> 16) % 12), "abcdefghijklmnop",
                         (x >> 8) % 9 == 0 ? "\t" : " ");
        }
        p += sprintf(p, (x & 1) ? "\r\n" : "\n");
        if (i % 17 == 0) p += sprintf(p, "\r\n");
    }
    d = doc(buf);
    round_trip("next/prev agree on text", &d);
    d.firstword = 1;
    round_trip("next/prev agree with 1st Word parsing", &d);
    d.firstword = 0;
    d.hex = 1;
    round_trip("next/prev agree in hex", &d);
}

static void t_find(void)
{
    TEXTDOC d = doc("The quick brown Fox\njumps over the lazy fox.");
    current = "find";
    CHECK(tv_find(&d, 0, "fox") == 16);
    CHECK(tv_find(&d, 17, "FOX") == 40);
    CHECK(tv_find(&d, 41, "fox") == -1);
    CHECK(tv_find(&d, 0, "") == -1);
    CHECK(tv_find(&d, 0, "fox.x") == -1);
}

int main(void)
{
    t_degas();
    t_elite();
    t_neo();
    t_fuzz();
    t_conversions();
    t_text_basics();
    t_text_firstword();
    t_text_hex();
    t_text_round_trip();
    t_find();
    printf("test_view: %d checks, %d failed\n", tests, failures);
    return failures ? 1 : 0;
}
