/*
 * picture.c -- decodage des images Degas, Degas Elite et NEOchrome.
 *
 * Degas (.PI1/.PI2/.PI3) : un mot de resolution, 16 mots de palette, puis
 * les 32000 octets de l'ecran tels quels (34 + 32000 octets ; Degas Elite
 * ajoute 32 octets d'animation, ignores).
 * Degas Elite compresse (.PC1/.PC2/.PC3) : resolution avec le bit 15, la
 * palette, puis chaque ligne d'ecran compressee plan par plan (PackBits).
 * NEOchrome (.NEO) : en-tete de 128 octets (drapeau, resolution, palette a
 * l'offset 4), puis les 32000 octets de l'ecran.
 */
#include "picture.h"
#include "libc.h"

static int ext_is(const char *name, const char *ext)
{
    const char *d = strrchr(name, '.');
    if (!d) return 0;
    d++;
    while (*d && *ext) {
        if (toupper((unsigned char)*d) != *ext) return 0;
        d++;
        ext++;
    }
    return *d == 0 && *ext == 0;
}

int pic_is_picture_name(const char *name)
{
    return ext_is(name, "PI1") || ext_is(name, "PI2") || ext_is(name, "PI3") ||
           ext_is(name, "PC1") || ext_is(name, "PC2") || ext_is(name, "PC3") ||
           ext_is(name, "NEO");
}

static unsigned short be16(const unsigned char *p)
{
    return (unsigned short)((p[0] << 8) | p[1]);
}

static void read_pal(PICINFO *info, const unsigned char *p)
{
    int i;
    for (i = 0; i < 16; i++) info->pal[i] = be16(p + 2 * i) & 0x0fff;
}

/* Octets par ligne et par plan, et nombre de plans. */
static void geometry(int res, int *planes, int *lines)
{
    *planes = res == PIC_LOW ? 4 : res == PIC_MED ? 2 : 1;
    *lines = res == PIC_HIGH ? 400 : 200;
}

/* PackBits ligne par ligne, plan par plan, vers le bitmap entrelace. */
static int unpack_degas(int res, const unsigned char *src, long size, unsigned char *out)
{
    int planes, lines, y, p, x;
    long pos = 0;
    unsigned char line[160];
    int bpp;                    /* octets par plan et par ligne */

    geometry(res, &planes, &lines);
    bpp = planes == 4 ? 40 : 80;
    for (y = 0; y < lines; y++) {
        int n = 0, need = planes * bpp;
        while (n < need) {
            int c;
            if (pos >= size) return PIC_BAD;
            c = (signed char)src[pos++];
            if (c >= 0) {
                int k = c + 1;
                if (n + k > need || pos + k > size) return PIC_BAD;
                memcpy(line + n, src + pos, k);
                pos += k;
                n += k;
            } else if (c != -128) {
                int k = 1 - c;
                if (n + k > need || pos >= size) return PIC_BAD;
                memset(line + n, src[pos++], k);
                n += k;
            }
        }
        /* line = plan 0 (bpp octets), plan 1, ... -> mots entrelaces.
         * Des pointeurs qui avancent : pas de multiplication par pixel. */
        if (planes == 1) {
            memcpy(out + (long)y * 80, line, 80);
            continue;
        }
        for (p = 0; p < planes; p++) {
            const unsigned char *sp = line + p * bpp;
            unsigned char *d = out + (long)y * 160 + p * 2;
            int step = planes * 2;
            for (x = 0; x < bpp; x += 2, sp += 2, d += step) {
                d[0] = sp[0];
                d[1] = sp[1];
            }
        }
    }
    return PIC_OK;
}

