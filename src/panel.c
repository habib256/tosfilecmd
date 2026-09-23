/*
 * panel.c -- lecture, tri et affichage d'un panneau.
 *
 * Colonnes (40 caracteres, cadre compris) :
 *   ║m NOM     EXT│     Taille│  Date  │Heure║
 *   x 1 2      13 14  15..23  24 25..32 33 34..38 39
 */
#include "panel.h"
#include "screen.h"
#include "fsops.h"
#include "tos.h"
#include "libc.h"

int opt_show_hidden = 1;

#define COL_NAME 1
#define SEP1 14
#define COL_SIZE 15
#define SEP2 24
#define COL_DATE 25
#define SEP3 33
#define COL_TIME 34

/* ---- Formats ---- */

void fmt_name83(char *out, const char *name)
{
    const char *dot = strchr(name, '.');
    int bl = dot ? (int)(dot - name) : (int)strlen(name), i;
    for (i = 0; i < 8; i++) out[i] = i < bl ? name[i] : ' ';
    for (i = 8; i < 12; i++) out[i] = ' ';
    if (dot)
        for (i = 0; i < 3 && dot[1 + i]; i++) out[9 + i] = dot[1 + i];
    out[12] = 0;
}

static void two(char *o, int v)
{
    char t = '0';
    while (v >= 100) v -= 100;
    while (v >= 10) { v -= 10; t++; }
    o[0] = t;
    o[1] = (char)('0' + v);
}

void fmt_date(char *out, unsigned short d)
{
    two(out, d & 31);
    out[2] = '/';
    two(out + 3, (d >> 5) & 15);
    out[5] = '/';
    two(out + 6, (d >> 9) + 80);
    out[8] = 0;
}

void fmt_time(char *out, unsigned short t)
{
    two(out, (t >> 11) & 31);
    out[2] = ':';
    two(out + 3, (t >> 5) & 63);
    out[5] = 0;
}

/* ---- Tri ---- */

static int cmp_base(const char *a, const char *b)
{
    while (*a && *a != '.' && *a == *b) { a++; b++; }
    {
        int ca = (*a == '.') ? 0 : (unsigned char)*a;
        int cb = (*b == '.') ? 0 : (unsigned char)*b;
        return ca - cb;
    }
}

static const char *ext_of(const char *n)
{
    const char *d = strchr(n, '.');
    return d ? d + 1 : "";
}

static int entry_cmp(const FINFO *a, const FINFO *b, int mode)
{
    int da = (a->attr & FA_DIR) != 0, db = (b->attr & FA_DIR) != 0, r;
    if ((a->pad & PF_UP) != (b->pad & PF_UP)) return (a->pad & PF_UP) ? -1 : 1;
    if (da != db) return da ? -1 : 1;
    switch (mode) {
    case SORT_EXT:
        r = strcmp(ext_of(a->name), ext_of(b->name));
        if (r) return r;
        break;
    case SORT_SIZE:
        if (a->size != b->size) return a->size > b->size ? -1 : 1;
        break;
    case SORT_DATE:
        if (a->date != b->date) return a->date > b->date ? -1 : 1;
        if (a->time != b->time) return a->time > b->time ? -1 : 1;
        break;
    }
    r = cmp_base(a->name, b->name);
    return r ? r : strcmp(ext_of(a->name), ext_of(b->name));
}

void panel_sort(PANEL *p)
{
    /* Tri de Shell : pas de recursion, pas de memoire en plus, et rapide
     * sur un millier d'entrees a 8 MHz. */
    static const short gaps[] = { 701, 301, 132, 57, 23, 10, 4, 1 };
    int g, i, j;
    FINFO t;
    if (p->sort == SORT_NONE) return;
    for (g = 0; g < 8; g++) {
        int gap = gaps[g];
        for (i = gap; i < p->n; i++) {
            t = p->ent[i];
            for (j = i; j >= gap && entry_cmp(&p->ent[j - gap], &t, p->sort) > 0; j -= gap)
                p->ent[j] = p->ent[j - gap];
            p->ent[j] = t;
        }
    }
}

