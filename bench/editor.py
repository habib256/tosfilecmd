#!/usr/bin/env python3
"""editor.py -- l'editeur de texte, et ce qu'il ecrit vraiment sur le disque.

Taper, deplacer, couper une ligne, enregistrer (F10), quitter avec ou sans
enregistrer ; un fichier de 66 Ko rouvert et enregistre sans changement est
identique octet pour octet ; nouveau fichier ; refus des documents 1st Word,
des fichiers en lecture seule et des fichiers binaires ; disquette protegee
en ecriture : rien n'est perdu. Aucun TOSFC.$ED ni TOSFC.BAK ne reste.
"""
import hashlib
import os
import stat
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tosfc import TOSFC, Checks, DISK, scratch, copy_disk, image  # noqa: E402


def leftovers(img):
    return [p for p, _ in img.walk() if p.endswith("TOSFC.$ED") or p.endswith("TOSFC.BAK")]


def main():
    c = Checks("editor")
    work = scratch()
    disk = copy_disk(DISK, work)
    orig = image(DISK)
    st = TOSFC(disk=disk)
    try:
        st.boot()
        st.go(0, "A:\\DEMO\\TEXTS\\")

        # ---- modifier, enregistrer ----
        st.select(0, "SHORT.TXT")
        st.letter("e")
        r = st.screen()
        c.check("SHORT.TXT" in r[0] and "line 1" in r[0] and "CRLF" in r[0], "E opens the editor (%s)" % r[0].strip())
        c.check(r[1].startswith("Just one line."), "the text is shown")
        st.type("Hello, ")
        r = st.screen()
        c.check(r[1].startswith("Hello, Just one line.") and "*" in r[0], "typing inserts, the title marks the change")
        st.hit("RIGHT", shift=True)                  # fin de ligne
        st.hit("RETURN")
        st.type("Second line")
        c.check(st.screen()[2].startswith("Second line") and "line 2" in st.screen()[0], "RETURN opens a new line")
        st.hit("F10")
        c.check("Saved." in st.screen()[24], "F10 saves")
        st.hit("ESC")
        c.check(st.panel(0)["path"] == "A:\\DEMO\\TEXTS\\", "ESC closes an unmodified text at once")

        # ---- quitter sans enregistrer ----
        st.select(0, "LOREM.TXT")
        st.letter("e")
        st.hit("DELETE")
        st.hit("DELETE")
        st.hit("ESC")
        d = st.dialog()
        c.check(d and d[0] == "LOREM.TXT" and "[ Discard ]" in " ".join(d[1]), "ESC after a change asks")
        st.letter("d")
        c.check(st.panel(0)["path"] == "A:\\DEMO\\TEXTS\\", "Discard returns to the panels")

        # ---- couper une ligne (Ctrl+Y) ----
        st.select(0, "LOREM.TXT")
        st.letter("e")
        first = st.screen()[1].rstrip()
        st.key(0x15, ctrl=True)                    # Ctrl+Y (Y : scancode $15)
        st.idle()
        c.check(st.screen()[1].rstrip() != first and st.screen()[1].startswith("floppies"),
                "Ctrl+Y cuts the line (%s)" % st.screen()[1][:30])
        st.hit("ESC")
        st.letter("d")

        # ---- un gros fichier, enregistre tel quel ----
        st.select(0, "LONG.TXT")
        st.letter("e")
        st.hit("HOME", shift=True)
        r = st.screen()
        c.check("line 601" in r[0] and any("Line 00600" in x for x in r[18:24]),
                "Shift+Home shows the end of a 66 KB file (%s)" % r[0].strip())
        f = st.frame
        st.type("abcde")
        st.idle()
        c.check(st.frame - f < 150, "typing at the end of 66 KB stays quick (%d frames for 5 keys)" % (st.frame - f))
        for _ in range(5):
            st.press("BACKSPACE")
        st.idle()
        st.hit("F10")
        st.hit("ESC")

        # ---- nouveau fichier ----
        st.select(0, "..")
        st.letter("e")
        d = st.dialog()
        c.check(d and d[0] == "New file", "E on a folder asks for a new file name")
        st.type("notes.txt")
        st.hit("RETURN")
        st.type("New file")
        st.hit("RETURN")
        st.type("on the ST")
        st.hit("F10")
        st.hit("ESC")
        c.check("NOTES.TXT" in st.panel(0)["entries"], "the new file appears in the panel")

        # ---- refus ----
        for name, what in (("LETTER.DOC", "1st Word"), ("LOCKED.TXT", "read-only")):
            if name == "LOCKED.TXT":
                st.go(0, "A:\\DEMO\\")
            st.select(0, name)
            st.letter("e")
            d = st.dialog()
            c.check(d and d[0] == "Edit" and what in " ".join(d[1]), "%s is not edited" % name)
            st.hit("RETURN")
        st.go(0, "A:\\")
        st.select(0, "TOSFC.PRG")
        st.letter("e")
        d = st.dialog()
        c.check(d and "not a text file" in " ".join(d[1]), "a program is not edited")
        st.hit("RETURN")

        st.letter("q")
        st.press("RETURN")   # TOSFC s'en va : pas d'attente
        st.run(200)
    finally:
        st.close()

    img = image(disk)
    c.check(img.read("A:\\DEMO\\TEXTS\\SHORT.TXT") == b"Hello, Just one line.\r\nSecond line\r\n",
            "the saved file is right, CRLF kept (%r)" % img.read("A:\\DEMO\\TEXTS\\SHORT.TXT"))
    c.check(img.read("A:\\DEMO\\TEXTS\\LOREM.TXT") == orig.read("A:\\DEMO\\TEXTS\\LOREM.TXT"),
            "discarded changes never reached the disk")
    c.check(img.read("A:\\DEMO\\TEXTS\\LONG.TXT") == orig.read("A:\\DEMO\\TEXTS\\LONG.TXT"),
            "66 KB saved without change: identical, byte for byte")
    c.check(img.read("A:\\DEMO\\TEXTS\\NOTES.TXT") == b"New file\r\non the ST",
            "new file written in the panel's folder, with CRLF")
    c.check(leftovers(img) == [], "no TOSFC.$ED or TOSFC.BAK left (%s)" % leftovers(img))
    c.check(img.fsck() == [], "disk consistent (%s)" % img.fsck())

    # ---- disquette protegee ----
    wp = copy_disk(DISK, work, "WP.ST")
    os.chmod(wp, stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)
    before = hashlib.sha256(open(wp, "rb").read()).hexdigest()
    st = TOSFC(disk=wp)
    try:
        st.boot()
        st.go(0, "A:\\DEMO\\TEXTS\\")
        st.select(0, "SHORT.TXT")
        st.letter("e")
        st.type("x")
        st.press("F10")
        st.wait_dialog("Disk error")
        st.letter("c")
        d = st.wait_dialog("Error")
        c.check("Not saved" in " ".join(d[1]), "write-protected: 'Not saved', the text stays open")
        st.hit("RETURN")
        c.check("*" in st.screen()[0], "the edit is kept in memory")
        st.hit("ESC")
        st.letter("d")
    finally:
        st.close()
    c.check(hashlib.sha256(open(wp, "rb").read()).hexdigest() == before, "write-protected disk unchanged")
    return c.done()


if __name__ == "__main__":
    sys.exit(main())
