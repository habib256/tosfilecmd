#!/usr/bin/env python3
"""mouse.py -- la souris IKBD : pointeur, selection, ouverture, marquage,
tri par les en-tetes, remontee par le chemin, barre des touches, boutons.

Les paquets relatifs arrivent par le serveur NeoST ; TOSFC compte la hauteur
sur 400 lignes quelle que soit la resolution (une cellule = 16 unites), la
largeur en pixels (une cellule = 8). On verifie la cellule du pointeur dans
ses variables (ptr_x, ptr_y), puis l'effet de chaque clic a l'ecran.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tosfc import TOSFC, Checks, DISK, scratch, copy_disk  # noqa: E402


class Pointer:
    def __init__(self, st):
        self.st = st
        self.x, self.y = 320, 200          # position de depart de TOSFC

    def to(self, col, row):
        tx, ty = col * 8 + 4, row * 16 + 8
        self.st.mouse(tx - self.x, ty - self.y)
        self.x, self.y = tx, ty
        self.st.idle()

    def click(self, col, row, right=False):
        self.to(col, row)
        b = 2 if right else 1               # serveur : bit 0 gauche, bit 1 droit
        self.st.cmd("mouse 0 0 %d" % b)
        self.st.run(3)
        self.st.cmd("mouse 0 0 0")
        self.st.idle()


def main():
    c = Checks("mouse")
    st = TOSFC(disk=copy_disk(DISK, scratch()))
    try:
        st.boot()
        m = Pointer(st)
        m.to(10, 5)
        c.check((st.var("ptr_x"), st.var("ptr_y")) == (10, 5), "pointer follows the mouse to cell (10,5)")
        m.to(79, 24)
        c.check((st.var("ptr_x"), st.var("ptr_y")) == (79, 24), "and reaches the bottom-right cell")

        m.click(5, 3)                        # DEMO
        p = st.panel(0)
        c.check(p["entries"][p["cursor"]] == "DEMO", "a click selects DEMO")
        m.click(5, 3)
        c.check(st.panel(0)["path"] == "A:\\DEMO\\", "a second click opens it")

        m.click(5, 4, right=True)            # premiere entree apres ".."
        p = st.panel(0)
        c.check(len(p["tagged"]) == 1, "a right click tags an entry (%s)" % p["tagged"])

        m.click(18, 1)                       # en-tete Size
        c.check("Size↓" in st.screen()[1][:40], "clicking the Size title sorts by size")
        m.click(4, 1)                        # en-tete Name
        c.check("Name↓" in st.screen()[1][:40], "clicking Name sorts by name again")

        m.click(20, 0)                       # le chemin
        c.check(st.panel(0)["path"] == "A:\\", "clicking the path goes up")

        m.click(50, 5)                       # panneau droit
        c.check(st.panel(1)["active"], "a click in the other panel activates it")

        m.click(1, 24)                       # ?Help
        d = st.dialog()
        c.check(st.text().count("TOS File Cmd 0.") == 1, "the key bar opens the help page")
        m.click(40, 12)
        c.check("Press any key" not in st.text(), "a click closes the help page")

        m.click(76, 24)                      # QQuit
        d = st.dialog()
        c.check(d and d[0] == "Quit", "Quit asks from the key bar")
        row = [y for y in range(25) if "[ No ]" in st.screen()[y]][0]
        col = st.screen()[row].index("[ No ]") + 2
        m.click(col, row)
        c.check(st.dialog() is None and st.running(), "clicking No keeps TOSFC running")
    finally:
        st.close()
    return c.done()


if __name__ == "__main__":
    sys.exit(main())