int pic_decode(const char *name, const unsigned char *data, long size,
               PICINFO *info, unsigned char *out)
{
    unsigned short r;

    if (ext_is(name, "NEO")) {
        if (size < 128 + PIC_BYTES) return size < 128 ? PIC_UNKNOWN : PIC_BAD;
        r = be16(data + 2);
        if (be16(data) != 0 || r > 2) return PIC_UNKNOWN;
        info->res = r;
        info->format = "NEOchrome";
        read_pal(info, data + 4);
        memcpy(out, data + 128, PIC_BYTES);
        return PIC_OK;
    }
    if (size < 34) return PIC_UNKNOWN;
    r = be16(data);
    if ((r & 0x7fff) > 2) return PIC_UNKNOWN;
    info->res = r & 3;
    read_pal(info, data + 2);
    if (r & 0x8000) {
        if (!ext_is(name, "PC1") && !ext_is(name, "PC2") && !ext_is(name, "PC3"))
            return PIC_UNKNOWN;
        info->format = "Degas Elite";
        return unpack_degas(info->res, data + 34, size - 34, out);
    }
    if (!ext_is(name, "PI1") && !ext_is(name, "PI2") && !ext_is(name, "PI3"))
        return PIC_UNKNOWN;
    if (size < 34 + PIC_BYTES) return PIC_BAD;
    info->format = "Degas";
    memcpy(out, data + 34, PIC_BYTES);
    return PIC_OK;
}

int pic_pixel(int res, const unsigned char *bm, int x, int y)
{
    int planes = res == PIC_LOW ? 4 : res == PIC_MED ? 2 : 1, p, v = 0;
    const unsigned char *w;
    unsigned short bit = (unsigned short)(0x8000 >> (x & 15));
    if (planes == 1) return (bm[(long)y * 80 + (x >> 3)] >> (7 - (x & 7))) & 1;
    w = bm + (long)y * 160 + (x >> 4) * planes * 2;
    for (p = 0; p < planes; p++)
        if (((w[p * 2] << 8) | w[p * 2 + 1]) & bit) v |= 1 << p;
    return v;
}

/* Luminance 0..15 d'un registre de couleur. Sur les 3 bits du ST : le
 * blanc $777 doit valoir 15 (le demi-pas du STE ne change pas le tramage). */
static int luma(unsigned short c)
{
    int r = (c >> 8) & 7, g = (c >> 4) & 7, b = c & 7;
    return (r * 5 + g * 9 + b * 2) * 15 / 112;
}

/* spread[b] : les 8 bits de l'octet b, un par octet (0 ou 1), en deux longs
 * (4 pixels chacun, l'octet de poids fort a gauche). */
/* unsigned int : 32 bits sur le 68000 comme sur l'hote (un long y fait 64). */
static unsigned int spread_hi[256], spread_lo[256];
static int spread_ready;

static void make_spread(void)
{
    /* Rempli octet par octet : juste sur le 68000 comme sur l'hote des
     * tests, quel que soit l'ordre des octets d'un long. */
    int b, k;
    for (b = 0; b < 256; b++) {
        unsigned char hi[4], lo[4];
        for (k = 0; k < 4; k++) {
            hi[k] = (unsigned char)((b >> (7 - k)) & 1);
            lo[k] = (unsigned char)((b >> (3 - k)) & 1);
        }
        memcpy(&spread_hi[b], hi, 4);
        memcpy(&spread_lo[b], lo, 4);
    }
    spread_ready = 1;
}

/* Indices de couleur d'une ligne source (320 ou 640 pixels), 8 a la fois :
 * chaque octet de plan, etale par la table, apporte son bit a 8 indices. */
static void row_indices(int res, const unsigned char *bm, int y, unsigned char *idx)
{
    int planes = res == PIC_LOW ? 4 : 2, groups = res == PIC_LOW ? 20 : 40, g, half, p;
    const unsigned char *w = bm + (long)y * 160;
    unsigned int *o = (unsigned int *)idx;
    if (!spread_ready) make_spread();
    for (g = 0; g < groups; g++, w += planes * 2) {
        for (half = 0; half < 2; half++) {
            unsigned int hi = 0, lo = 0;
            for (p = planes - 1; p >= 0; p--) {
                unsigned char b = w[p * 2 + half];
                hi = (hi << 1) | spread_hi[b];
                lo = (lo << 1) | spread_lo[b];
            }
            *o++ = hi;
            *o++ = lo;
        }
    }
}

