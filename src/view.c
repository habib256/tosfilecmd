/*
 * view.c -- les visionneuses de TOSFC.
 *
 * Le fichier est lu en entier en memoire (tant qu'il tient : au-dela, on
 * montre le debut et on le dit). Une erreur de lecture n'est pas prise pour
 * une fin de fichier : elle est signalee, et l'on montre ce qui a ete lu.
 */
#include "view.h"
#include "screen.h"
#include "input.h"
#include "ui.h"
#include "fsops.h"
#include "picture.h"
#include "textview.h"
#include "tos.h"
#include "libc.h"

#define TEXT_ROWS 23            /* lignes 1 a 23 ; 0 = titre, 24 = aide */
#define MEM_RESERVE 32768L      /* laisse au systeme */

typedef struct {
    unsigned char *data;
    long size;
    int truncated;
    long err;
} LOADED;

/* Lit path en entier, au plus max octets. */
static void load_file(const char *path, long max, LOADED *l)
{
    long h, n, cap = sys_avail() - MEM_RESERVE;
    const SOURCE *s;

    l->data = 0;
    l->size = 0;
    l->truncated = 0;
    l->err = 0;
    if (cap > max) cap = max;
    if (cap < 1024) { l->err = ENSMEM; return; }
    cap &= ~1L;
    l->data = sys_alloc(cap + 2);
    if (!l->data) { l->err = ENSMEM; return; }
    s = src_of(path);                   /* fichier d'une image ou d'une archive ? */
    h = s->open(s->ctx, path);
    if (h < 0) { l->err = h; return; }
    while (l->size < cap) {
        long want = cap - l->size;
        if (want > 32768L) want = 32768L;
        n = s->read(s->ctx, h, want, l->data + l->size);
        if (n < 0) { l->err = n; break; }
        if (n == 0) break;
        l->size += n;
    }
    if (!l->err && l->size == cap) {
        /* Un octet de plus ? Alors le fichier ne tient pas. */
        unsigned char probe[2];
        n = s->read(s->ctx, h, 1, probe);
        if (n > 0) l->truncated = 1;
        else if (n < 0) l->err = n;
    }
    s->close(s->ctx, h);
}

static void unload(LOADED *l)
{
    sys_free(l->data);
    l->data = 0;
}

/* ---- Texte ---- */

static void text_draw(const TEXTDOC *d, const char *name, long top, const LOADED *l,
                      const char *status)
{
    char line[TV_COLS + 1], num[16], title[96];
    long off = top;
    int y;
    unsigned long pct;

    for (y = 1; y <= TEXT_ROWS; y++) {
        if (off < d->size || (y == 1 && d->size == 0)) {
            long next = tv_layout(d, off, line);
            scr_field(0, y, TV_COLS, line, A_NORMAL);
            off = next;
        } else {
            scr_fill(0, y, SCR_COLS, ' ', A_NORMAL);
        }
    }

    str_copy(title, " ", sizeof title);
    str_add(title, name, sizeof title);
    str_add(title, "   ", sizeof title);
    fmt_ulong(num, (unsigned long)l->size, ',');
    str_add(title, num, sizeof title);
    str_add(title, " bytes", sizeof title);
    if (l->truncated) str_add(title, " shown (file is larger)", sizeof title);
    if (d->firstword) str_add(title, "   1st Word", sizeof title);
    if (d->hex) str_add(title, "   hex", sizeof title);
    scr_field(0, 0, SCR_COLS, title, A_BARTXT);
    /* Position : pourcentage du debut de page, "End" si la fin est visible. */
    if (off >= d->size) str_copy(num, "End ", sizeof num);
    else {
        pct = d->size ? (unsigned long)((top >> 8) * 100 / ((d->size >> 8) + 1)) : 100;
        fmt_ulong(num, pct, 0);
        str_add(num, "% ", sizeof num);
    }
    scr_puts(SCR_COLS - (int)strlen(num), 0, num, A_BARTXT);

    if (status && status[0]) scr_field(0, 24, SCR_COLS, status, A_TITLE);
    else scr_field(0, 24, SCR_COLS,
                   " Up/Down line  Left/Right page  Home top/end  F find  N next  H hex  ESC back",
                   A_BARKEY);
}

