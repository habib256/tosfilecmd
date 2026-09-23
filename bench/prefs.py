#!/usr/bin/env python3
"""prefs.py -- Options : cacher les fichiers caches, enregistrer TOSFC.INF,
et tout retrouver au demarrage suivant, sur la meme disquette.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tosfc import TOSFC, Checks, DISK, scratch, copy_disk, image  # noqa: E402


def main():
    c = Checks("prefs")
    disk = copy_disk(DISK, scratch())
    st = TOSFC(disk=disk)
    try:
        st.boot()
        st.go(0, "A:\\DEMO\\")
        c.check("HIDDEN.TXT" in st.panel(0)["entries"], "hidden files shown by default")
        st.go(1, "A:\\DEMO\\TEXTS\\")
        st.focus(0)
        st.letter("s")
        st.letter("s")                       # nom -> ext -> taille
        st.letter("o")
        d = st.dialog()
        c.check(d and d[0] == "Options" and any("[x] Show hidden" in l for l in d[1]),
                "Options shows the hidden-files switch")
        st.letter("h")
        c.check(any("[ ] Show hidden" in l for l in st.dialog()[1]), "H switches it off")
        st.letter("s")
        d = st.wait_dialog("Options")
        c.check(d and "Settings saved." in d[1], "S saves the settings")
        st.hit("RETURN")
        st.hit("ESC")
        c.check("HIDDEN.TXT" not in st.panel(0)["entries"], "hidden files no longer listed")
        st.letter("q")
        st.press("RETURN")   # TOSFC s'en va : pas d'attente
        st.run(200)
    finally:
        st.close()
    img = image(disk)
    inf = img.read("A:\\TOSFC.INF")
    c.check(inf is not None and inf.startswith(b"TOSFC 1\r\n"), "TOSFC.INF written next to the program")
    c.check(inf and b"L=A:\\DEMO\\\r\n" in inf and b"H=0" in inf, "it holds the panels and options (%r)" % inf)
    c.check(img.find("A:\\TOSFC.NEW") is None, "no TOSFC.NEW left behind")
    c.check(img.fsck() == [], "disk consistent after saving")

    st = TOSFC(disk=disk)
    try:
        st.boot()
        l, r = st.panel(0), st.panel(1)
        c.check(l["path"] == "A:\\DEMO\\" and r["path"] == "A:\\DEMO\\TEXTS\\",
                "both panels reopen where they were (%s, %s)" % (l["path"], r["path"]))
        c.check("Size↓" in st.screen()[1][:40], "left panel sorted by size again")
        c.check("HIDDEN.TXT" not in l["entries"], "hidden files still hidden")
    finally:
        st.close()
    return c.done()


if __name__ == "__main__":
    sys.exit(main())
