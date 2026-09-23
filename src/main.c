/*
 * main.c -- TOS File Cmd : deux panneaux, un Atari ST.
 */
#include "tos.h"
#include "libc.h"
#include "screen.h"
#include "input.h"
#include "ui.h"
#include "panel.h"
#include "fsops.h"
#include "prefs.h"
#include "view.h"
#include "edit.h"
#include "music.h"
#include "picture.h"
#include "version.h"

#define Cconws(s) TRAP_WL(1, 0x09, s)
#define Cconin()  TRAP_W(1, 0x01)

static PANEL panels[2];
static int active;
static FINFO items[PANEL_MAX];
static OPS ops;
static char home[PATH_MAX_TOSFC];
static int opt_verify = 1;
static int quit;

/* ---- Barre des touches ---- */

typedef struct { const char *key, *label; short cmd; } KEYBAR;
enum { C_HELP, C_COPY, C_MOVE, C_REN, C_DEL, C_MKDIR, C_ATTR, C_SORT,
       C_DRIVES, C_OPTS, C_QUIT, C_TEXT, C_IMAGE, C_HEX, C_VIEW, C_EDIT, C_MUSIC,
       C_PAUSE, C_NCMD };
/* 80 colonnes tout juste : les libelles sont courts. */
static const KEYBAR keybar[] = {
    { "?", "Help", C_HELP }, { "T", "View", C_TEXT }, { "I", "Pic", C_IMAGE },
    { "E", "Edit", C_EDIT }, { "C", "Copy", C_COPY }, { "V", "Move", C_MOVE },
    { "R", "Ren", C_REN }, { "D", "Del", C_DEL }, { "K", "MkD", C_MKDIR },
    { "A", "Attr", C_ATTR }, { "S", "Sort", C_SORT }, { "L", "Drv", C_DRIVES },
    { "O", "Opts", C_OPTS }, { "Q", "Quit", C_QUIT },
};
#define NKEYBAR (int)(sizeof keybar / sizeof keybar[0])
static short keybar_x[NKEYBAR + 1];

static void draw_keybar(void)
{
    int i, x = 0;
    for (i = 0; i < NKEYBAR; i++) {
        keybar_x[i] = (short)x;
        scr_puts(x, 24, keybar[i].key, A_BARKEY);
        x += (int)strlen(keybar[i].key);
        scr_puts(x, 24, keybar[i].label, A_BARTXT);
        x += (int)strlen(keybar[i].label);
        if (x < SCR_COLS) scr_putc(x++, 24, ' ', A_NORMAL);
    }
    keybar_x[NKEYBAR] = (short)x;
    scr_fill(x, 24, SCR_COLS - x, ' ', A_NORMAL);
    /* Une note au bout de la barre : musique en cours (clignote en pause). */
    if (music_state() == MUS_PLAYING) scr_putc(SCR_COLS - 1, 24, G_NOTE, A_TITLE);
    else if (music_state() == MUS_PAUSED) scr_putc(SCR_COLS - 1, 24, G_NOTE, A_FRAME);
}

static void draw(void)
{
    panel_draw(&panels[0], 0, active == 0);
    panel_draw(&panels[1], 40, active == 1);
    draw_keybar();
}

/* ---- Rappels des operations ---- */

static void fmt_info(char *out, const char *label, const FINFO *f)
{
    char num[16], d[10], t[8];
    str_copy(out, label, 60);
    fmt_ulong(num, f->size, ',');
    str_add(out, num, 60);
    str_add(out, " bytes  ", 60);
    fmt_date(d, f->date);
    fmt_time(t, f->time);
    str_add(out, d, 60);
    str_add(out, " ", 60);
    str_add(out, t, 60);
}

static int cb_ask(OPS *o, int q, const char *path, const FINFO *s, const FINFO *d)
{
    const char *lines[4];
    char l2[64], l3[64];
    static const char *const ow[] = { "Yes", "No", "All", "None", "Cancel" };
    static const char *const mg[] = { "Yes", "No", "Cancel" };
    int r;
    (void)o;
    if (q == Q_MERGE) {
        lines[0] = "This folder already exists:";
        lines[1] = path;
        lines[2] = "Copy into it? Existing files are asked about.";
        r = ui_dialog("Folder exists", lines, 3, mg, 3, 0);
        return r == 0 ? ANS_YES : r == 1 ? ANS_NO : ANS_CANCEL;
    }
    lines[0] = "Replace the existing file?";
    lines[1] = path;
    fmt_info(l2, "Existing: ", d);
    fmt_info(l3, "New:      ", s);
    lines[2] = l2;
    lines[3] = l3;
    r = ui_dialog("File exists", lines, 4, ow, 5, 1);
    switch (r) {
    case 0: return ANS_YES;
    case 1: return ANS_NO;
    case 2: return ANS_ALL;
    case 3: return ANS_NONE;
    }
    return ANS_CANCEL;
}

