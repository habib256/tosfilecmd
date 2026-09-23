/*
 * ui.c -- boites de dialogue.
 *
 * Chaque boite sauve l'ecran qu'elle recouvre et le rend en partant : une
 * erreur disque peut surgir au milieu d'une copie, par-dessus la barre de
 * progression, sans que l'appelant ait a tout redessiner.
 */
#include "ui.h"
#include "screen.h"
#include "input.h"
#include "fsops.h"
#include "tos.h"
#include "libc.h"

#define SAVE_DEPTH 3
static unsigned char save_chr[SAVE_DEPTH][SCR_ROWS * SCR_COLS];
static unsigned char save_att[SAVE_DEPTH][SCR_ROWS * SCR_COLS];
static int save_top;
static short in_critic;

static void push_screen(void)
{
    if (save_top < SAVE_DEPTH) {
        memcpy(save_chr[save_top], scr_chr, sizeof scr_chr);
        memcpy(save_att[save_top], scr_att, sizeof scr_att);
    }
    save_top++;
}

static void pop_screen(void)
{
    save_top--;
    if (save_top < SAVE_DEPTH) {
        memcpy(scr_chr, save_chr[save_top], sizeof scr_chr);
        memcpy(scr_att, save_att[save_top], sizeof scr_att);
    }
    scr_touch();
}

/* Attente d'un evenement. Dans le gestionnaire critique on est dans un
 * appel BIOS : pas de Vsync, on se contente de sonder. */
static void wait_event(EVENT *e)
{
    for (;;) {
        scr_flush();
        in_poll(e);
        if (e->type != EV_NONE) return;
        if (!in_critic) Vsync();
    }
}

static void box_title(int x, int y, int w, const char *title, int attr)
{
    int l = (int)strlen(title);
    if (l > w - 4) l = w - 4;
    scr_putc(x + (w - l) / 2 - 1, y, ' ', attr);
    {
        int i;
        for (i = 0; i < l; i++) scr_putc(x + (w - l) / 2 + i, y, (unsigned char)title[i], attr);
    }
    scr_putc(x + (w - l) / 2 + l, y, ' ', attr);
}

int ui_dialog(const char *title, const char *const *lines, int nlines,
              const char *const *buttons, int nbuttons, int def)
{
    int w = (int)strlen(title) + 6, h, x, y, i, bw = 0, sel = def, bx0, res = -2;
    int bx[6];
    EVENT e;

    for (i = 0; i < nlines; i++)
        if ((int)strlen(lines[i]) + 4 > w) w = (int)strlen(lines[i]) + 4;
    for (i = 0; i < nbuttons; i++) bw += (int)strlen(buttons[i]) + 4 + (i ? 1 : 0);
    if (bw + 4 > w) w = bw + 4;
    if (w < 32) w = 32;
    if (w > SCR_COLS - 2) w = SCR_COLS - 2;
    h = nlines + 5;
    x = (SCR_COLS - w) / 2;
    y = (SCR_ROWS - h) / 2;

    push_screen();
    scr_box(x, y, w, h, A_DIALOG);
    box_title(x, y, w, title, A_DIALOG);
    for (i = 0; i < nlines; i++) scr_field(x + 2, y + 2 + i, w - 4, lines[i], A_DIALOG);
    bx0 = x + (w - bw) / 2;

    while (res == -2) {
        int bxp = bx0;
        for (i = 0; i < nbuttons; i++) {
            int a = (i == sel) ? A_DLGSEL : A_DIALOG;
            int l = (int)strlen(buttons[i]);
            bx[i] = bxp;
            scr_putc(bxp, y + h - 2, '[', A_DIALOG);
            scr_putc(bxp + 1, y + h - 2, ' ', a);
            scr_puts(bxp + 2, y + h - 2, buttons[i], a);
            scr_putc(bxp + 2 + l, y + h - 2, ' ', a);
            scr_putc(bxp + 3 + l, y + h - 2, ']', A_DIALOG);
            bxp += l + 5;
        }
        wait_event(&e);
        if (e.type == EV_KEY) {
            int c = toupper(e.ascii);
            if (e.scan == SC_ESC || e.scan == SC_UNDO) res = -1;
            else if (e.scan == SC_RETURN || e.scan == SC_ENTER) res = sel;
            else if (e.scan == SC_LEFT) sel = (sel + nbuttons - 1) % nbuttons;
            else if (e.scan == SC_RIGHT || e.scan == SC_TAB) sel = (sel + 1) % nbuttons;
            else
                for (i = 0; i < nbuttons; i++)
                    if (c && c == toupper((unsigned char)buttons[i][0])) res = i;
        } else if (e.type == EV_CLICK && e.y == y + h - 2) {
            for (i = 0; i < nbuttons; i++)
                if (e.x >= bx[i] && e.x < bx[i] + (int)strlen(buttons[i]) + 4) res = i;
        }
    }
    pop_screen();
    return res;
}

void ui_message(const char *title, const char *l1, const char *l2)
{
    const char *lines[2];
    static const char *const ok[] = { "OK" };
    lines[0] = l1;
    lines[1] = l2;
    ui_dialog(title, lines, l2 ? 2 : 1, ok, 1, 0);
}

int ui_confirm(const char *title, const char *l1, const char *l2, int def_yes)
{
    const char *lines[2];
    static const char *const yn[] = { "Yes", "No" };
    lines[0] = l1;
    lines[1] = l2;
    return ui_dialog(title, lines, l2 ? 2 : 1, yn, 2, def_yes ? 0 : 1) == 0;
}

