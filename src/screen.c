/*
 * screen.c -- l'ecran texte de TOSFC.
 *
 * Moyenne resolution (640 x 200, 4 couleurs) : police systeme 8 x 8.
 * Haute resolution (640 x 400, monochrome) : police systeme 8 x 16.
 * En basse resolution, on passe en moyenne le temps du programme et on remet
 * tout (resolution, palette) en partant. Les polices viennent de la Line-A :
 * ce sont celles de la ROM, a l'identique sur tous les TOS et EmuTOS.
 */
#include "screen.h"
#include "tos.h"
#include "libc.h"

unsigned char scr_chr[SCR_ROWS * SCR_COLS] __attribute__((aligned(4)));
unsigned char scr_att[SCR_ROWS * SCR_COLS] __attribute__((aligned(4)));
short scr_mono;
short scr_cell_h;

static unsigned char drawn_chr[SCR_ROWS * SCR_COLS] __attribute__((aligned(4)));
static unsigned char drawn_att[SCR_ROWS * SCR_COLS] __attribute__((aligned(4)));
static unsigned char font[256 * 16];
static unsigned char *vram;
static short old_rez = -1;
static short old_pal[16];
static short pal_saved;
static short ptr_x = -1, ptr_y = -1;
static short drawn_ptr_x = -1, drawn_ptr_y = -1;
/* Lignes a comparer au prochain scr_flush : une trame sans changement ne
 * coute presque rien, au lieu de 2000 comparaisons. */
unsigned char scr_row_dirty[SCR_ROWS];
#define row_dirty scr_row_dirty
static void *linea_vars;

/* Couleurs de fond (bits hauts) et de texte (bits bas) par attribut.
 * Palette : 0 bleu, 1 cyan, 2 jaune, 3 blanc. */
static const unsigned char att_color[A_COUNT] = {
    0x01,   /* A_NORMAL : cyan sur bleu */
    0x01,   /* A_FRAME */
    0x02,   /* A_TAG : jaune sur bleu */
    0x10,   /* A_CURSOR : bleu sur cyan */
    0x12,   /* A_CURTAG : jaune sur cyan */
    0x30,   /* A_DIALOG : bleu sur blanc */
    0x13,   /* A_DLGSEL : blanc sur cyan */
    0x03,   /* A_BARKEY : blanc sur bleu */
    0x10,   /* A_BARTXT : bleu sur cyan */
    0x03,   /* A_TITLE : blanc sur bleu */
};
/* Monochrome : 1 = inverse video. */
static const unsigned char att_mono_rev[A_COUNT] = {
    0, 0, 0, 1, 1, 0, 1, 0, 1, 0
};
static const short palette[4] = { 0x004, 0x066, 0x760, 0x777 };

static void make_masks(void);

/* ---- Line-A ---- */

static void linea_init(void)
{
    register void *a0 __asm__("a0");
    register void *a1 __asm__("a1");
    void **fonts;
    const unsigned char *hdr;
    const unsigned short *off;
    const unsigned char *dat;
    short first, last, fw, fh, c, r;

    __asm__ volatile (".dc.w 0xa000" : "=r"(a0), "=r"(a1)
                      : : "d0", "d1", "d2", "a2", "memory", "cc");
    linea_vars = a0;
    fonts = (void **)a1;
    hdr = fonts[scr_cell_h == 16 ? 2 : 1];

    first = *(const short *)(hdr + 36);
    last = *(const short *)(hdr + 38);
    off = *(const unsigned short **)(hdr + 72);
    dat = *(const unsigned char **)(hdr + 76);
    fw = *(const short *)(hdr + 80);
    fh = *(const short *)(hdr + 82);
    if (fh > scr_cell_h) fh = scr_cell_h;
    memset(font, 0, sizeof font);
    for (c = first; c <= last && c < 256; c++) {
        const unsigned char *g = dat + (off[c - first] >> 3);
        for (r = 0; r < fh; r++)
            font[c * 16 + r] = g[r * fw];
    }
}

static void linea_hide_mouse(void)
{
    __asm__ volatile (".dc.w 0xa00a" : : : "d0", "d1", "d2", "a0", "a1", "a2",
                      "memory", "cc");
}

static void linea_show_mouse(void)
{
    /* $A009 lit INTIN[0] : 1 retire notre masquage sans forcer l'affichage.
     * Depuis le dossier AUTO, le VDI n'est pas encore ouvert et le pointeur
     * INTIN de la Line-A est nul : on lui prete notre propre tableau. */
    short **intin = (short **)((char *)linea_vars + 8);
    short *old = *intin;
    static short mine[2];
    mine[0] = 1;
    *intin = mine;
    __asm__ volatile (".dc.w 0xa009" : : : "d0", "d1", "d2", "a0", "a1", "a2",
                      "memory", "cc");
    *intin = old;
}

