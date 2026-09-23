#!/usr/bin/env python3
"""fastfdc_format.py -- reproduction pour NeoST : avec --fastfdc, apres un
formatage (Flopfmt, 80 pistes x 2 faces), TOSFC lit une memoire alteree.

Scenario (TOSFC demarre de C:, B: est une disquette 720 Ko jetable) :
  1. touche F, Format, B:, 720K : 160 appels XBIOS Flopfmt, puis ecriture et
     relecture des secteurs systeme ;
  2. touche F, Write, B: : ecrit C:\\SAMPLE.MSA sur B:. Le fichier est relu
     piste par piste sur C: (lecteur hote de NeoST).

Attendu : aucune erreur (c'est le cas sur l'hote, et dans NeoST sans
--fastfdc). Obtenu avec --fastfdc : "Floppy not written / Track 73 /
Unknown floppy format" -- une longueur de piste MSA lue juste avant (Fread de
2 octets, correct d'apres NEOST_GEMDOS_TRACE) vaut autre chose en memoire
quand TOSFC s'en sert. Sensible a la disposition memoire : ajouter ~150
octets de code a TOSFC fait disparaitre le symptome. Sans formatage
prealable, meme ecriture : aucune erreur.

    python3 bench/repro/fastfdc_format.py        # les deux modes
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from disktools import (TOSFC, DISK, scratch, copy_disk, hd_with_program,  # noqa: E402
                       fat12, data_disk, sample_disk, menu)


def run(fast, fmt=True):
    work = scratch()
    hd = hd_with_program(os.path.join(work, "hd"))
    disk_a = copy_disk(DISK, work, "A.ST")
    disk_b = os.path.join(work, "B.ST")
    open(disk_b, "wb").write(data_disk())
    open(os.path.join(hd, "SAMPLE.MSA"), "wb").write(fat12.msa(sample_disk()))
    st = TOSFC(disk=disk_a, diskb=disk_b, gemdos=hd, fastfdc=fast)
    try:
        st.boot()
        st.go(0, "C:\\")
        st.go(1, "A:\\")
        if fmt:
            menu(st, "f", "b")
            st.hit("RETURN")
            st.letter("f")
            st.wait_idle()
        st.focus(0)
        st.select(0, "SAMPLE.MSA")
        menu(st, "w", "b")
        st.letter("w")
        st.wait_idle()
        return st.dialog_text().replace("\n", " | ") or "no error"
    finally:
        st.close()


if __name__ == "__main__":
    for fast in (True, False):
        for fmt in (True, False):
            print("%-12s %-18s -> %s" % ("--fastfdc" if fast else "real FDC",
                                         "format then write" if fmt else "write only", run(fast, fmt)))