void view_text(PANEL *p, int hex)
{
    FINFO *f = panel_current(p);
    char path[PATH_MAX_TOSFC], needle[32], status[80];
    LOADED l;
    TEXTDOC d;
    long top = 0, r;
    long anchor = -1, anchor_hex = -1;  /* texte -> hexa -> texte sans bouger */
    int i, done = 0;
    EVENT e;

    if (!f || (f->attr & FA_DIR) || panel_is_drives(p)) return;
    if (path_join(path, p->path, f->name, 0)) return;
    /* La taille du panneau peut etre perimee : de la marge, et load_file
     * dit si le fichier depasse. */
    load_file(path, (long)f->size + 16384L, &l);
    if (!l.data || (l.err && l.size == 0)) {
        ui_error("Cannot read", path, l.err ? l.err : ENSMEM);
        unload(&l);
        return;
    }
    d.data = l.data;
    d.size = l.size;
    d.firstword = !hex && tv_is_firstword(f->name, l.data, l.size);
    d.hex = hex;
    needle[0] = 0;
    status[0] = 0;
    if (l.err) {
        /* Ce qui a ete lu avant l'erreur, et l'erreur elle-meme. */
        str_copy(status, " Read error: ", sizeof status);
        str_add(status, err_text(l.err), sizeof status);
        str_add(status, " -- only the start of the file is shown", sizeof status);
    }

    while (!done) {
        text_draw(&d, f->name, top, &l, status);
        status[0] = 0;
        in_wait(&e);
        if (e.type == EV_RCLICK) break;
        if (e.type == EV_CLICK) {
            /* Clic en haut : page precedente ; en bas : page suivante. */
            if (e.y < 12) e.scan = SC_LEFT;
            else e.scan = SC_RIGHT;
            e.type = EV_KEY;
            e.ascii = 0;
        }
        if (e.type != EV_KEY) continue;
        switch (e.scan) {
        case SC_ESC: case SC_UNDO: case SC_BACKSP: done = 1; continue;
        case SC_UP:
            top = tv_prev(&d, top);
            continue;
        case SC_DOWN: {
            long n = tv_layout(&d, top, 0);
            if (n < d.size) top = n;
            continue;
        }
        case SC_LEFT:
            for (i = 0; i < TEXT_ROWS - 1; i++) top = tv_prev(&d, top);
            continue;
        case SC_RIGHT: {
            long off = top;
            for (i = 0; i < TEXT_ROWS - 1 && off < d.size; i++) {
                long n = tv_layout(&d, off, 0);
                if (n >= d.size) break;
                off = n;
            }
            top = off;
            continue;
        }
        case SC_HOME:
            if (e.shift & SHIFT_ANY) {
                /* Derniere page : la fin, puis TEXT_ROWS-1 lignes en arriere. */
                top = tv_line_of(&d, d.size > 0 ? d.size - 1 : 0);
                for (i = 0; i < TEXT_ROWS - 1; i++) top = tv_prev(&d, top);
            } else {
                top = 0;
            }
            continue;
        }
        switch (toupper(e.ascii)) {
        case 'Q': done = 1; break;
        case ' ': {
            long off = top;
            for (i = 0; i < TEXT_ROWS - 1 && off < d.size; i++) {
                long n = tv_layout(&d, off, 0);
                if (n >= d.size) break;
                off = n;
            }
            top = off;
            break;
        }
        case 'H': {
            /* Bascule texte / hexa en gardant la position. Une ligne d'hexa
             * commence souvent au milieu d'une ligne de texte : si l'on n'a
             * pas bouge en hexa, on retrouve exactement la ligne de depart. */
            long pos = top;
            d.hex = !d.hex;
            d.firstword = !d.hex && tv_is_firstword(f->name, l.data, l.size);
            if (d.hex) {
                anchor = top;
                top = tv_line_of(&d, pos);
                anchor_hex = top;
            } else {
                top = (pos == anchor_hex && anchor >= 0) ? anchor : tv_line_of(&d, pos);
            }
            break;
        }
        case 'F':
            if (!ui_input("Find", "Text to find (any case):", needle, sizeof needle))
                break;
            /* fall through */
        case 'N': {
            long from;
            if (!needle[0]) break;
            /* Apres la premiere ligne affichee, pour que N avance. */
            from = tv_layout(&d, top, 0);
            if (toupper(e.ascii) == 'F') from = top;
            r = tv_find(&d, from, needle);
            if (r < 0) r = tv_find(&d, 0, needle);   /* on reprend au debut */
            if (r < 0) {
                str_copy(status, " Not found: ", sizeof status);
                str_add(status, needle, sizeof status);
            } else {
                top = tv_line_of(&d, r);
            }
            break;
        }
        }
    }
    unload(&l);
    scr_invalidate();
}