static void cb_progress(OPS *o, const char *name)
{
    ui_progress(name, o->bytes_done, o->bytes_total, o->files_done, o->files_total);
}

static int cb_cancel(OPS *o)
{
    (void)o;
    return in_escape();
}

static int cb_error(OPS *o, const char *path, long err, int more)
{
    const char *lines[3];
    static const char *const cs[] = { "Continue", "Stop" };
    static const char *const ok[] = { "OK" };
    (void)o;
    lines[0] = path;
    lines[1] = err_text(err);
    if (err == TE_RESTORE) lines[1] = "Could not restore: the old version is in TOSFC.BAK";
    if (!more) {
        ui_dialog("Error", lines, 2, ok, 1, 0);
        return 0;
    }
    lines[2] = "Continue with the remaining items?";
    return ui_dialog("Error", lines, 3, cs, 2, 1) == 0;
}

static void ops_setup(void)
{
    memset(&ops, 0, sizeof ops);
    ops.ask = cb_ask;
    ops.progress = cb_progress;
    ops.cancel = cb_cancel;
    ops.error = cb_error;
    ops.verify = opt_verify;
}

/* ---- Rechargement ---- */

static void reload(int i)
{
    PANEL *p = &panels[i];
    char keep[14];
    FINFO *f = panel_current(p);
    long r;
    keep[0] = 0;
    if (f) str_copy(keep, f->name, sizeof keep);
    r = panel_load(p, keep);
    if (r && p->n <= (strlen(p->path) > 3 ? 1 : 0) && !panel_is_drives(p)) {
        /* Le dossier a disparu ou la disquette a change : on remonte a la
         * liste des lecteurs plutot que de rester sur un panneau vide. */
        panel_drives(p, p->path[0]);
    }
}

static void reload_both(void)
{
    reload(0);
    if (strcmp(panels[0].path, panels[1].path) || panel_is_drives(&panels[1]))
        reload(1);
    else
        panel_load(&panels[1], panel_current(&panels[1]) ? panel_current(&panels[1])->name : 0);
}

/* ---- Resume ---- */

static void summary(const char *verb)
{
    char l1[64], l2[64], num[12];
    /* Les fichiers passes l'ont ete a la demande de l'utilisateur : seuls
     * les erreurs et avertissements meritent un compte rendu. */
    if (!ops.errors && !ops.warnings) return;
    /* Un seul element : sa boite d'erreur a deja tout dit. */
    if (ops.files_total + ops.dirs_total <= 1) return;
    fmt_ulong(num, (unsigned long)ops.files_done, 0);
    str_copy(l1, num, sizeof l1);
    str_add(l1, " file(s) ", sizeof l1);
    str_add(l1, verb, sizeof l1);
    str_add(l1, ".", sizeof l1);
    l2[0] = 0;
    fmt_ulong(num, (unsigned long)ops.errors, 0);
    str_add(l2, num, sizeof l2);
    str_add(l2, " error(s), ", sizeof l2);
    fmt_ulong(num, (unsigned long)ops.skipped, 0);
    str_add(l2, num, sizeof l2);
    str_add(l2, " skipped", sizeof l2);
    if (ops.warnings) str_add(l2, ", see warnings", sizeof l2);
    ui_message("Done", l1, l2);
}

/* ---- Commandes ---- */

static int count_items(int n, int *dirs)
{
    int i;
    *dirs = 0;
    for (i = 0; i < n; i++) if (items[i].attr & FA_DIR) (*dirs)++;
    return n;
}

