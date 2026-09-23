/*
 * input.c -- clavier et souris.
 *
 * La souris : on remplace le gestionnaire de paquets IKBD du systeme par le
 * notre (crt0.S), qui cumule les deplacements. Le pointeur est une cellule
 * inversee ; il n'apparait qu'au premier mouvement. On remet le gestionnaire
 * d'origine en partant.
 */
#include "input.h"
#include "screen.h"
#include "tos.h"

extern short mouse_dx, mouse_dy, mouse_buttons;
extern void mouse_handler(void);

static KBDVECS *kbv;
static void (*old_mousevec)(void);
static short px, py, shown, last_buttons;

static long swap_in(void)
{
    old_mousevec = kbv->mousevec;
    kbv->mousevec = mouse_handler;
    return 0;
}

static long swap_out(void)
{
    kbv->mousevec = old_mousevec;
    return 0;
}

void in_init(void)
{
    kbv = Kbdvbase();
    px = 320;
    py = 200;
    Supexec(swap_in);
}

void in_exit(void)
{
    Supexec(swap_out);
}

static void mouse_update(EVENT *e)
{
    /* py est compte sur 400 lignes quelle que soit la resolution : une
     * cellule en fait toujours 16, et en moyenne resolution un deplacement
     * d'un point vaut une demi-ligne, comme la souris du bureau. */
    short dx = mouse_dx, dy = mouse_dy, b = mouse_buttons;
    mouse_dx -= dx;
    mouse_dy -= dy;
    if (dx || dy) {
        px += dx;
        py += dy;
        if (px < 0) px = 0;
        if (px > 639) px = 639;
        if (py < 0) py = 0;
        if (py > 399) py = 399;
        shown = 1;
    }
    /* Bit 1 = gauche, bit 0 = droit : on reagit a l'appui. */
    if ((b & 2) && !(last_buttons & 2)) {
        e->type = EV_CLICK;
        shown = 1;
    } else if ((b & 1) && !(last_buttons & 1)) {
        e->type = EV_RCLICK;
    }
    if (shown) scr_pointer(px >> 3, py >> 4);
    e->x = px >> 3;
    e->y = py >> 4;
    last_buttons = b;
}

void in_poll(EVENT *e)
{
    e->type = EV_NONE;
    mouse_update(e);
    if (e->type != EV_NONE) return;
    if (Bconstat(2)) {
        long k = Bconin(2);
        e->type = EV_KEY;
        e->scan = (short)((k >> 16) & 0xff);
        e->ascii = (short)(k & 0xff);
        e->shift = (short)(Kbshift(-1) & 0x0f);
    }
}

void in_wait(EVENT *e)
{
    for (;;) {
        scr_flush();
        in_poll(e);
        if (e->type != EV_NONE) return;
        Vsync();
    }
}

int in_escape(void)
{
    int esc = 0;
    while (Bconstat(2)) {
        long k = Bconin(2);
        if (((k >> 16) & 0xff) == SC_ESC) esc = 1;
    }
    return esc;
}