/* ---- Images ---- */

/* Spectrum 512 : la routine de spec512.S dans la file VBL, l'ecran a 50 Hz. */
extern void spec_vbl(void);
extern unsigned short *spec_pal;
static int spec_slot = -1;
static unsigned char spec_sync;

static long spec_on(void)
{
    short n = *(volatile short *)0x454, i;
    void (**q)(void) = *(void (***)(void))0x456;
    for (i = 0; i < n; i++)
        if (!q[i]) {
            spec_sync = *(volatile unsigned char *)0xffff820aL;
            *(volatile unsigned char *)0xffff820aL = (unsigned char)(spec_sync | 2);
            spec_slot = i;
            q[i] = spec_vbl;
            return 0;
        }
    return -1;
}

static long spec_off(void)
{
    void (**q)(void) = *(void (***)(void))0x456;
    if (spec_slot >= 0) {
        q[spec_slot] = 0;
        *(volatile unsigned char *)0xffff820aL = spec_sync;
        spec_slot = -1;
    }
    return 0;
}

static void spec_stop(void)
{
    static const char mouse_on[] = { 0x08 };           /* souris relative */
    if (spec_slot < 0) return;
    Supexec(spec_off);
    Vsync();
    Ikbdws(0, mouse_on);
}

/* Palettes dans l'ordre d'ecriture de la routine : pour chaque ligne,
 * jeux 2 et 3 de la ligne puis jeu 1 de la suivante (ligne 0 : noire). */
static void spec_order(const unsigned short *spal, unsigned short *out)
{
    int y;
    memset(out, 0, 200 * 96);
    memcpy(out + 32, spal, 32);                          /* jeu 1 de la ligne 1 */
    for (y = 1; y < 200; y++) {
        unsigned short *o = out + y * 48;
        const unsigned short *p = spal + (y - 1) * 48;
        memcpy(o, p + 16, 64);
        if (y < 199) memcpy(o + 32, p + 48, 32);
    }
}

static long show_spectrum(const char *path, const char *name, unsigned char *bm,
                          unsigned char *conv, unsigned short *spal, unsigned short *order)
{
    static const unsigned short black[16];
    static const char mouse_off[] = { 0x12 };
    LOADED l;
    int r;
    unsigned char *screen;

    load_file(path, 60000L, &l);
    if (!l.data || l.err) {
        long e = l.err ? l.err : ENSMEM;
        unload(&l);
        return e;
    }
    r = pic_decode_spectrum(name, l.data, l.size, bm, spal);
    unload(&l);
    if (r != PIC_OK) return r == PIC_BAD ? TE_BADPIC : TE_NOTPIC;
    if (scr_mono) {
        pic_spectrum_to_mono(bm, spal, conv);
        screen = scr_graphics(2, 0);
        memcpy(screen, conv, PIC_BYTES);
        return 0;
    }
    spec_order(spal, order);
    spec_pal = order;
    screen = scr_graphics(0, black);
    memcpy(screen, bm, PIC_BYTES);
    /* La routine masque les interruptions presque toute la trame : des
     * paquets souris perdus desynchroniseraient le TOS. */
    Ikbdws(0, mouse_off);
    if (Supexec(spec_on) < 0) {
        static const char mouse_on[] = { 0x08 };
        Ikbdws(0, mouse_on);
        return ENSMEM;
    }
    Vsync();
    return 0;
}

/* Affiche l'image de nom name (dans dir) ; 0, ou l'erreur a signaler. */
static unsigned short *spal_buf, *order_buf;