static void describe(char *out, int n, int dirs)
{
    char num[12];
    if (n == 1) {
        str_copy(out, items[0].name, 60);
        str_add(out, dirs ? " (folder)" : "", 60);
        return;
    }
    fmt_ulong(num, (unsigned long)n, 0);
    str_copy(out, num, 60);
    str_add(out, " items", 60);
    if (dirs) {
        fmt_ulong(num, (unsigned long)dirs, 0);
        str_add(out, ", ", 60);
        str_add(out, num, 60);
        str_add(out, " folder(s)", 60);
    }
}

static void cmd_copy(int move)
{
    PANEL *src = &panels[active], *dst = &panels[1 - active];
    const char *lines[3];
    static const char *const cb[] = { "Copy", "Cancel" };
    static const char *const mb[] = { "Move", "Cancel" };
    char what[64], to[PATH_MAX_TOSFC + 4];
    int n, dirs;
    long r;

    n = panel_items(src, items, PANEL_MAX);
    if (n == 0) return;
    if (panel_is_drives(dst)) {
        ui_message(move ? "Move" : "Copy", "Open the destination folder",
                   "in the other panel first.");
        return;
    }
    if (!strcmp(src->path, dst->path)) {
        ui_message(move ? "Move" : "Copy", "Source and destination",
                   "are the same folder.");
        return;
    }
    count_items(n, &dirs);
    describe(what, n, dirs);
    str_copy(to, "to ", sizeof to);
    str_add(to, dst->path, sizeof to);
    lines[0] = what;
    lines[1] = to;
    lines[2] = move ? "Sources are deleted only after verification." : 0;
    if (ui_dialog(move ? "Move" : "Copy", lines, move ? 3 : 2,
                  move ? mb : cb, 2, 0) != 0)
        return;

    ops_setup();
    r = ops_begin(&ops);
    if (r) { ui_error("Cannot start", "", r); return; }
    r = ops_scan(&ops, src->path, items, n);
    if (r) {
        ui_error("Nothing was written. Cannot read:", ops.warn_path, r);
        ops_end(&ops);
        reload_both();
        return;
    }
    ui_progress_open(move ? "Moving" : "Copying");
    ui_progress("", 0, ops.bytes_total, 0, ops.files_total);
    ops_copy(&ops, src->path, items, n, dst->path, move);
    ui_progress_close();
    ops_end(&ops);
    reload_both();
    summary(move ? "moved" : "copied");
}

static void cmd_delete(void)
{
    PANEL *p = &panels[active];
    const char *lines[3];
    static const char *const db[] = { "Delete", "Cancel" };
    char what[64];
    int n, dirs;
    long r;

    n = panel_items(p, items, PANEL_MAX);
    if (n == 0) return;
    count_items(n, &dirs);
    describe(what, n, dirs);
    lines[0] = what;
    lines[1] = p->path;
    lines[2] = dirs ? "Folders are deleted with everything in them." : 0;
    if (ui_dialog("Delete", lines, dirs ? 3 : 2, db, 2, 1) != 0) return;

    ops_setup();
    r = ops_begin(&ops);
    if (r) { ui_error("Cannot start", "", r); return; }
    r = ops_scan(&ops, p->path, items, n);
    if (r) {
        ui_error("Nothing was deleted. Cannot read:", ops.warn_path, r);
        ops_end(&ops);
        reload_both();
        return;
    }
    ui_progress_open("Deleting");
    ui_progress("", 0, ops.bytes_total, 0, ops.files_total);
    ops_delete(&ops, p->path, items, n);
    ui_progress_close();
    ops_end(&ops);
    reload_both();
    summary("deleted");
}

static FINFO *current_real(void)
{
    PANEL *p = &panels[active];
    FINFO *f = panel_current(p);
    if (!f || panel_is_drives(p) || (f->pad & PF_UP)) return 0;
    return f;
}

static void cmd_rename(void)
{
    FINFO *f = current_real();
    char name[14], old[14], norm[13];
    long r;
    if (!f) return;
    str_copy(old, f->name, sizeof old);
    str_copy(name, old, sizeof name);
    if (!ui_input("Rename", old, name, 13)) return;
    r = ops_rename(panels[active].path, old, name);
    if (r) { ui_error("Cannot rename", old, r); return; }
    name_normalize(name, norm);
    panel_load(&panels[active], norm);
    if (!strcmp(panels[0].path, panels[1].path)) reload(1 - active);
}

