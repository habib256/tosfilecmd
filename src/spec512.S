/*
 * spec512.S -- affichage Spectrum 512 : 48 couleurs par ligne.
 *
 * Installee dans la file VBL du TOS pendant l'affichage. A chaque trame :
 * attente du debut de l'ecran (compteur video), puis pour chacune des 200
 * lignes, 24 "move.l (a0)+,(aN)+" consecutifs -- 16 couleurs par 160 cycles,
 * soit une couleur tous les 10 points, exactement le rythme que suppose la
 * formule du format (pic_spectrum_slot). Trois registres d'adresse, un par
 * jeu de 16 couleurs, pour qu'aucune instruction ne s'intercale entre deux
 * jeux ; ils reviennent au debut de la palette pendant les 32 cycles de fin
 * de ligne. 512 cycles par ligne : l'ecran doit etre a 50 Hz.
 *
 * spec_pal : 200 lignes de 48 mots, dans l'ordre d'ecriture : jeux 2 et 3
 * de la ligne, puis jeu 1 de la ligne suivante (preparee par view.c).
 */
#ifndef SPEC_DELAY
#define SPEC_DELAY 66           /* nops avant la ligne 1 : regle au pixel par bench/viewers.py ; 65 ou 67 decalent */
#endif
#ifndef SYNC_K
#define SYNC_K 24
#endif

        .text
        .globl  spec_vbl
spec_vbl:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        move.w  %sr,-(%sp)
        move.w  #0x2700,%sr
        move.l  spec_pal,%a0
        lea     0xffff8240.w,%a4
        move.l  %a4,%a1
        move.l  %a4,%a2
        move.l  %a4,%a3
        /* Ligne 0 : palette noire (le jeu 1 de la ligne 1 arrive pendant
         * la ligne 0). */
        moveq   #0,%d0
        moveq   #7,%d1
1:      move.l  %d0,(%a4)+
        dbra    %d1,1b
        move.l  %a1,%a4

        /* Debut de l'ecran : l'octet bas du compteur video quitte 0. Chaque
         * octet lu vaut 2 cycles ; lsl absorbe l'ecart (8 + 2n cycles). */
        lea     0xffff8209.w,%a5
2:      move.b  (%a5),%d0
        beq.s   2b
        moveq   #SYNC_K,%d1
        sub.b   %d0,%d1
        lsl.l   %d1,%d1

        /* Pendant la ligne 0 (toute en couleur 0, noire) : le jeu 1 de la
         * ligne 1, puis l'attente qui cale la boucle sur la ligne 1. */
        lea     64(%a0),%a0
        .rept   8
        move.l  (%a0)+,(%a3)+
        .endr
        move.l  %a4,%a3
        .rept   SPEC_DELAY
        nop
        .endr

        move.w  #198,%d7
3:
        .rept   8
        move.l  (%a0)+,(%a1)+
        .endr
        .rept   8
        move.l  (%a0)+,(%a2)+
        .endr
        .rept   8
        move.l  (%a0)+,(%a3)+
        .endr
        move.l  %a4,%a1
        move.l  %a4,%a2
        move.l  %a4,%a3
        nop
        nop
        dbra    %d7,3b

        move.w  (%sp)+,%sr
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        rts

        .bss
        .even
        .globl  spec_pal
spec_pal:       .space 4