/* ---- Glyphes des cadres ---- */

static void glyph_set(int c, int row, unsigned char bits)
{
    font[c * 16 + row] |= bits;
}

/* Trace une ligne horizontale de la colonne x0 a x1 (0..7). */
static void hline(int c, int row, int x0, int x1)
{
    int x;
    for (x = x0; x <= x1; x++) glyph_set(c, row, (unsigned char)(0x80 >> x));
}

static void vline(int c, int col, int y0, int y1)
{
    int y;
    for (y = y0; y <= y1; y++) glyph_set(c, y, (unsigned char)(0x80 >> col));
}

static void make_glyphs(void)
{
    int H = scr_cell_h, e = H - 1;
    /* Lignes doubles : rangees r1/r2 et colonnes c1/c2 ; simple au milieu. */
    int r1 = H / 2 - 2, r2 = H / 2, rs = H / 2 - 1;
    int c1 = 2, c2 = 4, cs = 3;
    int c, r;
    static const unsigned char check[8] = {
        0x00, 0x01, 0x03, 0x06, 0xcc, 0x78, 0x30, 0x00 };
    static const unsigned char updir[8] = {
        0x10, 0x38, 0x7c, 0xfe, 0x38, 0x38, 0x38, 0x00 };

    for (c = 0x0f; c <= 0x1f; c++) memset(font + c * 16, 0, 16);

    hline(G_DH, r1, 0, 7); hline(G_DH, r2, 0, 7);
    vline(G_DV, c1, 0, e); vline(G_DV, c2, 0, e);

    hline(G_DTL, r1, c1, 7); vline(G_DTL, c1, r1, e);
    hline(G_DTL, r2, c2, 7); vline(G_DTL, c2, r2, e);

    hline(G_DTR, r1, 0, c2); vline(G_DTR, c2, r1, e);
    hline(G_DTR, r2, 0, c1); vline(G_DTR, c1, r2, e);

    hline(G_DBL, r2, c1, 7); vline(G_DBL, c1, 0, r2);
    hline(G_DBL, r1, c2, 7); vline(G_DBL, c2, 0, r1);

    hline(G_DBR, r2, 0, c2); vline(G_DBR, c2, 0, r2);
    hline(G_DBR, r1, 0, c1); vline(G_DBR, c1, 0, r1);

    hline(G_DTS, r1, 0, 7); hline(G_DTS, r2, 0, 7); vline(G_DTS, cs, r2, e);
    hline(G_DBS, r1, 0, 7); hline(G_DBS, r2, 0, 7); vline(G_DBS, cs, 0, r1);
    vline(G_DLS, c1, 0, e); vline(G_DLS, c2, 0, e); hline(G_DLS, rs, c2, 7);
    vline(G_DRS, c1, 0, e); vline(G_DRS, c2, 0, e); hline(G_DRS, rs, 0, c1);

    vline(G_SV, cs, 0, e);
    hline(G_SH, rs, 0, 7);
    hline(G_SBT, rs, 0, 7); vline(G_SBT, cs, 0, rs);

    for (r = 0; r < H; r++) {
        int s = r * 8 / H;
        font[G_CHECK * 16 + r] = check[s];
        font[G_UPDIR * 16 + r] = updir[s];
        font[G_SHADE * 16 + r] = (r & 1) ? 0x55 : 0xaa;
        font[G_BLOCK * 16 + r] = 0xff;
    }
}

/* ---- Prise en main de l'ecran ---- */

const char *scr_init(void)
{
    short rez = Getrez(), i;

    if (rez == 2) {
        scr_mono = 1;
        scr_cell_h = 16;
    } else if (rez == 0 || rez == 1) {
        scr_mono = 0;
        scr_cell_h = 8;
    } else {
        return "TOS File Cmd needs ST low, medium or high resolution.\r\n";
    }
    linea_init();
    make_glyphs();
    linea_hide_mouse();
    Cursconf(0, 0);

    if (!scr_mono) {
        for (i = 0; i < 16; i++) old_pal[i] = Setcolor(i, -1);
        pal_saved = 1;
        if (rez == 0) {
            old_rez = 0;
            Setscreen(-1L, -1L, 1);
        }
        for (i = 0; i < 4; i++) (void)Setcolor(i, palette[i]);
    }
    vram = Logbase();
    make_masks();
    memset(scr_chr, ' ', sizeof scr_chr);
    memset(scr_att, A_NORMAL, sizeof scr_att);
    scr_invalidate();
    return 0;
}