static void cmd_mkdir(void)
{
    PANEL *p = &panels[active];
    char name[14], norm[13];
    long r;
    if (panel_is_drives(p)) return;
    name[0] = 0;
    if (!ui_input("Make folder", "Name of the new folder:", name, 13)) return;
    if (!name[0]) return;
    r = ops_mkdir(p->path, name);
    if (r) { ui_error("Cannot make folder", name, r); return; }
    name_normalize(name, norm);
    panel_load(p, norm);
    if (!strcmp(panels[0].path, panels[1].path)) reload(1 - active);
}

static void cmd_attrib(void)
{
    PANEL *p = &panels[active];
    static const char *const bt[] = { "Read-only", "Hidden", "System", "Archive", "OK" };
    static const int bits[] = { FA_RDONLY, FA_HIDDEN, FA_SYSTEM, FA_ARCH };
    const char *lines[5];
    char l[4][24], what[64];
    int n, dirs, attr, i, r, files;
    long e;

    n = panel_items(p, items, PANEL_MAX);
    if (n == 0) return;
    count_items(n, &dirs);
    files = n - dirs;
    if (files == 0) { ui_message("Attributes", "Folder attributes are left", "as they are."); return; }
    describe(what, n, dirs);
    for (i = 0; i < n && (items[i].attr & FA_DIR); i++) {}
    attr = items[i].attr & (FA_RDONLY | FA_HIDDEN | FA_SYSTEM | FA_ARCH);
    for (;;) {
        for (i = 0; i < 4; i++) {
            str_copy(l[i], (attr & bits[i]) ? "[x] " : "[ ] ", sizeof l[i]);
            str_add(l[i], bt[i], sizeof l[i]);
            lines[1 + i] = l[i];
        }
        lines[0] = what;
        r = ui_dialog("Attributes", lines, 5, bt, 5, 4);
        if (r < 0) return;
        if (r == 4) break;
        attr ^= bits[r];
    }
    for (i = 0; i < n; i++) {
        if (items[i].attr & FA_DIR) continue;
        e = ops_setattr(p->path, &items[i], attr);
        if (e) { ui_error("Cannot change attributes", items[i].name, e); break; }
    }
    reload_both();
}

static void cmd_sort(PANEL *p, int mode)
{
    FINFO *f = panel_current(p);
    char keep[14];
    keep[0] = 0;
    if (f) str_copy(keep, f->name, sizeof keep);
    p->sort = mode;
    if (panel_is_drives(p)) return;
    if (mode == SORT_NONE) {
        panel_load(p, keep);
        return;
    }
    panel_sort(p);
    {
        int i;
        for (i = 0; i < p->n; i++)
            if (!strcmp(p->ent[i].name, keep)) { p->cur = i; break; }
        panel_move(p, 0);
    }
}

static void cmd_help(void)
{
    static const char *const help[] = {
        "TAB          switch panels        RETURN   open folder / drive",
        "ESC, BkSp    go up                L        list of drives",
        "Up / Down    select               Left / Right  page",
        "Home         first entry          Shift+Home    last entry",
        "SPACE, Ins   tag and move down    *        invert tags",
        "+ / -        tag all / untag all  Ctrl+R   re-read both panels",
        "",
        "RETURN, F3   view a file: pictures full screen, others as text",
        "T  I  H      read as text, show as a picture, show in hex",
        "E  F4        edit a text file (on a folder: a new file)",
        "M  P         music box, pause (RETURN on .YM/.SND plays it)",
        "C  F5        copy to the other panel",
        "V  F6        move to the other panel",
        "R            rename               K  F7    make a folder",
        "D  F8        delete               A  F2    attributes",
        "S  F9        sort: name, ext, size, date, disk order",
        "O            options (verify, hidden files, save settings)",
        "?  HELP  F1  this page            Q  F10   quit",
        "Mouse: click selects, click again opens; right click tags;",
        "click a column title to sort, the path to go up.",
        "",
        "Copies are verified; a replaced file stays TOSFC.BAK until checked.",
    };
    const int n = (int)(sizeof help / sizeof help[0]);
    int i;
    EVENT e;
    scr_box(4, 1, 72, n + 4, A_DIALOG);
    scr_puts(30, 1, " " TOSFC_NAME " " TOSFC_VERSION " ", A_DIALOG);
    for (i = 0; i < n; i++) scr_field(6, 3 + i, 68, help[i], A_DIALOG);
    scr_puts(26, n + 4, " Press any key ", A_DIALOG);
    do in_wait(&e); while (e.type == EV_NONE);
}

