/*
 * edit.c -- l'editeur de texte de TOSFC.
 *
 * On n'edite que ce que l'on peut reecrire sans rien perdre : le fichier
 * doit tenir en entier en memoire (jamais d'edition d'un debut de fichier),
 * etre lu sans erreur, ne contenir ni octet nul ni fins de ligne melangees,
 * ne pas etre un document 1st Word (ses codes seraient abimes) ni en lecture
 * seule. L'enregistrement passe par ops_save_stream : temporaire relu,
 * original garde jusqu'au bout.
 *
 * Pas de coupure des lignes : une ligne longue defile horizontalement.
 */
#include "edit.h"
#include "edbuf.h"
#include "screen.h"
#include "input.h"
#include "ui.h"
#include "fsops.h"
#include "textview.h"
#include "tos.h"
#include "libc.h"

#define ROWS 23                 /* lignes 1 a 23 */
#define MEM_RESERVE 49152L      /* systeme + tampons d'enregistrement */
#define SAVE_HALF 8192L
#define GROW 32768L             /* place pour taper, au-dela du fichier */

typedef struct {
    EDBUF b;
    long cur;                   /* position du curseur */
    long top;                   /* debut de la premiere ligne affichee */
    int left;                   /* premiere colonne affichee */
    int want_col;               /* colonne visee en montant/descendant */
    long cache_pos, cache_line; /* numero de ligne, calcule par ecart */
    char name[14];
    const char *dir;
    int is_new;
} ED;

static long line_of(ED *e, long pos)
{
    long i;
    if (pos >= e->cache_pos) {
        for (i = e->cache_pos; i < pos; i++)
            if (eb_at(&e->b, i) == '\n') e->cache_line++;
    } else {
        for (i = pos; i < e->cache_pos; i++)
            if (eb_at(&e->b, i) == '\n') e->cache_line--;
    }
    e->cache_pos = pos;
    return e->cache_line;
}

/* Toute modification avant le repere du cache le rend faux. */
static void cache_reset(ED *e) { e->cache_pos = 0; e->cache_line = 1; }

static long gen(void *ctx, long at, char *out, long cap)
{
    return eb_export(&((ED *)ctx)->b, at, out, cap);
}

static void draw(ED *e, const char *status)
{
    char title[100], num[16];
    long p = e->top;
    int y, col;

    for (y = 1; y <= ROWS; y++) {
        char line[SCR_COLS];
        int c = 0, x;
        memset(line, ' ', SCR_COLS);
        if (p >= 0) {
            long q = p;
            for (;;) {
                int ch = eb_at(&e->b, q);
                if (ch < 0 || ch == '\n') break;
                if (ch == '\t') {
                    int w = 8 - (c & 7), k;
                    for (k = 0; k < w; k++, c++)
                        if (c - e->left >= 0 && c - e->left < SCR_COLS) line[c - e->left] = ' ';
                } else {
                    if (c - e->left >= 0 && c - e->left < SCR_COLS)
                        line[c - e->left] = (ch < 32 || ch == 127) ? '.' : (char)ch;
                    c++;
                }
                if (c - e->left >= SCR_COLS) break;
                q++;
            }
            p = eb_next_line(&e->b, p);
        }
        for (x = 0; x < SCR_COLS; x++) scr_putc(x, y, (unsigned char)line[x], A_NORMAL);
    }

    str_copy(title, " ", sizeof title);
    str_add(title, e->name, sizeof title);
    if (e->b.modified) str_add(title, " *", sizeof title);
    str_add(title, "   line ", sizeof title);
    fmt_ulong(num, (unsigned long)line_of(e, e->cur), 0);
    str_add(title, num, sizeof title);
    str_add(title, "  col ", sizeof title);
    col = eb_col(&e->b, e->cur);
    fmt_ulong(num, (unsigned long)(col + 1), 0);
    str_add(title, num, sizeof title);
    str_add(title, "   ", sizeof title);
    fmt_ulong(num, (unsigned long)eb_file_size(&e->b), ',');
    str_add(title, num, sizeof title);
    str_add(title, e->b.eol == EOL_CRLF ? " bytes CRLF" : e->b.eol == EOL_LF ? " bytes LF" : " bytes CR",
            sizeof title);
    scr_field(0, 0, SCR_COLS, title, A_BARTXT);
    if (status && status[0]) scr_field(0, 24, SCR_COLS, status, A_TITLE);
    else scr_field(0, 24, SCR_COLS,
                   " Arrows move  Shift+arrows page/line ends  Ctrl+Y cut line  F10 save  ESC quit",
                   A_BARKEY);

    /* Curseur : la cellule sous le point d'insertion, en inverse. */
    {
        int cy = 1, cx = col - e->left;
        long q = e->top;
        while (q >= 0 && q < eb_line_start(&e->b, e->cur)) {
            q = eb_next_line(&e->b, q);
            cy++;
        }
        if (cy >= 1 && cy <= ROWS && cx >= 0 && cx < SCR_COLS)
            scr_attr(cx, cy, 1, A_CURSOR);
    }
}

