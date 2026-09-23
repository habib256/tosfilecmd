/*
 * tos.h -- appels systeme TOS (GEMDOS trap #1, BIOS trap #13, XBIOS trap #14).
 *
 * Aucune bibliotheque C : chaque appel empile ses arguments dans l'ordre
 * attendu par le TOS (mots ou longs), declenche la trappe et depile. Le TOS
 * detruit d0-d2/a0-a2 ; ils sont declares comme tels. Tous les arguments
 * passent par des registres ("r") : une operande memoire relative a sp serait
 * faussee par les empilements qui precedent.
 *
 * Ce fichier n'est inclus que par la cible Atari. Les tests hote remplacent
 * l'interface portable de sys.h par un faux GEMDOS (tests/fakedos.c).
 */
#ifndef TOS_H
#define TOS_H

#define TRAP_CLOBBERS "d1", "d2", "a0", "a1", "a2", "memory", "cc"

#define TRAP_W(t, n) ({                                                  \
    register long _r __asm__("d0");                                      \
    __asm__ volatile ("move.w %1,-(%%sp)\n\ttrap #" #t "\n\taddq.l #2,%%sp" \
        : "=r"(_r) : "i"(n) : TRAP_CLOBBERS);                            \
    _r; })

#define TRAP_WW(t, n, a) ({                                              \
    register long _r __asm__("d0"); short _a = (short)(a);               \
    __asm__ volatile ("move.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\t"       \
        "trap #" #t "\n\taddq.l #4,%%sp"                                 \
        : "=r"(_r) : "i"(n), "r"(_a) : TRAP_CLOBBERS);                   \
    _r; })

#define TRAP_WL(t, n, a) ({                                              \
    register long _r __asm__("d0"); long _a = (long)(a);                 \
    __asm__ volatile ("move.l %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\t"       \
        "trap #" #t "\n\taddq.l #6,%%sp"                                 \
        : "=r"(_r) : "i"(n), "r"(_a) : TRAP_CLOBBERS);                   \
    _r; })

#define TRAP_WWW(t, n, a, b) ({                                          \
    register long _r __asm__("d0"); short _a = (short)(a), _b = (short)(b); \
    __asm__ volatile ("move.w %3,-(%%sp)\n\tmove.w %2,-(%%sp)\n\t"       \
        "move.w %1,-(%%sp)\n\ttrap #" #t "\n\taddq.l #6,%%sp"            \
        : "=r"(_r) : "i"(n), "r"(_a), "r"(_b) : TRAP_CLOBBERS);          \
    _r; })

#define TRAP_WLW(t, n, a, b) ({                                          \
    register long _r __asm__("d0"); long _a = (long)(a); short _b = (short)(b); \
    __asm__ volatile ("move.w %3,-(%%sp)\n\tmove.l %2,-(%%sp)\n\t"       \
        "move.w %1,-(%%sp)\n\ttrap #" #t "\n\taddq.l #8,%%sp"            \
        : "=r"(_r) : "i"(n), "r"(_a), "r"(_b) : TRAP_CLOBBERS);          \
    _r; })

#define TRAP_WLWW(t, n, a, b, c) ({                                      \
    register long _r __asm__("d0"); long _a = (long)(a);                 \
    short _b = (short)(b), _c = (short)(c);                              \
    __asm__ volatile ("move.w %4,-(%%sp)\n\tmove.w %3,-(%%sp)\n\t"       \
        "move.l %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #" #t "\n\t"     \
        "lea 10(%%sp),%%sp"                                              \
        : "=r"(_r) : "i"(n), "r"(_a), "r"(_b), "r"(_c) : TRAP_CLOBBERS); \
    _r; })

#define TRAP_WWLL(t, n, a, b, c) ({                                      \
    register long _r __asm__("d0"); short _a = (short)(a);               \
    long _b = (long)(b), _c = (long)(c);                                 \
    __asm__ volatile ("move.l %4,-(%%sp)\n\tmove.l %3,-(%%sp)\n\t"       \
        "move.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #" #t "\n\t"     \
        "lea 12(%%sp),%%sp"                                              \
        : "=r"(_r) : "i"(n), "r"(_a), "r"(_b), "r"(_c) : TRAP_CLOBBERS); \
    _r; })

#define TRAP_WLLW(t, n, a, b, c) ({                                      \
    register long _r __asm__("d0"); long _a = (long)(a), _b = (long)(b); \
    short _c = (short)(c);                                               \
    __asm__ volatile ("move.w %4,-(%%sp)\n\tmove.l %3,-(%%sp)\n\t"       \
        "move.l %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #" #t "\n\t"     \
        "lea 12(%%sp),%%sp"                                              \
        : "=r"(_r) : "i"(n), "r"(_a), "r"(_b), "r"(_c) : TRAP_CLOBBERS); \
    _r; })