static long save_settings(void)
{
    PREFS pr;
    int i;
    memset(&pr, 0, sizeof pr);
    for (i = 0; i < 2; i++) {
        str_copy(pr.path[i], panels[i].path, PATH_MAX_TOSFC);
        pr.sort[i] = panels[i].sort;
    }
    pr.active = active;
    pr.verify = opt_verify;
    pr.show_hidden = opt_show_hidden;
    return prefs_save(&pr, home);
}

static void cmd_options(void)
{
    static const char *const bt[] = { "Verify", "Hidden", "Save", "OK" };
    const char *lines[4];
    char l1[48], l2[48], where[PATH_MAX_TOSFC + 16];
    int r;
    long e;
    for (;;) {
        str_copy(l1, opt_verify ? "[x] Verify every copy (slower, safer)"
                                : "[ ] Verify every copy (slower, safer)", sizeof l1);
        str_copy(l2, opt_show_hidden ? "[x] Show hidden and system files"
                                     : "[ ] Show hidden and system files", sizeof l2);
        str_copy(where, "Save writes TOSFC.INF in ", sizeof where);
        str_add(where, home, sizeof where);
        lines[0] = l1;
        lines[1] = l2;
        lines[2] = "";
        lines[3] = where;
        r = ui_dialog("Options", lines, 4, bt, 4, 3);
        if (r < 0 || r == 3) break;
        if (r == 0) opt_verify = !opt_verify;
        if (r == 1) {
            opt_show_hidden = !opt_show_hidden;
            reload(0);
            reload(1);
            draw();
        }
        if (r == 2) {
            e = save_settings();
            if (e) ui_error("Settings not saved", home, e);
            else ui_message("Options", "Settings saved.", 0);
        }
    }
}

static void cmd_music(void)
{
    static const char *const bt[] = { "Pause", "Stop", "Prev", "Next", "OK" };
    const char *lines[4];
    char l2[64], l3[64], num[12];
    int r;
    for (;;) {
        if (music_state() == MUS_NONE) {
            ui_message("Music", "Nothing is playing. RETURN on a .YM, .SND",
                       "or .SNDH file starts a tune.");
            return;
        }
        lines[0] = music_title()[0] ? music_title() : "(no title)";
        lines[1] = music_author()[0] ? music_author() : "(unknown author)";
        str_copy(l2, music_format(), sizeof l2);
        str_add(l2, "   tune ", sizeof l2);
        fmt_ulong(num, (unsigned long)music_tune_number(), 0);
        str_add(l2, num, sizeof l2);
        str_add(l2, " of ", sizeof l2);
        fmt_ulong(num, (unsigned long)music_tunes(), 0);
        str_add(l2, num, sizeof l2);
        lines[2] = l2;
        str_copy(l3, music_state() == MUS_PAUSED ? "Paused at " : "Playing: ", sizeof l3);
        fmt_ulong(num, music_seconds(), 0);
        str_add(l3, num, sizeof l3);
        str_add(l3, " s", sizeof l3);
        lines[3] = l3;
        r = ui_dialog("Music", lines, 4, bt, 5, 4);
        if (r == 0) music_pause();
        else if (r == 1) { music_stop(); return; }
        else if (r == 2) music_tune(-1);
        else if (r == 3) music_tune(1);
        else return;
    }
}

static void do_cmd(int c)
{
    switch (c) {
    case C_HELP: cmd_help(); break;
    case C_COPY: cmd_copy(0); break;
    case C_MOVE: cmd_copy(1); break;
    case C_REN: cmd_rename(); break;
    case C_DEL: cmd_delete(); break;
    case C_MKDIR: cmd_mkdir(); break;
    case C_ATTR: cmd_attrib(); break;
    case C_SORT: cmd_sort(&panels[active], (panels[active].sort + 1) % SORT_COUNT); break;
    case C_DRIVES: {
        PANEL *p = &panels[active];
        if (!panel_is_drives(p)) panel_drives(p, p->path[0]);
        break;
    }
    case C_OPTS: cmd_options(); break;
    case C_TEXT: view_text(&panels[active], 0); break;
    case C_HEX: view_text(&panels[active], 1); break;
    case C_IMAGE: view_picture(&panels[active]); break;
    case C_EDIT:
        if (edit_file(&panels[active])) reload_both();
        break;
    case C_VIEW: {
        FINFO *f = panel_current(&panels[active]);
        if (f && !(f->attr & FA_DIR) && !panel_is_drives(&panels[active])) {
            if (music_is_name(f->name)) {
                long r = music_play(panels[active].path, f->name);
                if (r) ui_error("Cannot play", f->name, r);
            } else {
                do_cmd(pic_is_picture_name(f->name) ? C_IMAGE : C_TEXT);
            }
        }
        break;
    }
    case C_PAUSE: music_pause(); break;
    case C_MUSIC: cmd_music(); break;
    case C_QUIT:
        if (ui_confirm("Quit", "Leave " TOSFC_NAME "?", 0, 1)) {
            music_stop();
            if (music_hooked()) {
                ui_message("Quit", "Another program hooked the timer after the",
                           "music: TOSFC must stay until it unhooks.");
                break;
            }
            quit = 1;
        }
        break;
    }
}

static void show_load_error(long r)
{
    if (r) ui_error("Cannot read this folder", panels[active].path[0] ? panels[active].path : "", r);
}

/* ---- Evenements ---- */

static void on_key(EVENT *e)
{
    PANEL *p = &panels[active];
    int c = toupper(e->ascii), i;

    switch (e->scan) {
    case SC_TAB: active = 1 - active; return;
    case SC_RETURN: case SC_ENTER: {
        FINFO *f = panel_current(p);
        if (!f) return;
        if (panel_is_drives(p) || (f->attr & FA_DIR)) show_load_error(panel_enter(p));
        else do_cmd(C_VIEW);
        return;
    }
    case SC_ESC: case SC_BACKSP: show_load_error(panel_up(p)); return;
    case SC_UP:
        panel_move(p, (e->shift & SHIFT_ANY) ? -(PANEL_ROWS - 1) : -1);
        return;
    case SC_DOWN:
        panel_move(p, (e->shift & SHIFT_ANY) ? PANEL_ROWS - 1 : 1);
        return;
    case SC_LEFT: panel_move(p, -(PANEL_ROWS - 1)); return;
    case SC_RIGHT: panel_move(p, PANEL_ROWS - 1); return;
    case SC_HOME:
        panel_move(p, (e->shift & SHIFT_ANY) ? p->n : -p->n);
        return;
    case SC_INSERT:
        panel_tag(p, p->cur, !(p->ent[p->cur].pad & PF_TAG));
        panel_move(p, 1);
        return;
    case SC_HELP: case SC_F1: do_cmd(C_HELP); return;
    case SC_F2: do_cmd(C_ATTR); return;
    case SC_F3: do_cmd(C_VIEW); return;
    case SC_F4: do_cmd(C_EDIT); return;
    case SC_F5: do_cmd(C_COPY); return;
    case SC_F6: do_cmd(C_MOVE); return;
    case SC_F7: do_cmd(C_MKDIR); return;
    case SC_F8: case SC_DELETE: do_cmd(C_DEL); return;
    case SC_F9: do_cmd(C_SORT); return;
    case SC_F10: do_cmd(C_QUIT); return;
    }
    if (e->ascii == 0x12) {         /* Ctrl+R */
        reload(0);
        reload(1);
        return;
    }
    if (c == ' ') {
        if (p->n > 0) panel_tag(p, p->cur, !(p->ent[p->cur].pad & PF_TAG));
        panel_move(p, 1);
        return;
    }
    if (c == '*' || c == '+' || c == '-') {
        for (i = 0; i < p->n; i++) {
            if (p->ent[i].attr & FA_DIR && c == '+') continue;
            panel_tag(p, i, c == '*' ? !(p->ent[i].pad & PF_TAG) : c == '+');
        }
        return;
    }
    if (c == 'H') { do_cmd(C_HEX); return; }
    if (c == 'P') { do_cmd(C_PAUSE); return; }
    if (c == 'M') { do_cmd(C_MUSIC); return; }
    for (i = 0; i < NKEYBAR; i++)
        if (c == keybar[i].key[0]) { do_cmd(keybar[i].cmd); return; }
}