void scr_exit(void)
{
    /* Ecran efface en couleur 0 : le bureau ou le dossier AUTO repartent
     * d'une page propre. */
    long i, n = 32000 / 4;
    unsigned long *p = (unsigned long *)vram;
    for (i = 0; i < n; i++) p[i] = 0;
    if (old_rez >= 0) Setscreen(-1L, -1L, old_rez);
    if (pal_saved) {
        Setpalette(old_pal);
        Vsync();
    }
    linea_show_mouse();
}

/* ---- Visionneuses ---- */

unsigned char *scr_graphics(int rez, const unsigned short *pal)
{
    if (!scr_mono) {
        /* Ecran noir pendant le changement : ni l'ancien texte dans les
         * nouvelles couleurs, ni l'image dans les anciennes. */
        static const short black[16];
        Setpalette(black);
        Vsync();
        Setscreen(-1L, -1L, rez);
        if (pal) Setpalette(pal);
    }
    return vram;
}

void scr_text(void)
{
    int i;
    if (!scr_mono) {
        Setscreen(-1L, -1L, 1);
        for (i = 0; i < 4; i++) (void)Setcolor(i, palette[i]);
        Vsync();
    }
    scr_invalidate();
}

/* ---- Cellules ---- */

void scr_puts(int x, int y, const char *s, int attr)
{
    unsigned char *c, *a;
    if ((unsigned)y >= SCR_ROWS || x < 0) return;
    c = scr_chr + y * SCR_COLS + x;
    a = scr_att + y * SCR_COLS + x;
    while (*s && x++ < SCR_COLS) {
        *c++ = (unsigned char)*s++;
        *a++ = (unsigned char)attr;
    }
    row_dirty[y] = 1;
}

void scr_field(int x, int y, int w, const char *s, int attr)
{
    unsigned char *c, *a;
    if ((unsigned)y >= SCR_ROWS || x < 0) return;
    if (x + w > SCR_COLS) w = SCR_COLS - x;
    c = scr_chr + y * SCR_COLS + x;
    a = scr_att + y * SCR_COLS + x;
    while (w-- > 0) {
        *c++ = *s ? (unsigned char)*s++ : ' ';
        *a++ = (unsigned char)attr;
    }
    row_dirty[y] = 1;
}

void scr_fill(int x, int y, int w, int ch, int attr)
{
    if ((unsigned)y >= SCR_ROWS || x < 0) return;
    if (x + w > SCR_COLS) w = SCR_COLS - x;
    if (w <= 0) return;
    memset(scr_chr + y * SCR_COLS + x, ch, w);
    memset(scr_att + y * SCR_COLS + x, attr, w);
    row_dirty[y] = 1;
}

void scr_attr(int x, int y, int w, int attr)
{
    int i;
    if (y < 0 || y >= SCR_ROWS) return;
    for (i = 0; i < w; i++)
        if (x + i >= 0 && x + i < SCR_COLS)
            scr_att[y * SCR_COLS + x + i] = (unsigned char)attr;
    row_dirty[y] = 1;
}

void scr_box(int x, int y, int w, int h, int attr)
{
    int i;
    scr_putc(x, y, G_DTL, attr);
    scr_fill(x + 1, y, w - 2, G_DH, attr);
    scr_putc(x + w - 1, y, G_DTR, attr);
    for (i = 1; i < h - 1; i++) {
        scr_putc(x, y + i, G_DV, attr);
        scr_fill(x + 1, y + i, w - 2, ' ', attr);
        scr_putc(x + w - 1, y + i, G_DV, attr);
    }
    scr_putc(x, y + h - 1, G_DBL, attr);
    scr_fill(x + 1, y + h - 1, w - 2, G_DH, attr);
    scr_putc(x + w - 1, y + h - 1, G_DBR, attr);
}

void scr_pointer(int x, int y)
{
    ptr_x = (short)x;
    ptr_y = (short)y;
}

void scr_touch(void)
{
    memset(row_dirty, 1, sizeof row_dirty);
}

void scr_invalidate(void)
{
    memset(drawn_att, 0xff, sizeof drawn_att);
    scr_touch();
}

