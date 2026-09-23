#!/usr/bin/env python3
"""chiptune.py -- la musique de demonstration de TOSFC, composee ici.

    WELCOME.YM  YM5 entrelace, 50 Hz, dans une archive LHA -lh5- (comme les
                fichiers YM que l'on trouve partout) ;
    TOSFC.SND   un vrai SNDH : un petit lecteur 68000 independant de sa
                position, deux sous-morceaux, compresse ICE! par unice68.

La meme partition sert aux deux : quatre mesures La mineur, Fa, Do, Sol,
une melodie, une basse et des arpeges. Aucun son tiers.

    chiptune.py ym  out.ym          (archive LHA)
    chiptune.py asm out.S           (source 68000 du SNDH, a assembler)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lha  # noqa: E402

CLOCK = 2000000
STEP = 6                    # trames par double croche (125 BPM a 50 Hz)

CHORDS = [(57, 60, 64), (53, 57, 60), (48, 52, 55), (55, 59, 62)]   # Am F C G
MELODY = [
    76, 0, 72, 0, 74, 76, 0, 72, 69, 0, 0, 72, 71, 0, 69, 0,
    77, 0, 76, 0, 72, 0, 69, 72, 72, 0, 0, 0, 69, 0, 72, 0,
    76, 0, 79, 0, 76, 74, 72, 0, 67, 0, 72, 0, 76, 0, 0, 0,
    74, 0, 71, 0, 67, 0, 71, 74, 74, 0, 72, 0, 71, 0, 67, 0,
]
DRUMS = [1, 0, 0, 0, 2, 0, 0, 1, 1, 0, 1, 0, 2, 0, 0, 0] * 4   # 1 grosse caisse, 2 caisse claire


def period(note):
    f = 440.0 * 2 ** ((note - 69) / 12.0)
    return max(1, min(4095, int(round(CLOCK / (16 * f)))))


def frames(loops=2):
    """Registres 0..13 de chaque trame (YM5 : plus 2 registres a 0)."""
    out = []
    for loop in range(loops):
        for step in range(64):
            chord = CHORDS[step // 16]
            lead = MELODY[step]
            for t in range(STEP):
                r = [0] * 16
                # A : la melodie, attaque puis decroissance.
                if lead:
                    p = period(lead)
                    r[0], r[1] = p & 0xFF, p >> 8
                    r[8] = max(0, 13 - t)
                # B : la basse, une octave sous la fondamentale.
                p = period(chord[0] - 12)
                r[2], r[3] = p & 0xFF, p >> 8
                r[9] = 11 if t < 4 else 9
                # C : arpege sur l'accord, une note par trame ; ou batterie.
                drum = DRUMS[step]
                mixer = 0b00111000                 # tons A B C, pas de bruit
                if drum and t < 3:
                    r[6] = 28 if drum == 1 else 8
                    mixer = 0b00011100 if drum == 1 else 0b00011000
                    r[10] = 14 - 3 * t
                    p = period(36 if drum == 1 else 60)
                else:
                    p = period(chord[(step * STEP + t) % 3] + 12)
                    r[10] = 7
                r[4], r[5] = p & 0xFF, p >> 8
                r[7] = mixer
                r[13] = 0xFF                        # enveloppe : ne pas toucher
                out.append(r)
    return out


def ym5():
    fr = frames()
    n = len(fr)
    hdr = (b"YM5!LeOnArD!" + struct.pack(">IIH", n, 1, 0) + struct.pack(">IHIH", CLOCK, 50, 0, 0))
    strings = b"Welcome to TOS File Cmd\0TOSFC\0Composed for the TOSFC demo disk\0"
    data = bytearray(n * 16)
    for f, regs in enumerate(fr):
        for r in range(16):
            data[r * n + f] = regs[r]
    return hdr + strings + bytes(data) + b"End!"


def sndh_asm():
    """Le lecteur SNDH : independant de sa position (tout en (pc)), ecrit
    directement dans le YM2149, un compteur de pas que les bancs lisent."""
    lead = ",".join(str(period(n) if n else 0) for n in MELODY)
    bass = ",".join(str(period(c[0] - 12)) for c in CHORDS)
    arp = ",".join(str(period(n + 12)) for c in CHORDS for n in c)
    return """/* Genere par tools/chiptune.py : lecteur SNDH de la disquette TOSFC. */
        .text