static void on_click(EVENT *e, int right)
{
    int side = e->x >= 40, i;
    PANEL *p = &panels[side];
    int rel = e->x - side * 40;

    if (e->y == 24) {
        if (right) return;
        for (i = 0; i < NKEYBAR; i++)
            if (e->x >= keybar_x[i] && e->x < keybar_x[i + 1]) do_cmd(keybar[i].cmd);
        return;
    }
    if (side != active) {
        active = side;
        if (e->y < 2 || e->y > 20) return;
    }
    if (e->y == 0) {
        if (!right) show_load_error(panel_up(p));
        return;
    }
    if (e->y == 1) {
        if (right) return;
        if (rel < 14) cmd_sort(p, p->sort == SORT_NAME ? SORT_EXT : SORT_NAME);
        else if (rel < 24) cmd_sort(p, SORT_SIZE);
        else if (rel < 33) cmd_sort(p, SORT_DATE);
        else cmd_sort(p, SORT_NONE);
        return;
    }
    if (e->y >= 2 && e->y <= 20) {
        int idx = p->top + e->y - 2;
        if (idx >= p->n) return;
        if (right) {
            panel_tag(p, idx, !(p->ent[idx].pad & PF_TAG));
            return;
        }
        if (idx == p->cur) {
            EVENT k;
            k.type = EV_KEY;
            k.scan = SC_RETURN;
            k.ascii = 13;
            k.shift = 0;
            on_key(&k);
        } else {
            p->cur = idx;
        }
    }
}

/* ---- Demarrage ---- */

static void init_home(void)
{
    short d = Dgetdrv();
    char buf[PATH_MAX_TOSFC];
    home[0] = (char)('A' + d);
    home[1] = ':';
    home[2] = 0;
    buf[0] = 0;
    if (Dgetpath(buf, d + 1) < 0) buf[0] = 0;
    if (strlen(buf) + 4 < PATH_MAX_TOSFC - 14) str_add(home, buf, sizeof home);
    if (home[strlen(home) - 1] != '\\') str_add(home, "\\", sizeof home);
}

/* Un chemin enregistre n'est rouvert que sur le lecteur du programme ou un
 * disque dur : pas de demande de disquette au demarrage. */
static int restorable(const char *path)
{
    unsigned long map = Drvmap();
    int d;
    if (!path[0]) return 0;
    d = toupper((unsigned char)path[0]) - 'A';
    if (d < 0 || d > 25 || !(map & (1UL << d))) return 0;
    return d >= 2 || path[0] == home[0];
}

static void init_panels(void)
{
    PREFS pr;
    int i;
    long r;
    memset(&pr, 0, sizeof pr);
    str_copy(pr.path[0], home, PATH_MAX_TOSFC);
    pr.verify = 1;
    pr.show_hidden = 1;
    prefs_load(&pr, home);
    opt_verify = pr.verify;
    opt_show_hidden = pr.show_hidden;
    active = pr.active ? 1 : 0;
    for (i = 0; i < 2; i++) {
        PANEL *p = &panels[i];
        p->sort = pr.sort[i];
        p->path[0] = 0;
        if (restorable(pr.path[i])) str_copy(p->path, pr.path[i], PATH_MAX_TOSFC);
        if (panel_is_drives(p)) {
            panel_drives(p, home[0]);
        } else {
            r = panel_load(p, 0);
            if (r && p->n <= 1) panel_drives(p, p->path[0]);
        }
    }
}

int main(void)
{
    const char *msg;
    EVENT e;
    FINFO *mem;

    mem = (FINFO *)sys_alloc(2L * PANEL_MAX * sizeof(FINFO));
    if (!mem) {
        Cconws("TOS File Cmd: not enough memory.\r\n");
        Cconin();
        return 1;
    }
    panels[0].ent = mem;
    panels[1].ent = mem + PANEL_MAX;

    msg = scr_init();
    if (msg) {
        Cconws(msg);
        Cconin();
        sys_free(mem);
        return 1;
    }
    in_init();
    ui_critic_install();
    init_home();
    init_panels();

    while (!quit) {
        draw();
        in_wait(&e);
        if (e.type == EV_KEY) on_key(&e);
        else if (e.type == EV_CLICK) on_click(&e, 0);
        else if (e.type == EV_RCLICK) on_click(&e, 1);
    }

    music_stop();
    ui_critic_remove();
    in_exit();
    scr_exit();
    sys_free(mem);
    return 0;
}