int ui_input(const char *title, const char *prompt, char *buf, int cap)
{
    int w = 44, h = 7, x = (SCR_COLS - w) / 2, y = (SCR_ROWS - h) / 2;
    int len = (int)strlen(buf), pos = len, fw = w - 4, i, res = -1;
    EVENT e;

    push_screen();
    scr_box(x, y, w, h, A_DIALOG);
    box_title(x, y, w, title, A_DIALOG);
    scr_field(x + 2, y + 2, fw, prompt, A_DIALOG);
    scr_field(x + 2, y + 5, fw, "RETURN accept   ESC cancel", A_DIALOG);
    while (res < 0) {
        scr_field(x + 2, y + 3, fw, buf, A_DLGSEL);
        /* Curseur : la cellule sous la position d'insertion. */
        scr_putc(x + 2 + pos, y + 3, pos < len ? (unsigned char)buf[pos] : '_', A_DIALOG);
        wait_event(&e);
        if (e.type != EV_KEY) continue;
        if (e.scan == SC_ESC || e.scan == SC_UNDO) res = 0;
        else if (e.scan == SC_RETURN || e.scan == SC_ENTER) res = 1;
        else if (e.scan == SC_LEFT) { if (pos > 0) pos--; }
        else if (e.scan == SC_RIGHT) { if (pos < len) pos++; }
        else if (e.scan == SC_HOME) pos = (e.shift & SHIFT_ANY) ? len : 0;
        else if (e.scan == SC_BACKSP) {
            if (pos > 0) {
                for (i = pos - 1; i < len; i++) buf[i] = buf[i + 1];
                pos--; len--;
            }
        } else if (e.scan == SC_DELETE) {
            if (pos < len) {
                for (i = pos; i < len; i++) buf[i] = buf[i + 1];
                len--;
            }
        } else if (e.ascii >= 32 && e.ascii < 127 && len < cap - 1 && len < fw - 1) {
            for (i = len + 1; i > pos; i--) buf[i] = buf[i - 1];
            buf[pos++] = (char)toupper(e.ascii);
            len++;
        }
    }
    pop_screen();
    return res;
}

void ui_error(const char *what, const char *path, long err)
{
    const char *lines[3];
    static const char *const ok[] = { "OK" };
    lines[0] = what;
    lines[1] = path;
    lines[2] = err_text(err);
    ui_dialog("Error", lines, 3, ok, 1, 0);
}

/* ---- Progression ---- */

#define PW 60
static int pg_x, pg_y;

void ui_progress_open(const char *title)
{
    pg_x = (SCR_COLS - PW) / 2;
    pg_y = 9;
    push_screen();
    scr_box(pg_x, pg_y, PW, 7, A_DIALOG);
    box_title(pg_x, pg_y, PW, title, A_DIALOG);
    scr_field(pg_x + 2, pg_y + 5, PW - 4, "ESC stops after the current block", A_DIALOG);
    scr_flush();
}

void ui_progress(const char *name, unsigned long done, unsigned long total,
                 long files_done, long files_total)
{
    char line[64], num[16];
    int i, filled, bw = PW - 4;

    str_copy(line, name, sizeof line);
    scr_field(pg_x + 2, pg_y + 2, bw - 12, line, A_DIALOG);
    fmt_ulong(num, (unsigned long)files_done, 0);
    str_copy(line, num, sizeof line);
    str_add(line, "/", sizeof line);
    fmt_ulong(num, (unsigned long)files_total, 0);
    str_add(line, num, sizeof line);
    pad_left(num, line, 11);
    scr_field(pg_x + 2 + bw - 11, pg_y + 2, 11, num, A_DIALOG);
    /* Barre : total peut etre 0 (fichiers vides) ou depasse (taille
     * perimee dans le panneau). */
    if (total == 0) filled = files_total ? (int)(files_done * bw / files_total) : bw;
    else if (done >= total) filled = bw;
    else filled = (int)((done >> 8) * bw / ((total >> 8) + 1));
    for (i = 0; i < bw; i++)
        scr_putc(pg_x + 2 + i, pg_y + 3, i < filled ? G_BLOCK : G_SHADE, A_DIALOG);
    scr_flush();
}

void ui_progress_close(void)
{
    pop_screen();
}

/* ---- Erreurs critiques du BIOS ---- */

extern void critic_entry(void);
static long old_critic;

long critic_c(long err, long drive);
long critic_c(long err, long drive)
{
    char l1[48], l2[48];
    const char *lines[2];
    static const char *const rc[] = { "Retry", "Cancel" };
    static const char *const ok[] = { "OK" };
    int r;

    if (in_critic) return err;
    in_critic = 1;
    if (err == EOTHER) {
        /* Un seul lecteur : le TOS demande l'autre disquette. */
        str_copy(l1, "Insert disk ?: into drive A:", sizeof l1);
        l1[12] = (char)('A' + drive);
        lines[0] = l1;
        lines[1] = "then press RETURN.";
        ui_dialog("Change disk", lines, 2, ok, 1, 0);
        in_critic = 0;
        return 0;
    }
    str_copy(l1, err_text(err), sizeof l1);
    str_copy(l2, "Drive ?:", sizeof l2);
    l2[6] = (char)('A' + drive);
    lines[0] = l1;
    lines[1] = l2;
    r = ui_dialog("Disk error", lines, 2, rc, 2, 0);
    in_critic = 0;
    return r == 0 ? 0x10000L : err;
}

void ui_critic_install(void)
{
    old_critic = Setexc(0x101, critic_entry);
}

void ui_critic_remove(void)
{
    Setexc(0x101, old_critic);
}
