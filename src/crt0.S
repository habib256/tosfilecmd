/*
 * crt0.S -- demarrage d'un programme TOS, gestionnaire souris et
 * gestionnaire d'erreurs critiques.
 *
 * Le GEMDOS donne tout le reste de la memoire au programme : on rend ce qui
 * depasse de la basepage + texte + donnees + BSS (Mshrink), on met la BSS a
 * zero soi-meme (le TOS 1.0 ne le garantit pas pour toutes les versions), et
 * on prend une pile a nous, dans la BSS : sa taille est connue et verifiee
 * au lien (tools/check_budget.py).
 */
        .text
        .globl  _start
_start:
        move.l  4(%sp),%a5              /* basepage */

        /* BSS a zero */
        move.l  24(%a5),%a0             /* p_bbase */
        move.l  28(%a5),%d0             /* p_blen */
        lsr.l   #1,%d0
        bra.s   2f
1:      clr.w   (%a0)+
2:      subq.l  #1,%d0
        bpl.s   1b

        move.l  %a5,basepage            /* apres la mise a zero */
        lea     stack_top,%sp

        /* Mshrink(0, basepage, 256 + texte + donnees + bss) */
        move.l  12(%a5),%d0
        add.l   20(%a5),%d0
        add.l   28(%a5),%d0
        add.l   #256,%d0
        move.l  %d0,-(%sp)
        move.l  %a5,-(%sp)
        clr.w   -(%sp)
        move.w  #0x4a,-(%sp)
        trap    #1
        lea     12(%sp),%sp

        jsr     main

        move.w  %d0,-(%sp)
        move.w  #0x4c,-(%sp)            /* Pterm */
        trap    #1

/*
 * Gestionnaire de paquets souris IKBD (Kbdvbase()->mousevec).
 * a0 pointe sur le paquet : en-tete $F8-$FB (bit 1 = gauche, bit 0 = droit),
 * dx, dy signes. On cumule les deplacements ; le C les consomme.
 */
        .globl  mouse_handler
mouse_handler:
        move.l  %d0,-(%sp)
        move.b  (%a0),%d0
        and.w   #3,%d0
        move.w  %d0,mouse_buttons
        move.b  1(%a0),%d0
        ext.w   %d0
        add.w   %d0,mouse_dx
        move.b  2(%a0),%d0
        ext.w   %d0
        add.w   %d0,mouse_dy
        move.l  (%sp)+,%d0
        rts

/*
 * etv_critic : appele par le BIOS avec 4(sp).w = erreur, 6(sp).w = lecteur.
 * On bascule sur une pile a nous (la pile superviseur du systeme est petite)
 * et on appelle long critic_c(long err, long drive). d0 = reponse :
 * $10000 = reessayer, sinon le code d'erreur a rendre.
 */
        .globl  critic_entry
critic_entry:
        movem.l %d1-%d7/%a0-%a6,-(%sp)
        move.w  60(%sp),%d0
        move.w  62(%sp),%d1
        ext.l   %d0
        ext.l   %d1
        move.l  %sp,critic_saved_sp
        lea     critic_stack_top,%sp
        move.l  %d1,-(%sp)
        move.l  %d0,-(%sp)
        jsr     critic_c
        move.l  critic_saved_sp,%sp
        movem.l (%sp)+,%d1-%d7/%a0-%a6
        rts

        .bss
        .even
        .globl  basepage, mouse_dx, mouse_dy, mouse_buttons
basepage:       .space 4
mouse_dx:       .space 2
mouse_dy:       .space 2
mouse_buttons:  .space 2
critic_saved_sp: .space 4
        .globl  stack_bottom
stack_bottom:   .space 16384
stack_top:
critic_stack:   .space 3072
critic_stack_top:
