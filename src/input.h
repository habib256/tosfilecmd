/*
 * input.h -- clavier (BIOS) et souris (paquets IKBD) sans le GEM.
 */
#ifndef INPUT_H
#define INPUT_H

/* Scancodes ST utiles. */
#define SC_ESC    0x01
#define SC_BACKSP 0x0e
#define SC_TAB    0x0f
#define SC_RETURN 0x1c
#define SC_SPACE  0x39
#define SC_F1     0x3b
#define SC_F2     0x3c
#define SC_F3     0x3d
#define SC_F4     0x3e
#define SC_F5     0x3f
#define SC_F6     0x40
#define SC_F7     0x41
#define SC_F8     0x42
#define SC_F9     0x43
#define SC_F10    0x44
#define SC_HOME   0x47
#define SC_UP     0x48
#define SC_LEFT   0x4b
#define SC_RIGHT  0x4d
#define SC_DOWN   0x50
#define SC_INSERT 0x52
#define SC_DELETE 0x53
#define SC_UNDO   0x61
#define SC_HELP   0x62
#define SC_ENTER  0x72

#define SHIFT_ANY 0x03
#define SHIFT_CTRL 0x04
#define SHIFT_ALT 0x08

enum { EV_NONE, EV_KEY, EV_CLICK, EV_RCLICK };

typedef struct {
    short type;
    short scan;     /* EV_KEY */
    short ascii;
    short shift;
    short x, y;     /* EV_CLICK : cellule */
} EVENT;

void in_init(void);
void in_exit(void);
/* Evenement suivant, sans attendre (EV_NONE sinon). Met a jour le pointeur. */
void in_poll(EVENT *e);
/* Attend un evenement ; l'ecran est rafraichi pendant l'attente. */
void in_wait(EVENT *e);
/* Pour les operations longues : 1 si ESC a ete frappe (les autres touches
 * sont ignorees). */
int in_escape(void);

#endif