/* Garde le curseur visible : premiere ligne et premiere colonne. Un saut
 * lointain (fin du texte, page) remet la ligne du curseur en bas de l'ecran. */
static void follow(ED *e)
{
    long ls = eb_line_start(&e->b, e->cur), q;
    int col = eb_col(&e->b, e->cur), n = 0;

    if (ls < e->top) {
        e->top = ls;
    } else {
        for (q = e->top; q >= 0 && q < ls && n < ROWS; q = eb_next_line(&e->b, q)) n++;
        if (q != ls || n >= ROWS) {
            long t = ls;
            int k;
            for (k = 0; k < ROWS - 1; k++) {
                long pl = eb_prev_line(&e->b, t);
                if (pl < 0) break;
                t = pl;
            }
            e->top = t;
        }
    }
    if (col < e->left) e->left = col & ~7;
    if (col >= e->left + SCR_COLS) e->left = ((col - SCR_COLS + 8) + 7) & ~7;
}

static void move_vert(ED *e, int lines)
{
    long ls = eb_line_start(&e->b, e->cur);
    while (lines < 0) {
        long p = eb_prev_line(&e->b, ls);
        if (p < 0) break;
        ls = p;
        lines++;
    }
    while (lines > 0) {
        long n = eb_next_line(&e->b, ls);
        if (n < 0) break;
        ls = n;
        lines--;
    }
    e->cur = eb_pos_at_col(&e->b, ls, e->want_col);
}

static int save(ED *e, char *work)
{
    long r = ops_save_stream(e->dir, e->name, e->is_new, gen, e, work, SAVE_HALF);
    if (r) {
        ui_error(r == TE_RESTORE ? "Saved, but TOSFC.BAK is left here:"
                                 : "Not saved. The file on disk is unchanged:",
                 e->name, r);
        return r == TE_RESTORE;
    }
    e->b.modified = 0;
    e->is_new = 0;
    return 1;
}