static long show_picture(const char *dir, const char *name, unsigned char *bm,
                         unsigned char *conv)
{
    char path[PATH_MAX_TOSFC];
    LOADED l;
    PICINFO info;
    int r;
    unsigned char *screen;

    spec_stop();
    if (path_join(path, dir, name, 0)) return TE_TOOLONG;
    if (pic_is_spectrum_name(name))
        return show_spectrum(path, name, bm, conv, spal_buf, order_buf);
    /* Degas + animation Elite : 32066 octets ; NEOchrome : 32128. */
    load_file(path, 40000L, &l);
    if (!l.data || l.err) {
        long e = l.err ? l.err : ENSMEM;
        unload(&l);
        return e;
    }
    r = pic_decode(name, l.data, l.size, &info, bm);
    unload(&l);
    if (r != PIC_OK) return r == PIC_BAD ? TE_BADPIC : TE_NOTPIC;

    if (scr_mono) {
        const unsigned char *src = bm;
        if (info.res != PIC_HIGH) {
            pic_to_mono(&info, bm, conv);
            src = conv;
        }
        screen = scr_graphics(2, 0);
        memcpy(screen, src, PIC_BYTES);
    } else if (info.res == PIC_HIGH) {
        pic_mono_to_medium(bm, conv);
        screen = scr_graphics(1, 0);
        memcpy(screen, conv, PIC_BYTES);
        {
            unsigned short pal[16];
            int i;
            for (i = 0; i < 16; i++) pal[i] = pic_gray_pal[i & 3];
            Setpalette(pal);
        }
    } else {
        screen = scr_graphics(info.res, 0);
        memcpy(screen, bm, PIC_BYTES);
        Setpalette(info.pal);
    }
    Vsync();
    return 0;
}

/* Attente sans redessiner l'ecran texte par-dessus l'image. */
static void picture_wait(EVENT *e)
{
    for (;;) {
        in_poll(e);
        if (e->type != EV_NONE) return;
        Vsync();
    }
}

static int next_picture(PANEL *p, int from, int dir)
{
    int i;
    for (i = from + dir; i >= 0 && i < p->n; i += dir)
        if (!(p->ent[i].attr & FA_DIR) && pic_is_picture_name(p->ent[i].name))
            return i;
    return -1;
}

void view_picture(PANEL *p)
{
    FINFO *f = panel_current(p);
    unsigned char *bm, *conv;
    long r;
    int done = 0, shown;
    EVENT e;
    char name[14];

    if (!f || (f->attr & FA_DIR) || panel_is_drives(p)) return;
    bm = sys_alloc(2 * PIC_BYTES + 2L * SPEC_PAL_WORDS + 200L * 96);
    if (!bm) { ui_error("Cannot show", f->name, ENSMEM); return; }
    conv = bm + PIC_BYTES;
    spal_buf = (unsigned short *)(conv + PIC_BYTES);
    order_buf = spal_buf + SPEC_PAL_WORDS;

    str_copy(name, f->name, sizeof name);
    r = show_picture(p->path, name, bm, conv);
    if (r) {
        spec_stop();
        scr_text();
        ui_error("Cannot show this picture", name, r);
        sys_free(bm);
        return;
    }
    shown = p->cur;
    while (!done) {
        int k;
        picture_wait(&e);
        k = 0;
        if (e.type == EV_CLICK) k = 1;          /* image suivante */
        else if (e.type == EV_RCLICK) done = 1;
        else if (e.type == EV_KEY) {
            if (e.scan == SC_RIGHT || e.scan == SC_DOWN || e.ascii == ' ') k = 1;
            else if (e.scan == SC_LEFT || e.scan == SC_UP || e.scan == SC_BACKSP) k = -1;
            else done = 1;                      /* ESC, RETURN, Q... */
        }
        if (k) {
            /* Les fichiers qui ne se decodent pas sont passes. */
            int i = shown;
            for (;;) {
                i = next_picture(p, i, k);
                if (i < 0) break;
                str_copy(name, p->ent[i].name, sizeof name);
                if (show_picture(p->path, name, bm, conv) == 0) {
                    shown = i;
                    break;
                }
            }
        }
    }
    spec_stop();
    p->cur = shown;
    panel_move(p, 0);
    sys_free(bm);
    scr_text();
}
