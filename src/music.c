/*
 * music.c -- le lecteur de musique.
 *
 * YM : a chaque tick, les 14 registres de la trame courante sont ecrits dans
 * le YM2149, masques a leurs bits utiles. Le registre 7 garde ses bits de
 * port (6-7) a 1 : le port A commande la selection du lecteur de disquette
 * et le TOS le veut en sortie. Le registre 13 a $FF n'est pas ecrit (il
 * relancerait l'enveloppe). Les ports (14-15) ne sont jamais ecrits. Le TOS
 * masque les interruptions quand il touche au port A : nos ecritures ne
 * peuvent pas s'intercaler dans les siennes.
 *
 * SNDH : init (d0 = sous-morceau), puis play a chaque tick, exit a l'arret.
 * Le code du morceau s'execute tel quel : c'est un programme 68000.
 */
#include "music.h"
#include "fsops.h"
#include "lzh.h"
#include "ice.h"
#include "ym.h"
#include "sndh.h"
#include "tos.h"
#include "libc.h"

#define MAX_SONG   (256L * 1024)        /* fichier decompresse */
#define SNDH_EXTRA 32768L               /* BSS des lecteurs SNDH */

enum { K_NONE, K_YM, K_SNDH };

/* Partage avec music.S et les bancs. */
volatile short music_kind;
volatile short music_paused;
volatile unsigned long music_ticks;     /* ticks joues */
volatile long ym_frame;
unsigned char ym_shadow[14];            /* derniere valeur ecrite par registre */

extern void music_etv(void);
extern void music_timera(void);
extern void (*music_old_etv)(void);
extern long sndh_call(long entry, long d0);

static unsigned char *song;             /* memoire du morceau */
static YMSONG ym;
static SNDHINFO sn;
static unsigned char *sndh_base;
static int tune = 1;
static short use_timera, hooked;
static char title[48], author[48], format[12];

int music_is_name(const char *name)
{
    const char *d = strrchr(name, '.');
    if (!d) return 0;
    d++;
    return !strcmp(d, "YM") || !strcmp(d, "SND") || !strcmp(d, "SNDH");
}

/* ---- YM2149 ---- */

static void psg(int r, int v)
{
    *(volatile unsigned char *)0xffff8800L = (unsigned char)r;
    *(volatile unsigned char *)0xffff8802L = (unsigned char)v;
}

static long silence(void)
{
    psg(8, 0);
    psg(9, 0);
    psg(10, 0);
    psg(7, 0xff);                       /* tout coupe, ports en sortie */
    return 0;
}

static void ym_tick(void)
{
    static const unsigned char mask[14] = {
        0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0x3f,
        0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x0f };
    long f = ym_frame;
    int r;
    for (r = 0; r < 14; r++) {
        int v = ym_reg(&ym, f, r);
        if (r == 13 && v == 0xff) continue;
        v &= mask[r];
        if (r == 7) v |= 0xc0;
        ym_shadow[r] = (unsigned char)v;
        psg(r, v);
    }
    if (++f >= ym.frames) f = ym.loop;
    ym_frame = f;
}

void music_tick(void);
void music_tick(void)
{
    if (music_paused || music_kind == K_NONE) return;
    music_ticks++;
    if (music_kind == K_YM) ym_tick();
    else sndh_call((long)(sndh_base + 8), 0);
}

/* ---- Accroches ---- */

static long hook_on(void)
{
    if (use_timera) {
        /* 2457600 / 200 / hz : Timer A, predivision 200 (mode 7). */
        long hz = music_kind == K_YM ? ym.hz : sn.hz;
        long data = 12288L / hz;
        if (data < 1) data = 1;
        if (data > 255) data = 255;
        TRAP_WWWWL_XBTIMER(0, 7, data, music_timera);
    } else {
        music_old_etv = *(void (**)(void))0x400;
        *(void (**)(void))0x400 = music_etv;
    }
    hooked = 1;
    return 0;
}

/* Retire music_etv de la chaine etv_timer (XBRA). 0, ou -1 si introuvable. */
static long hook_off(void)
{
    if (!hooked) return 0;
    if (use_timera) {
        TRAP_WW(14, 26, 13);                            /* Jdisint : Timer A */
        *(volatile unsigned char *)0xfffffa19L = 0;     /* TACR : arret */
    } else {
        void (**p)(void) = (void (**)(void))0x400;
        int guard = 0;
        while (*p != music_etv) {
            long *h = (long *)*p;
            /* Un gestionnaire XBRA : "XBRA", id, ancien vecteur, code. */
            if (!h || h[-3] != 0x58425241L || ++guard > 32) return -1;
            p = (void (**)(void))&h[-1];
        }
        *p = music_old_etv;
    }
    hooked = 0;
    return 0;
}

/* ---- Chargement ---- */

static long sndh_init(void)
{
    sndh_call((long)sndh_base, tune);
    return 0;
}

static long sndh_exit(void)
{
    sndh_call((long)(sndh_base + 4), 0);
    return 0;
}

void music_stop(void)
{
    if (music_kind == K_NONE) return;
    music_paused = 1;
    if (Supexec(hook_off) < 0) {
        /* Un autre programme s'est accroche apres nous sans XBRA : on reste
         * en place mais muet. */
        music_kind = K_NONE;
        return;
    }
    if (music_kind == K_SNDH) Supexec(sndh_exit);
    Supexec(silence);
    music_kind = K_NONE;
    sys_free(song);
    song = 0;
}