int edit_file(PANEL *p)
{
    FINFO *f = panel_current(p);
    ED e;
    char path[PATH_MAX_TOSFC], status[80];
    char *mem = 0, *work = 0;
    long avail, h, n = 0, size = 0, r, memcap;
    int done = 0, saved = 0, i;
    EVENT ev;
    static char dummy;

    if (!f || panel_is_drives(p)) return 0;
    memset(&e, 0, sizeof e);
    e.dir = p->path;
    status[0] = 0;

    if ((f->attr & FA_DIR)) {
        /* Nouveau fichier dans le dossier du panneau. */
        char nn[13];
        e.name[0] = 0;
        if (!ui_input("New file", "Name of the new text file:", e.name, 13) || !e.name[0])
            return 0;
        if (name_normalize(e.name, nn)) { ui_error("Cannot create", e.name, TE_BADNAME); return 0; }
        str_copy(e.name, nn, sizeof e.name);
        e.is_new = 1;
    } else {
        str_copy(e.name, f->name, sizeof e.name);
        if (f->attr & FA_RDONLY) {
            ui_message("Edit", "This file is read-only.", "Clear the attribute first (A).");
            return 0;
        }
    }
    if (path_join(path, p->path, e.name, 0)) return 0;

    work = sys_alloc(2 * SAVE_HALF);
    avail = sys_avail() - MEM_RESERVE;
    if (!work || avail < 16384L) {
        sys_free(work);
        ui_error("Cannot edit", e.name, ENSMEM);
        return 0;
    }
    memcap = avail;
    if (!e.is_new && memcap > (long)f->size * 2 + GROW) memcap = (long)f->size * 2 + GROW;
    if (e.is_new) memcap = GROW;
    memcap &= ~1L;
    mem = sys_alloc(memcap);
    if (!mem) { sys_free(work); ui_error("Cannot edit", e.name, ENSMEM); return 0; }

    if (!e.is_new) {
        /* Tout le fichier, sans erreur, dans la moitie haute du tampon ; le
         * surplus prouve qu'il tient. */
        long room = memcap / 2;
        h = sys_open(path, 0);
        if (h < 0) r = h;
        else {
            r = 0;
            for (;;) {
                n = sys_read((int)h, room - size > 32768L ? 32768L : room - size,
                             mem + memcap - room + size);
                if (n < 0) { r = n; break; }
                if (n == 0) break;
                size += n;
                if (size >= room) { r = ENSMEM; break; }
            }
            sys_close((int)h);
        }
        if (r) {
            ui_error(r == ENSMEM ? "Too large to edit in the free memory:" : "Cannot read:",
                     e.name, r);
            goto out;
        }
        if (tv_is_firstword(e.name, (unsigned char *)mem + memcap - room, size)) {
            ui_message("Edit", "1st Word documents are not edited here:",
                       "their formatting codes would be damaged.");
            goto out;
        }
        /* Le texte est lu en haut ; eb_load le recopie en bas puis a la fin. */
        memmove(mem, mem + memcap - room, size);
        r = eb_load(&e.b, (unsigned char *)mem, size, mem, memcap);
        if (r == EB_BINARY || r == EB_MIXED) {
            ui_message("Edit", r == EB_BINARY ? "This file contains zero bytes: it is"
                                              : "Its line ends are mixed: editing would",
                       r == EB_BINARY ? "not a text file." : "change bytes you did not touch.");
            goto out;
        }
        if (r) { ui_error("Cannot edit", e.name, ENSMEM); goto out; }
    } else {
        eb_load(&e.b, (unsigned char *)&dummy, 0, mem, memcap);
    }
    cache_reset(&e);

    while (!done) {
        follow(&e);
        draw(&e, status);
        status[0] = 0;
        in_wait(&ev);
        if (ev.type != EV_KEY) continue;
        if (ev.scan == SC_ESC || ev.scan == SC_UNDO) {
            if (e.b.modified) {
                static const char *const bt[] = { "Save", "Discard", "Cancel" };
                const char *lines[1];
                int a;
                lines[0] = "The text was changed.";
                a = ui_dialog(e.name, lines, 1, bt, 3, 0);
                if (a == 0) { if (save(&e, work)) { saved = 1; done = 1; } }
                else if (a == 1) done = 1;
            } else {
                done = 1;
            }
            continue;
        }
        if (ev.scan == SC_F10 || ev.ascii == 0x13) {           /* F10, Ctrl+S */
            if (save(&e, work)) { saved = 1; str_copy(status, " Saved.", sizeof status); }
            continue;
        }
        switch (ev.scan) {
        case SC_LEFT:
            if (ev.shift & SHIFT_ANY) e.cur = eb_line_start(&e.b, e.cur);
            else if (e.cur > 0) e.cur--;
            e.want_col = eb_col(&e.b, e.cur);
            continue;
        case SC_RIGHT:
            if (ev.shift & SHIFT_ANY) e.cur = eb_line_end(&e.b, e.cur);
            else if (e.cur < eb_len(&e.b)) e.cur++;
            e.want_col = eb_col(&e.b, e.cur);
            continue;
        case SC_UP:
            move_vert(&e, (ev.shift & SHIFT_ANY) ? -(ROWS - 1) : -1);
            continue;
        case SC_DOWN:
            move_vert(&e, (ev.shift & SHIFT_ANY) ? ROWS - 1 : 1);
            continue;
        case SC_HOME:
            e.cur = (ev.shift & SHIFT_ANY) ? eb_len(&e.b) : 0;
            e.want_col = eb_col(&e.b, e.cur);
            continue;
        case SC_BACKSP:
            if (e.cur > 0) {
                if (e.cur <= e.cache_pos) cache_reset(&e);
                eb_delete(&e.b, e.cur - 1, 1);
                e.cur--;
                if (e.top > e.cur) e.top = eb_line_start(&e.b, e.cur);
            }
            e.want_col = eb_col(&e.b, e.cur);
            continue;
        case SC_DELETE:
            if (e.cur < e.cache_pos) cache_reset(&e);
            eb_delete(&e.b, e.cur, 1);
            continue;
        case SC_RETURN: case SC_ENTER:
            if (e.cur < e.cache_pos) cache_reset(&e);
            if (eb_insert(&e.b, e.cur, "\n", 1)) str_copy(status, " The text is full.", sizeof status);
            else e.cur++;
            e.want_col = 0;
            continue;
        }
        if (ev.ascii == 0x19) {                                 /* Ctrl+Y : couper la ligne */
            long s = eb_line_start(&e.b, e.cur), t = eb_next_line(&e.b, e.cur);
            if (t < 0) t = eb_len(&e.b);
            if (s < e.cache_pos) cache_reset(&e);
            eb_delete(&e.b, s, t - s);
            e.cur = s;
            if (e.top > s) e.top = eb_line_start(&e.b, s);
            e.cur = eb_pos_at_col(&e.b, s, e.want_col);
            continue;
        }
        if (ev.ascii == '\t' || (ev.ascii >= 32 && ev.ascii != 127)) {
            char c = (char)ev.ascii;
            if (e.cur < e.cache_pos) cache_reset(&e);
            if (eb_insert(&e.b, e.cur, &c, 1)) str_copy(status, " The text is full.", sizeof status);
            else e.cur++;
            e.want_col = eb_col(&e.b, e.cur);
        }
        (void)i;
    }
out:
    sys_free(mem);
    sys_free(work);
    scr_invalidate();
    return saved;
}