/*
 * Une cellule. Pour chaque plan, octet = (glyphe & a) ^ b :
 *   texte 1, fond 0 : a = $FF, b = 0     (le glyphe)
 *   texte 0, fond 1 : a = $FF, b = $FF   (le glyphe inverse)
 *   texte 1, fond 1 : a = 0,   b = $FF   (plein)
 *   texte 0, fond 0 : a = 0,   b = 0     (vide)
 * Les masques sont calcules une fois par attribut (et par inversion sous le
 * pointeur), les lignes de pixels sont deroulees : un ecran complet se
 * redessine en quelques trames a 8 MHz.
 */
static unsigned char masks[2][A_COUNT][4];
static unsigned long row_addr[SCR_ROWS];

static void make_masks(void)
{
    int inv, at, y;
    for (inv = 0; inv < 2; inv++)
        for (at = 0; at < A_COUNT; at++) {
            unsigned char col = att_color[at];
            int fg = col & 0x0f, bg = col >> 4;
            unsigned char *m = masks[inv][at];
            if (inv) { int t = fg; fg = bg; bg = t; }
            if (scr_mono) {
                m[0] = (att_mono_rev[at] ^ inv) ? 0xff : 0;
                continue;
            }
            m[0] = ((fg ^ bg) & 1) ? 0xff : 0;
            m[1] = (bg & 1) ? 0xff : 0;
            m[2] = ((fg ^ bg) & 2) ? 0xff : 0;
            m[3] = (bg & 2) ? 0xff : 0;
        }
    for (y = 0; y < SCR_ROWS; y++)
        row_addr[y] = (unsigned long)vram + (unsigned long)y * (scr_mono ? 16 * 80 : 8 * 160);
}

static void draw_cell(int x, int y, unsigned char ch, unsigned char at)
{
    const unsigned char *g = font + ((unsigned)ch << 4);
    const unsigned char *m = masks[at >> 7][at & 0x7f];

    if (scr_mono) {
        unsigned char *p = (unsigned char *)row_addr[y] + x;
        unsigned char i = m[0];
        p[0] = g[0] ^ i;     p[80] = g[1] ^ i;    p[160] = g[2] ^ i;   p[240] = g[3] ^ i;
        p[320] = g[4] ^ i;   p[400] = g[5] ^ i;   p[480] = g[6] ^ i;   p[560] = g[7] ^ i;
        p[640] = g[8] ^ i;   p[720] = g[9] ^ i;   p[800] = g[10] ^ i;  p[880] = g[11] ^ i;
        p[960] = g[12] ^ i;  p[1040] = g[13] ^ i; p[1120] = g[14] ^ i; p[1200] = g[15] ^ i;
    } else {
        unsigned char *p = (unsigned char *)row_addr[y] + ((x >> 1) << 2) + (x & 1);
        unsigned char a0 = m[0], b0 = m[1], a1 = m[2], b1 = m[3];
#define ROW(r) p[(r) * 160] = (g[r] & a0) ^ b0; p[(r) * 160 + 2] = (g[r] & a1) ^ b1
        ROW(0); ROW(1); ROW(2); ROW(3); ROW(4); ROW(5); ROW(6); ROW(7);
#undef ROW
    }
}

void scr_flush(void)
{
    int x, y;
    if (ptr_x != drawn_ptr_x || ptr_y != drawn_ptr_y) {
        if (drawn_ptr_y >= 0) row_dirty[drawn_ptr_y] = 1;
        if (ptr_y >= 0) row_dirty[ptr_y] = 1;
        drawn_ptr_x = ptr_x;
        drawn_ptr_y = ptr_y;
    }
    for (y = 0; y < SCR_ROWS; y++) {
        const unsigned char *c, *a;
        unsigned char *dc, *da;
        if (!row_dirty[y]) continue;
        row_dirty[y] = 0;
        c = scr_chr + y * SCR_COLS;
        a = scr_att + y * SCR_COLS;
        dc = drawn_chr + y * SCR_COLS;
        da = drawn_att + y * SCR_COLS;
        /* Quatre cellules a la fois ; la cellule du pointeur porte le bit 7
         * dans drawn_att, elle est donc toujours revue. */
        for (x = 0; x < SCR_COLS; x += 4) {
            int k;
            if (*(const unsigned long *)(c + x) == *(const unsigned long *)(dc + x) &&
                *(const unsigned long *)(a + x) == *(const unsigned long *)(da + x) &&
                (y != ptr_y || ptr_x < x || ptr_x >= x + 4))
                continue;
            for (k = x; k < x + 4; k++) {
                unsigned char at = a[k];
                if (y == ptr_y && k == ptr_x) at |= 0x80;
                if (dc[k] != c[k] || da[k] != at) {
                    draw_cell(k, y, c[k], at);
                    dc[k] = c[k];
                    da[k] = at;
                }
            }
        }
    }
}