void music_pause(void)
{
    if (music_kind == K_NONE) return;
    music_paused = !music_paused;
    if (music_paused) Supexec(silence);
}

int music_hooked(void) { return hooked; }

int music_state(void)
{
    if (music_kind == K_NONE) return MUS_NONE;
    return music_paused ? MUS_PAUSED : MUS_PLAYING;
}

static void start(void)
{
    music_ticks = 0;
    ym_frame = 0;
    music_paused = 0;
    Supexec(hook_on);
}

long music_play(const char *dir, const char *name)
{
    char path[PATH_MAX_TOSFC];
    unsigned char *raw, *data;
    long h, n = 0, r, size, cap;
    int kind;
    const SOURCE *src;

    music_stop();
    if (path_join(path, dir, name, 0)) return TE_TOOLONG;
    cap = sys_avail() - 65536L;
    if (cap > MAX_SONG) cap = MAX_SONG;
    if (cap < 16384) return ENSMEM;
    raw = sys_alloc(cap);
    if (!raw) return ENSMEM;
    src = src_of(path);                 /* image ou archive ouverte ? */
    h = src->open(src->ctx, path);
    if (h < 0) { sys_free(raw); return h; }
    for (;;) {
        r = src->read(src->ctx, h, cap - n > 32768L ? 32768L : cap - n, raw + n);
        if (r < 0) { src->close(src->ctx, h); sys_free(raw); return r; }
        if (r == 0) break;
        n += r;
        if (n >= cap) { src->close(src->ctx, h); sys_free(raw); return ENSMEM; }
    }
    src->close(src->ctx, h);
    /* Rendre ce qui depasse (un SNDH non compresse garde la place de sa BSS). */
    Mshrink(raw, n + SNDH_EXTRA < cap ? n + SNDH_EXTRA : cap);
    if (n + SNDH_EXTRA < cap) cap = n + SNDH_EXTRA;

    /* Decompression : LHA (YM) ou ICE! (SNDH). */
    data = raw;
    size = n;
    if (lzh_is_archive(raw, n)) {
        LZHENTRY e;
        void *work;
        if (lzh_entry(raw, n, 0, &e) != LZH_OK || e.size > MAX_SONG) {
            sys_free(raw);
            return TE_BADMUS;
        }
        data = sys_alloc(e.size + 16);
        work = sys_alloc(LZH_WORK_BYTES);
        if (!data || !work) { sys_free(data); sys_free(work); sys_free(raw); return ENSMEM; }
        r = lzh_extract(raw, n, &e, data, work);
        sys_free(work);
        sys_free(raw);
        if (r != LZH_OK) { sys_free(data); return TE_BADMUS; }
        size = e.size;
    } else if ((size = ice_size(raw, n)) > 0) {
        if (size > MAX_SONG) { sys_free(raw); return TE_BADMUS; }
        data = sys_alloc(size + SNDH_EXTRA);
        if (!data) { sys_free(raw); return ENSMEM; }
        r = ice_unpack(raw, n, data, size);
        sys_free(raw);
        if (r) { sys_free(data); return TE_BADMUS; }
    } else {
        size = n;
    }

    if (ym_parse(data, size, &ym) == YM_OK) {
        kind = K_YM;
        str_copy(title, ym.title, sizeof title);
        str_copy(author, ym.author, sizeof author);
        memcpy(format, ym.format, 4);
        format[4] = 0;
        use_timera = ym.hz != 50;
    } else if (sndh_parse(data, size, &sn) == SNDH_OK) {
        unsigned char *code = data;
        kind = K_SNDH;
        /* Le code a besoin de sa BSS apres lui : le tampon de lecture
         * (ou de decompression) en a garde la place. */
        if (data == raw && size + SNDH_EXTRA > cap) { sys_free(raw); return ENSMEM; }
        memset(code + size, 0, SNDH_EXTRA);
        sndh_base = code;
        str_copy(title, sn.title, sizeof title);
        str_copy(author, sn.composer, sizeof author);
        str_copy(format, "SNDH", sizeof format);
        use_timera = sn.hz != 50;
        tune = 1;
    } else {
        sys_free(data);
        return TE_BADMUS;
    }
    song = data;
    music_kind = (short)kind;
    music_paused = 1;
    if (kind == K_SNDH) Supexec(sndh_init);
    start();
    return 0;
}

void music_tune(int delta)
{
    int t;
    if (music_kind != K_SNDH) return;
    t = tune + delta;
    if (t < 1 || t > sn.tunes) return;
    music_paused = 1;
    Supexec(sndh_exit);
    Supexec(silence);
    tune = t;
    Supexec(sndh_init);
    music_ticks = 0;
    music_paused = 0;
}

const char *music_title(void) { return title; }
const char *music_author(void) { return author; }
const char *music_format(void) { return format; }
int music_tune_number(void) { return tune; }
int music_tunes(void) { return music_kind == K_SNDH ? sn.tunes : 1; }
unsigned long music_seconds(void)
{
    long hz = music_kind == K_YM ? ym.hz : sn.hz;
    return hz ? music_ticks / (unsigned long)hz : 0;
}