start:  bra.w   init
        bra.w   exit
        bra.w   play
        .ascii  "SNDH"
        .ascii  "TITLTOSFC SNDH demo"
        .byte   0
        .ascii  "COMMTOSFC"
        .byte   0
        .ascii  "YEAR2026"
        .byte   0
        .ascii  "##02"
        .byte   0
        .ascii  "TC50"
        .byte   0
        .even
        .ascii  "HDNS"

/* d0 = sous-morceau : 1 la melodie et tout, 2 la basse et les arpeges. */
init:   lea     vars(%%pc),%%a0
        move.w  %%d0,(%%a0)
        clr.w   2(%%a0)
        clr.w   4(%%a0)
        clr.l   6(%%a0)
        lea     0xffff8800.w,%%a1
        move.b  #7,(%%a1)
        move.b  #0xf8,2(%%a1)            /* tons A B C, ports en sortie */
        rts

exit:   lea     0xffff8800.w,%%a1
        move.b  #8,(%%a1)
        clr.b   2(%%a1)
        move.b  #9,(%%a1)
        clr.b   2(%%a1)
        move.b  #10,(%%a1)
        clr.b   2(%%a1)
        move.b  #7,(%%a1)
        move.b  #0xff,2(%%a1)
        rts

/* vars : +0 sous-morceau, +2 trame dans le pas, +4 pas (0..63), +6 pas joues */
play:   lea     vars(%%pc),%%a0
        lea     0xffff8800.w,%%a1
        move.w  2(%%a0),%%d0            /* trame dans le pas */
        move.w  4(%%a0),%%d1            /* pas */
        /* Melodie sur A (sous-morceau 1 seulement ; 0 = silence) */
        moveq   #0,%%d2
        moveq   #0,%%d3                  /* volume */
        cmp.w   #1,(%%a0)
        bne.s   1f
        lea     lead(%%pc),%%a2
        move.w  %%d1,%%d4
        add.w   %%d4,%%d4
        move.w  0(%%a2,%%d4.w),%%d2
        beq.s   1f
        moveq   #13,%%d3
        sub.w   %%d0,%%d3
1:      move.b  #0,(%%a1)
        move.b  %%d2,2(%%a1)
        lsr.w   #8,%%d2
        move.b  #1,(%%a1)
        move.b  %%d2,2(%%a1)
        move.b  #8,(%%a1)
        move.b  %%d3,2(%%a1)
        /* Basse sur B */
        move.w  %%d1,%%d3
        lsr.w   #4,%%d3                  /* mesure 0..3 */
        add.w   %%d3,%%d3
        lea     bass(%%pc),%%a2
        move.w  0(%%a2,%%d3.w),%%d2
        move.b  #2,(%%a1)
        move.b  %%d2,2(%%a1)
        lsr.w   #8,%%d2
        move.b  #3,(%%a1)
        move.b  %%d2,2(%%a1)
        move.b  #9,(%%a1)
        move.b  #10,2(%%a1)
        /* Arpege sur C : une note de l'accord par trame */
        move.w  %%d1,%%d3
        lsr.w   #4,%%d3
        mulu    #6,%%d3                  /* 3 mots par accord */
        move.w  6(%%a0),%%d4
        add.w   %%d0,%%d4
        and.l   #0xffff,%%d4
        divu    #3,%%d4
        swap    %%d4                     /* reste 0..2 */
        add.w   %%d4,%%d4
        add.w   %%d4,%%d3
        lea     arp(%%pc),%%a2
        move.w  0(%%a2,%%d3.w),%%d2
        move.b  #4,(%%a1)
        move.b  %%d2,2(%%a1)
        lsr.w   #8,%%d2
        move.b  #5,(%%a1)
        move.b  %%d2,2(%%a1)
        move.b  #10,(%%a1)
        move.b  #7,2(%%a1)
        /* Trame suivante */
        addq.w  #1,%%d0
        cmp.w   #%(step)d,%%d0
        blt.s   3f
        moveq   #0,%%d0
        addq.w  #1,%%d1
        and.w   #63,%%d1
        addq.l  #1,6(%%a0)
3:      move.w  %%d0,2(%%a0)
        move.w  %%d1,4(%%a0)
        rts

        .even
        .globl  vars
vars:   .space  10
lead:   .word   %(lead)s
bass:   .word   %(bass)s
arp:    .word   %(arp)s
""" % dict(step=STEP, lead=lead, bass=bass, arp=arp)


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "ym":
        open(sys.argv[2], "wb").write(lha.archive("WELCOME.YM", ym5()))
    elif len(sys.argv) == 3 and sys.argv[1] == "asm":
        open(sys.argv[2], "w").write(sndh_asm())
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