/* ---- Lecture ---- */

int panel_is_drives(const PANEL *p) { return p->path[0] == 0; }

static int is_root(const PANEL *p) { return !p->vfs && strlen(p->path) == 3; }

void panel_close_vfs(PANEL *p)
{
    if (!p->vfs) return;
    src_remove(vfs_source(p->vfs));
    vfs_close(p->vfs);
    p->vfs = 0;
}

void panel_update_free(PANEL *p)
{
    unsigned long total;
    p->free_ok = 0;
    if (panel_is_drives(p) || p->vfs) return;
    if (sys_dfree(toupper((unsigned char)p->path[0]) - 'A', &p->freeb, &total) == 0)
        p->free_ok = 1;
}

static void select_name(PANEL *p, const char *keep)
{
    int i;
    if (keep && keep[0]) {
        for (i = 0; i < p->n; i++)
            if (!strcmp(p->ent[i].name, keep)) { p->cur = i; break; }
    }
    if (p->cur >= p->n) p->cur = p->n - 1;
    if (p->cur < 0) p->cur = 0;
    if (p->top > p->cur) p->top = p->cur;
    if (p->cur >= p->top + PANEL_ROWS) p->top = p->cur - PANEL_ROWS + 1;
    if (p->top > 0 && p->top + PANEL_ROWS > p->n) {
        p->top = p->n - PANEL_ROWS;
        if (p->top < 0) p->top = 0;
    }
}

long panel_load(PANEL *p, const char *keep)
{
    char pat[PATH_MAX_TOSFC];
    FINFO f;
    long r;
    int n = 0;

    if (panel_is_drives(p)) { panel_drives(p, 0); return 0; }
    p->truncated = 0;
    p->err = 0;
    p->ntag = 0;
    p->tagbytes = 0;
    if (!is_root(p)) {
        memset(&p->ent[0], 0, sizeof(FINFO));
        strcpy(p->ent[0].name, "..");
        p->ent[0].attr = FA_DIR;
        p->ent[0].pad = PF_UP;
        n = 1;
    }
    if (p->vfs) {
        const SOURCE *s = vfs_source(p->vfs);
        r = s->first(s->ctx, p->path, &f);
    } else if (path_join(pat, p->path, "*.*", 0)) r = TE_TOOLONG;
    else r = sys_first(pat, FA_DIR | FA_HIDDEN | FA_SYSTEM | FA_RDONLY, &f);
    while (r == 0) {
        if (f.name[0] == '.' && (f.name[1] == 0 || (f.name[1] == '.' && f.name[2] == 0))) {
            /* . et .. du disque : le notre est deja en tete */
        } else if (f.attr & FA_LABEL) {
        } else if (!opt_show_hidden && (f.attr & (FA_HIDDEN | FA_SYSTEM))) {
        } else if (n >= PANEL_MAX) {
            p->truncated = 1;
            break;
        } else {
            f.pad = 0;
            p->ent[n++] = f;
        }
        if (p->vfs) {
            const SOURCE *s = vfs_source(p->vfs);
            r = s->next(s->ctx, &f);
        } else r = sys_next(&f);
    }
    if (r != 0 && r != EFILNF && r != ENMFIL) p->err = r;
    p->n = n;
    panel_sort(p);
    select_name(p, keep);
    panel_update_free(p);
    return p->err;
}

void panel_drives(PANEL *p, char select_drive)
{
    unsigned long map = Drvmap();
    int d, n = 0;
    panel_close_vfs(p);
    p->path[0] = 0;
    p->ntag = 0;
    p->tagbytes = 0;
    p->truncated = 0;
    p->err = 0;
    p->free_ok = 0;
    p->cur = 0;
    p->top = 0;
    for (d = 0; d < 26 && n < PANEL_MAX; d++) {
        if (!(map & (1UL << d))) continue;
        memset(&p->ent[n], 0, sizeof(FINFO));
        p->ent[n].name[0] = (char)('A' + d);
        p->ent[n].name[1] = ':';
        p->ent[n].attr = FA_DIR;
        if (p->ent[n].name[0] == select_drive) p->cur = n;
        n++;
    }
    p->n = n;
    select_name(p, 0);
}

/* ---- Navigation ---- */

FINFO *panel_current(PANEL *p)
{
    return (p->n > 0) ? &p->ent[p->cur] : 0;
}

void panel_move(PANEL *p, int delta)
{
    p->cur += delta;
    select_name(p, 0);
}

void panel_tag(PANEL *p, int idx, int on)
{
    FINFO *f;
    if (idx < 0 || idx >= p->n || panel_is_drives(p)) return;
    f = &p->ent[idx];
    if (f->pad & PF_UP) return;
    if (on && !(f->pad & PF_TAG)) {
        f->pad |= PF_TAG;
        p->ntag++;
        p->tagbytes += f->size;
    } else if (!on && (f->pad & PF_TAG)) {
        f->pad &= (unsigned char)~PF_TAG;
        p->ntag--;
        p->tagbytes -= f->size;
    }
}

long panel_enter(PANEL *p)
{
    FINFO *f = panel_current(p);
    char old[PATH_MAX_TOSFC], name[14];
    long r;

    if (!f) return 0;
    /* panel_load reecrit ent[] : on garde le nom avant. */
    str_copy(name, f->name, sizeof name);
    str_copy(old, p->path, sizeof old);
    if (panel_is_drives(p)) {
        p->path[0] = name[0];
        p->path[1] = ':';
        p->path[2] = '\\';
        p->path[3] = 0;
        p->cur = p->top = 0;
        r = panel_load(p, 0);
        if (r && p->n <= 0) panel_drives(p, name[0]);   /* lecteur illisible */
        return r;
    }
    if (f->pad & PF_UP) return panel_up(p);
    if (!(f->attr & FA_DIR)) {
        /* Une image ou une archive s'ouvre comme un dossier (pas une
         * archive dans une archive). */
        char file[PATH_MAX_TOSFC];
        VFS *v;
        if (p->vfs || vfs_kind_of_name(name) == VK_NONE) return 0;
        if (strlen(p->path) + strlen(name) + 2 + 13 >= PATH_MAX_TOSFC) return TE_TOOLONG;
        str_copy(file, p->path, sizeof file);
        str_add(file, name, sizeof file);
        str_copy(p->path, file, PATH_MAX_TOSFC);
        str_add(p->path, "\\", PATH_MAX_TOSFC);
        r = vfs_open(&v, file, p->path);
        if (r) {
            str_copy(p->path, old, PATH_MAX_TOSFC);
            return r;
        }
        p->vfs = v;
        src_add(vfs_source(v), vfs_root(v));
        p->cur = p->top = 0;
        r = panel_load(p, 0);
        if (r && p->n <= 1) {
            panel_close_vfs(p);
            str_copy(p->path, old, PATH_MAX_TOSFC);
            panel_load(p, name);
        }
        return r;
    }
    /* Garder de quoi ajouter un nom de fichier 8.3 au chemin. */
    if (strlen(p->path) + strlen(name) + 1 + 13 >= PATH_MAX_TOSFC) return TE_TOOLONG;
    str_add(p->path, name, PATH_MAX_TOSFC);
    str_add(p->path, "\\", PATH_MAX_TOSFC);
    p->cur = p->top = 0;
    r = panel_load(p, 0);
    if (r && p->n <= 1) {
        str_copy(p->path, old, PATH_MAX_TOSFC);
        panel_load(p, name);
    }
    return r;
}

long panel_up(PANEL *p)
{
    char name[14];
    int l;
    char *s;

    if (panel_is_drives(p)) return 0;
    if (is_root(p)) {
        panel_drives(p, p->path[0]);
        return 0;
    }
    /* A la racine d'une image ou d'une archive : on la referme, et la
     * selection revient sur son fichier (meme chemin sans le '\' final). */
    if (p->vfs && !strcmp(p->path, vfs_root(p->vfs))) panel_close_vfs(p);
    l = (int)strlen(p->path);
    p->path[l - 1] = 0;
    s = strrchr(p->path, '\\');
    str_copy(name, s + 1, sizeof name);
    s[1] = 0;
    p->cur = p->top = 0;
    return panel_load(p, name);
}

int panel_items(PANEL *p, FINFO *out, int cap)
{
    int i, n = 0;
    if (panel_is_drives(p)) return 0;
    if (p->ntag) {
        for (i = 0; i < p->n && n < cap; i++)
            if (p->ent[i].pad & PF_TAG) out[n++] = p->ent[i];
        return n;
    }
    if (p->n > 0 && !(p->ent[p->cur].pad & PF_UP)) out[n++] = p->ent[p->cur];
    return n;
}

/* ---- Affichage ---- */

static void seps(int x, int y, int ch, int attr)
{
    scr_putc(x + SEP1, y, ch, attr);
    scr_putc(x + SEP2, y, ch, attr);
    scr_putc(x + SEP3, y, ch, attr);
}

static void draw_entry(PANEL *p, int x, int y, int idx, int active)
{
    FINFO *f = &p->ent[idx];
    char buf[16], num[16];
    int tagged = (f->pad & PF_TAG) != 0;
    int attr = (active && idx == p->cur) ? (tagged ? A_CURTAG : A_CURSOR)
             : (tagged ? A_TAG : A_NORMAL);

    scr_fill(x + 1, y, 38, ' ', attr);
    seps(x, y, G_SV, attr);
    scr_putc(x + COL_NAME, y, tagged ? G_CHECK : ' ', attr);
    if (panel_is_drives(p)) {
        scr_puts(x + COL_NAME + 1, y, f->name, attr);
        scr_puts(x + COL_SIZE, y, "  <DRIVE>", attr);
        return;
    }
    if (f->pad & PF_UP) {
        scr_putc(x + COL_NAME + 1, y, G_UPDIR, attr);
        scr_puts(x + COL_NAME + 2, y, "..", attr);
        scr_puts(x + COL_SIZE, y, "     <UP>", attr);
        return;
    }
    fmt_name83(buf, f->name);
    scr_puts(x + COL_NAME + 1, y, buf, attr);
    if (f->attr & FA_DIR) {
        scr_puts(x + COL_SIZE, y, "    <DIR>", attr);
    } else {
        if (fmt_ulong(num, f->size, ',') > 9) fmt_ulong(num, f->size, 0);
        pad_left(buf, num, 9);
        scr_puts(x + COL_SIZE, y, buf, attr);
    }
    fmt_date(buf, f->date);
    scr_puts(x + COL_DATE, y, buf, attr);
    fmt_time(buf, f->time);
    scr_puts(x + COL_TIME, y, buf, attr);
}

static void center(int x, int y, int w, const char *s, int attr)
{
    int l = (int)strlen(s);
    if (l > w) { s += l - w; l = w; }   /* chemin trop long : on garde la fin */
    scr_puts(x + (w - l) / 2, y, s, attr);
}