void pic_to_mono(const PICINFO *info, const unsigned char *in, unsigned char *out)
{
    /* Tramage de Bayer 4 x 4 ; sur le moniteur monochrome, 1 = noir. Le
     * motif se repete tous les 4 points : un quartet de sortie ne depend que
     * de la phase verticale et des couleurs de ses points, d'ou des tables
     * par image (la palette change tout) :
     *   basse res.  : 2 points source (deux indices) -> 4 points de sortie ;
     *   moyenne res : les quartets des deux plans -> 4 points de sortie. */
    static const unsigned char bayer[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    static unsigned char tab[4][256];
    static unsigned int idxbuf[80];         /* 320 indices, alignes */
    unsigned char *idx = (unsigned char *)idxbuf;
    unsigned char black[16][4];             /* [couleur][colonne] pour la phase */
    int c, q, k, i, y, b;

    for (q = 0; q < 4; q++) {
        for (c = 0; c < 16; c++) {
            int l = luma(info->pal[c]);
            /* Noir si l/15 < (seuil + 1/2)/16 : 0 tout noir, 15 tout blanc. */
            for (k = 0; k < 4; k++)
                black[c][k] = l * 32 < (2 * bayer[q][k] + 1) * 15;
        }
        for (i = 0; i < 256; i++) {
            unsigned char v = 0;
            for (k = 0; k < 4; k++) {
                int col;
                if (info->res == PIC_LOW)
                    col = k < 2 ? i >> 4 : i & 15;             /* 2 points de large */
                else
                    col = ((i >> (7 - k)) & 1) << 1 | ((i >> (3 - k)) & 1);
                if (black[col][k]) v |= (unsigned char)(8 >> k);
            }
            tab[q][i] = v;
        }
    }
    for (y = 0; y < 200; y++) {
        const unsigned char *w = in + (long)y * 160;
        if (info->res == PIC_LOW) row_indices(PIC_LOW, in, y, idx);
        for (k = 0; k < 2; k++) {
            int oy = y * 2 + k;
            const unsigned char *t = tab[oy & 3];
            unsigned char *o = out + (long)oy * 80;
            if (info->res == PIC_LOW) {
                const unsigned char *s = idx;
                for (b = 0; b < 80; b++, s += 4)
                    o[b] = (unsigned char)(t[(s[0] << 4) | s[1]] << 4 | t[(s[2] << 4) | s[3]]);
            } else {
                /* Moyenne resolution : mot plan 0, mot plan 1, par 16 points. */
                const unsigned char *g = w;
                for (b = 0; b < 80; b += 2, g += 4) {
                    unsigned char p0 = g[0], p1 = g[2];
                    o[b] = (unsigned char)(t[(p1 & 0xf0) | (p0 >> 4)] << 4 |
                                           t[((p1 & 15) << 4) | (p0 & 15)]);
                    p0 = g[1];
                    p1 = g[3];
                    o[b + 1] = (unsigned char)(t[(p1 & 0xf0) | (p0 >> 4)] << 4 |
                                               t[((p1 & 15) << 4) | (p0 & 15)]);
                }
            }
        }
    }
}

const unsigned short pic_gray_pal[4] = { 0x777, 0x444, 0x444, 0x000 };

void pic_mono_to_medium(const unsigned char *in, unsigned char *out)
{
    /* Deux lignes monochromes -> une ligne moyenne resolution : un point noir
     * sur deux donne le gris (plan 0 ou plan 1), deux donnent le noir. */
    int y, g;
    for (y = 0; y < 200; y++) {
        const unsigned char *a = in + (long)y * 160, *b = a + 80;
        unsigned char *d = out + (long)y * 160;
        for (g = 0; g < 40; g++) {
            unsigned short wa = (unsigned short)((a[g * 2] << 8) | a[g * 2 + 1]);
            unsigned short wb = (unsigned short)((b[g * 2] << 8) | b[g * 2 + 1]);
            unsigned short both = wa & wb, one = wa ^ wb;
            unsigned short p0 = both | one, p1 = both;
            d[g * 4] = (unsigned char)(p0 >> 8);
            d[g * 4 + 1] = (unsigned char)p0;
            d[g * 4 + 2] = (unsigned char)(p1 >> 8);
            d[g * 4 + 3] = (unsigned char)p1;
        }
    }
}