/* ---- GEMDOS ---- */
#define Pterm0()              TRAP_W(1, 0x00)
#define Dsetdrv(d)            TRAP_WW(1, 0x0e, d)
#define Dgetdrv()             ((short)TRAP_W(1, 0x19))
#define Fsetdta(p)            TRAP_WL(1, 0x1a, p)
#define Super(p)              TRAP_WL(1, 0x20, p)
#define Tgetdate()            ((unsigned short)TRAP_W(1, 0x2a))
#define Tgettime()            ((unsigned short)TRAP_W(1, 0x2c))
#define Fgetdta()             ((void *)TRAP_W(1, 0x2f))
#define Sversion()            ((unsigned short)TRAP_W(1, 0x30))
#define Dfree(buf, d)         TRAP_WLW(1, 0x36, buf, d)
#define Dcreate(p)            TRAP_WL(1, 0x39, p)
#define Ddelete(p)            TRAP_WL(1, 0x3a, p)
#define Dsetpath(p)           TRAP_WL(1, 0x3b, p)
#define Fcreate(p, a)         TRAP_WLW(1, 0x3c, p, a)
#define Fopen(p, m)           TRAP_WLW(1, 0x3d, p, m)
#define Fclose(h)             TRAP_WW(1, 0x3e, h)
#define Fread(h, n, b)        TRAP_WWLL(1, 0x3f, h, n, b)
#define Fwrite(h, n, b)       TRAP_WWLL(1, 0x40, h, n, b)
#define Fdelete(p)            TRAP_WL(1, 0x41, p)
#define Fseek(o, h, m)        TRAP_WLWW(1, 0x42, o, h, m)
#define Fattrib(p, w, a)      TRAP_WLWW(1, 0x43, p, w, a)
#define Dgetpath(b, d)        TRAP_WLW(1, 0x47, b, d)
#define Malloc(n)             TRAP_WL(1, 0x48, n)
#define Mfree(p)              TRAP_WL(1, 0x49, p)
#define Fsfirst(p, a)         TRAP_WLW(1, 0x4e, p, a)
#define Fsnext()              TRAP_W(1, 0x4f)
#define Frename(p1, p2)       TRAP_WWLL(1, 0x56, 0, p1, p2)
#define Fdatime(b, h, w)      TRAP_WLWW(1, 0x57, b, h, w)

/* ---- BIOS ---- */
#define Bconstat(d)           TRAP_WW(13, 1, d)
#define Bconin(d)             TRAP_WW(13, 2, d)
#define Setexc(v, a)          TRAP_WWL_SETEXC(v, a)
#define Drvmap()              ((unsigned long)TRAP_W(13, 10))
#define Kbshift(m)            TRAP_WW(13, 11, m)

/* Setexc(vecnum, addr) : un mot puis un long. */
#define TRAP_WWL_SETEXC(v, a) ({                                         \
    register long _r __asm__("d0"); short _v = (short)(v); long _a = (long)(a); \
    __asm__ volatile ("move.l %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\t"       \
        "move.w #5,-(%%sp)\n\ttrap #13\n\taddq.l #8,%%sp"                \
        : "=r"(_r) : "r"(_v), "r"(_a) : TRAP_CLOBBERS);                  \
    _r; })

/* ---- XBIOS ---- */
#define Physbase()            ((void *)TRAP_W(14, 2))
#define Logbase()             ((void *)TRAP_W(14, 3))
#define Getrez()              ((short)TRAP_W(14, 4))
#define Setscreen(l, p, r)    TRAP_WLLW(14, 5, l, p, r)
#define Setpalette(p)         TRAP_WL(14, 6, p)
#define Setcolor(n, c)        ((short)TRAP_WWW(14, 7, n, c))
#define Kbdvbase()            ((void *)TRAP_W(14, 34))
#define Cursconf(f, r)        TRAP_WWW(14, 21, f, r)
#define Vsync()               TRAP_W(14, 37)
#define Supexec(f)            TRAP_WL(14, 38, f)

/* Ikbdws(n, buf) : envoie n + 1 octets au clavier. */
#define Ikbdws(n, b) ({                                                  \
    register long _r __asm__("d0"); short _n = (short)(n); long _b = (long)(b); \
    __asm__ volatile ("move.l %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\t"       \
        "move.w #25,-(%%sp)\n\ttrap #14\n\taddq.l #8,%%sp"                \
        : "=r"(_r) : "r"(_n), "r"(_b) : TRAP_CLOBBERS);                  \
    _r; })

/* Xbtimer(timer, ctrl, data, vec) : trois mots puis un long. */
#define TRAP_WWWWL_XBTIMER(t, c, d, v) ({                                \
    register long _r __asm__("d0"); short _t = (short)(t), _c = (short)(c), _d = (short)(d); \
    long _v = (long)(v);                                                 \
    __asm__ volatile ("move.l %4,-(%%sp)\n\tmove.w %3,-(%%sp)\n\t"       \
        "move.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\tmove.w #31,-(%%sp)\n\t" \
        "trap #14\n\tlea 12(%%sp),%%sp"                                   \
        : "=r"(_r) : "r"(_t), "r"(_c), "r"(_d), "r"(_v) : TRAP_CLOBBERS); \
    _r; })
#define Mshrink(b, n)         TRAP_WWLL(1, 0x4a, 0, b, n)

/* Structure renvoyee par Kbdvbase : on n'utilise que mousevec. */
typedef struct {
    void (*midivec)(void);
    void (*vkbderr)(void);
    void (*vmiderr)(void);
    void (*statvec)(void);
    void (*mousevec)(void);
    void (*clockvec)(void);
    void (*joyvec)(void);
    void (*midisys)(void);
    void (*ikbdsys)(void);
} KBDVECS;

#endif
