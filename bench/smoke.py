#!/usr/bin/env python3
"""smoke.py -- la disquette publiee demarre-t-elle sur les panneaux ?

Sur la disquette 720 Ko (couleur et monochrome) et la 360 Ko simple face,
d'un ST de 512 Ko a un STE : TOSFC part du dossier AUTO, montre la racine
de A: a gauche et les lecteurs a droite, puis Q rend la main au systeme
dans la resolution et avec le fond d'origine.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tosfc import TOSFC, Checks, DISK, DISK_SS, scratch, copy_disk  # noqa: E402


SHOTS = scratch("tosfc-shots-")


def crashed(st):
    """Le gestionnaire d'exceptions d'EmuTOS range l'etat dans proc_lives
    ($380 = $12345678) avant d'afficher son ecran de panique."""
    return st.long(0x380) == 0x12345678


def session(c, label, disk, **kw):
    st = TOSFC(disk=disk, **kw)
    try:
        st.boot()
        left, right = st.panel(0), st.panel(1)
        c.check(left["path"] == "A:\\", "%s: left panel opens A:\\ (%s)" % (label, left["path"]))
        c.check(left["entries"][:4] == ["AUTO", "DEMO", "READ_ME.TXT", "TOSFC.PRG"],
                "%s: root of the disk listed and sorted (%s)" % (label, left["entries"]))
        c.check(right["path"] == "Drives" and "A:" in right["entries"],
                "%s: right panel lists the drives" % label)
        c.check(left["active"] and left["cursor"] == 0, "%s: left panel active on AUTO" % label)
        c.check("bytes free" in left["free"], "%s: free space shown (%s)" % (label, left["free"]))
        mono = st.var("scr_mono", 2)
        c.check(mono == (1 if kw.get("mono") else 0), "%s: resolution detected (mono=%d)" % (label, mono))
        rez_before = st.peek(0x44C, 1)[0]
        st.letter("q")
        c.check(st.dialog() and st.dialog()[0] == "Quit", "%s: Q asks before quitting" % label)
        st.hit("RETURN")
        st.run(300)
        c.check(not st.running(), "%s: TOSFC has returned to the system" % label)
        # Un plantage du TOS affiche "Panic" / "Crash" ; la sortie doit etre propre.
        st.shot(os.path.join(SHOTS, "smoke-after-quit.ppm"))
        c.check(st.long(0x4F2) != 0 and not crashed(st),
                "%s: no crash on the way out" % label)
        rez_after = st.peek(0x44C, 1)[0]
        c.check(rez_after == (2 if kw.get("mono") else 0),
                "%s: resolution restored (sshiftmd %d -> %d)" % (label, rez_before, rez_after))
    finally:
        st.close()


def main():
    c = Checks("smoke")
    d = scratch()
    session(c, "ST 1 MB colour", copy_disk(DISK, d))
    session(c, "ST 1 MB mono", copy_disk(DISK, d), mono=True)
    session(c, "520 ST 512 KB, 360 KB disk", copy_disk(DISK_SS, d, "SS.ST"), mem="512k")
    session(c, "STE 1 MB", copy_disk(DISK, d), machine="ste")
    return c.done()


if __name__ == "__main__":
    sys.exit(main())