void panel_draw(PANEL *p, int x, int active)
{
    int i, y;
    char line[48], num[16];
    static const char *const heads[] = { "Name", "Size", "Date", "Time" };
    static const short hx[] = { COL_NAME, COL_SIZE, COL_DATE, COL_TIME };
    static const short hw[] = { 13, 9, 8, 5 };
    static const short hsort[] = { SORT_NAME, SORT_SIZE, SORT_DATE, -1 };

    /* Cadre */
    scr_putc(x, 0, G_DTL, A_FRAME);
    scr_fill(x + 1, 0, 38, G_DH, A_FRAME);
    seps(x, 0, G_DTS, A_FRAME);
    scr_putc(x + 39, 0, G_DTR, A_FRAME);
    for (y = 1; y <= 22; y++) {
        scr_putc(x, y, G_DV, A_FRAME);
        scr_putc(x + 39, y, G_DV, A_FRAME);
    }
    scr_putc(x, 21, G_DLS, A_FRAME);
    scr_fill(x + 1, 21, 38, G_SH, A_FRAME);
    seps(x, 21, G_SBT, A_FRAME);
    scr_putc(x + 39, 21, G_DRS, A_FRAME);
    scr_putc(x, 23, G_DBL, A_FRAME);
    scr_fill(x + 1, 23, 38, G_DH, A_FRAME);
    scr_putc(x + 39, 23, G_DBR, A_FRAME);

    /* Chemin dans le cadre du haut */
    line[0] = ' ';
    if (panel_is_drives(p)) str_copy(line + 1, "Drives", 34);
    else if (strlen(p->path) <= 33) str_copy(line + 1, p->path, 34);
    else {
        /* Trop long : la fin du chemin, la plus utile ("...\SUB\DIR\"). */
        str_copy(line + 1, "...", 34);
        str_add(line, p->path + strlen(p->path) - 30, sizeof line);
    }
    str_add(line, " ", sizeof line);
    center(x + 1, 0, 38, line, active ? A_BARTXT : A_TITLE);

    /* En-tetes : la colonne de tri est suivie d'une fleche. */
    scr_fill(x + 1, 1, 38, ' ', A_FRAME);
    seps(x, 1, G_SV, A_FRAME);
    for (i = 0; i < 4; i++) {
        int is = (hsort[i] == p->sort) || (i == 0 && p->sort == SORT_EXT);
        str_copy(line, i == 0 && p->sort == SORT_EXT ? "Ext" : heads[i], sizeof line);
        if (is) { int l = (int)strlen(line); line[l] = 0x02; line[l + 1] = 0; }
        center(x + hx[i], 1, hw[i], line, is ? A_TITLE : A_FRAME);
    }

    /* Entrees */
    for (i = 0; i < PANEL_ROWS; i++) {
        int idx = p->top + i;
        if (idx < p->n) draw_entry(p, x, 2 + i, idx, active);
        else {
            scr_fill(x + 1, 2 + i, 38, ' ', A_NORMAL);
            seps(x, 2 + i, G_SV, A_NORMAL);
        }
    }

    /* Ligne d'information */
    line[0] = 0;
    if (p->err) {
        str_copy(line, err_text(p->err), sizeof line);
    } else if (p->ntag) {
        fmt_ulong(num, (unsigned long)p->ntag, 0);
        str_copy(line, num, sizeof line);
        str_add(line, " tagged, ", sizeof line);
        fmt_ulong(num, p->tagbytes, ',');
        str_add(line, num, sizeof line);
        str_add(line, " bytes", sizeof line);
    } else if (panel_is_drives(p)) {
        str_copy(line, "RETURN opens a drive", sizeof line);
    } else if (p->vfs && p->n > 0 && (p->ent[p->cur].pad & PF_UP)
               && !strcmp(p->path, vfs_root(p->vfs))) {
        str_copy(line, vfs_describe(p->vfs), sizeof line);
    } else if (p->n > 0) {
        FINFO *f = &p->ent[p->cur];
        if (f->pad & PF_UP) str_copy(line, "Parent folder", sizeof line);
        else {
            str_copy(line, f->name, sizeof line);
            while (strlen(line) < 13) str_add(line, " ", sizeof line);
            if (f->attr & FA_RDONLY) str_add(line, "Read-only ", sizeof line);
            if (f->attr & FA_HIDDEN) str_add(line, "Hidden ", sizeof line);
            if (f->attr & FA_SYSTEM) str_add(line, "System ", sizeof line);
            if (f->attr & FA_ARCH) str_add(line, "Archive", sizeof line);
        }
    } else {
        str_copy(line, "Empty folder", sizeof line);
    }
    if (p->truncated && !p->err) str_copy(line, "Only the first 1024 entries shown", sizeof line);
    scr_field(x + 1, 22, 38, line, p->ntag ? A_TAG : A_NORMAL);

    /* Place libre */
    if (p->vfs) {
        center(x + 1, 23, 38, " Read-only ", A_FRAME);
    } else if (p->free_ok) {
        line[0] = ' ';
        fmt_ulong(line + 1, p->freeb, ',');
        str_add(line, " bytes free ", sizeof line);
        center(x + 1, 23, 38, line, A_FRAME);
    }
}
