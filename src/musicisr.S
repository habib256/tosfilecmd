/*
 * music.S -- les interruptions du lecteur de musique.
 *
 * music_etv : accroche a etv_timer ($400), que le TOS appelle 50 fois par
 * seconde depuis le Timer C. En-tete XBRA : on peut s'en retirer meme si
 * un autre programme s'est accroche apres nous. On chaine vers l'ancien
 * gestionnaire par move.l/rts, la pile reste celle que le TOS a faite.
 *
 * music_timera : Timer A du MFP, pour les morceaux qui ne tournent pas a
 * 50 Hz (SNDH "TA100" par exemple). Fin d'interruption logicielle du MFP.
 *
 * Tous les registres sont sauves : le code d'un SNDH peut tout modifier.
 */
        .text
        .long   0x58425241              /* "XBRA" */
        .long   0x544f5346              /* "TOSF" */
        .globl  music_old_etv
music_old_etv:
        .long   0
        .globl  music_etv
music_etv:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        jsr     music_tick
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        move.l  music_old_etv,-(%sp)
        rts

        .globl  music_timera
music_timera:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        jsr     music_tick
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        bclr    #5,0xfffffa0f.w          /* ISRA : Timer A servi */
        rte

/*
 * long sndh_call(long entry, long d0) : appelle un point d'entree SNDH en
 * mode superviseur (init, exit ou play), tous registres preserves.
 */
        .globl  sndh_call
sndh_call:
        movem.l %d1-%d7/%a0-%a6,-(%sp)
        move.l  60(%sp),%a0
        move.l  64(%sp),%d0
        jsr     (%a0)
        movem.l (%sp)+,%d1-%d7/%a0-%a6
        rts
