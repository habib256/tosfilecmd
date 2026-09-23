/*
 * flop.S -- appels XBIOS de disquette, et changement de disquette force.
 *
 * Floprd/Flopwr/Flopfmt prennent sept a neuf arguments : trop pour les
 * macros de tos.h (un registre par argument). Ici, fonctions C ordinaires
 * (arguments de 32 bits sur la pile, d2/a2 preserves comme l'attend GCC).
 *
 *   long x_floprd(void *buf, int dev, int sect, int track, int side, int count);
 *   long x_flopwr(const void *buf, int dev, int sect, int track, int side, int count);
 *   long x_flopfmt(void *buf, int dev, int spt, int track, int side);
 *   void x_mediach(int dev);
 */
        .text

        .globl  x_floprd, x_flopwr, x_flopfmt, x_mediach

/* Floprd(buf, filler, dev, sect, track, side, count) : XBIOS 8 (9 : Flopwr). */
x_floprd:
        moveq   #8,%d0
        bra.s   rw
x_flopwr:
        moveq   #9,%d0
rw:
        move.l  %d2,-(%sp)
        move.l  %a2,-(%sp)
        lea     12(%sp),%a1             /* a1 -> buf (1er argument) */
        move.w  22(%a1),-(%sp)          /* count : mot de poids faible */
        move.w  18(%a1),-(%sp)          /* side */
        move.w  14(%a1),-(%sp)          /* track */
        move.w  10(%a1),-(%sp)          /* sect */
        move.w  6(%a1),-(%sp)           /* dev */
        clr.l   -(%sp)                  /* filler */
        move.l  (%a1),-(%sp)            /* buf */
        move.w  %d0,-(%sp)
        trap    #14
        lea     20(%sp),%sp
        move.l  (%sp)+,%a2
        move.l  (%sp)+,%d2
        rts

/* Flopfmt(buf, filler, dev, spt, track, side, interlv, magic, virgin) : XBIOS 10. */
x_flopfmt:
        move.l  %d2,-(%sp)
        move.l  %a2,-(%sp)
        lea     12(%sp),%a1
        move.w  #0xe5e5,-(%sp)          /* virgin */
        move.l  #0x87654321,-(%sp)      /* magic */
        move.w  #1,-(%sp)               /* entrelacement */
        move.w  18(%a1),-(%sp)          /* side */
        move.w  14(%a1),-(%sp)          /* track */
        move.w  10(%a1),-(%sp)          /* spt */
        move.w  6(%a1),-(%sp)           /* dev */
        clr.l   -(%sp)                  /* filler */
        move.l  (%a1),-(%sp)            /* buf */
        move.w  #10,-(%sp)
        trap    #14
        lea     26(%sp),%sp
        move.l  (%sp)+,%a2
        move.l  (%sp)+,%d2
        rts

/*
 * Changement de disquette force (methode d'Atari, "mediach") : pendant un
 * Fopen sur le lecteur, hdv_mediach repond "change" et hdv_rw "disquette
 * changee" ; GEMDOS oublie alors ce qu'il savait du lecteur et relit le BPB
 * par hdv_bpb, qui remet les vecteurs d'origine. Au plus quatre essais.
 */
hdv_bpb     = 0x472
hdv_rw      = 0x476
hdv_mediach = 0x47e

x_mediach:
        move.l  %d2,-(%sp)
        move.l  %a2,-(%sp)
        move.w  14(%sp),%d0             /* dev : mot de poids faible */
        move.w  %d0,mydev
        add.b   #'A',%d0
        move.b  %d0,fspec
        moveq   #3,%d2
1:      pea     install(%pc)
        move.w  #38,-(%sp)              /* Supexec */
        trap    #14
        addq.l  #6,%sp
        clr.w   -(%sp)                  /* Fopen(fspec, 0) */
        pea     fspec
        move.w  #0x3d,-(%sp)
        trap    #1
        addq.l  #8,%sp
        tst.l   %d0
        bmi.s   2f
        move.w  %d0,-(%sp)              /* Fclose */
        move.w  #0x3e,-(%sp)
        trap    #1
        addq.l  #4,%sp
2:      pea     check(%pc)              /* encore installes ? on recommence */
        move.w  #38,-(%sp)
        trap    #14
        addq.l  #6,%sp
        tst.w   stillin
        dbeq    %d2,1b
        pea     restore(%pc)            /* dans tous les cas, on rend la main */
        move.w  #38,-(%sp)
        trap    #14
        addq.l  #6,%sp
        move.l  (%sp)+,%a2
        move.l  (%sp)+,%d2
        rts

install:
        move.l  hdv_bpb,oldbpb
        move.l  hdv_rw,oldrw
        move.l  hdv_mediach,oldmed
        move.l  #newbpb,hdv_bpb
        move.l  #newrw,hdv_rw
        move.l  #newmed,hdv_mediach
        rts

check:
        clr.w   stillin
        cmp.l   #newbpb,hdv_bpb
        bne.s   1f
        move.w  #1,stillin
1:      rts

restore:
        cmp.l   #newbpb,hdv_bpb
        bne.s   1f
        move.l  oldbpb,hdv_bpb
1:      cmp.l   #newrw,hdv_rw
        bne.s   2f
        move.l  oldrw,hdv_rw
2:      cmp.l   #newmed,hdv_mediach
        bne.s   3f
        move.l  oldmed,hdv_mediach
3:      rts

newbpb:                                 /* 4(sp) : lecteur */
        move.w  4(%sp),%d0
        cmp.w   mydev,%d0
        bne.s   1f
        move.l  oldbpb,hdv_bpb
        move.l  oldrw,hdv_rw
        move.l  oldmed,hdv_mediach
1:      move.l  oldbpb,%a0
        jmp     (%a0)

newmed:
        move.w  4(%sp),%d0
        cmp.w   mydev,%d0
        bne.s   1f
        moveq   #2,%d0                  /* MED_CHANGED */
        rts
1:      move.l  oldmed,%a0
        jmp     (%a0)

newrw:                                  /* Rwabs(rw, buf, count, recno, dev) : dev en 14(sp) */
        move.w  14(%sp),%d0
        cmp.w   mydev,%d0
        bne.s   1f
        moveq   #-14,%d0                /* E_CHNG */
        rts
1:      move.l  oldrw,%a0
        jmp     (%a0)

        .data
fspec:  .ascii  "X:\\X"
        .byte   0
        .even
        .bss
        .even
mydev:  .space  2
stillin: .space 2
oldbpb: .space  4
oldrw:  .space  4
oldmed: .space  4
