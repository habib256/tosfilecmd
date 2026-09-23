#!/usr/bin/env python3
"""ops.py -- les operations de J1, jouees au clavier et verifiees octet par octet.

C: est un dossier de l'hote (disque GEMDOS de NeoST) : TOSFC y demarre depuis
AUTO\\, et tout ce qu'il y ecrit se relit directement. A: est une copie jetable
de la disquette publiee : on la relit avec tools/fat12.py, fsck compris.

Copier (fichiers marques, fichier vide, arborescence), ecraser (non, puis
oui, sans TOSFC.BAK restant), renommer, creer un dossier, deplacer (vers un
autre lecteur, puis sur le meme), attributs, refus de supprimer un fichier
en lecture seule, supprimer une arborescence, ESC pendant une copie, trier.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tosfc import (TOSFC, Checks, DISK, scratch, copy_disk, hd_with_program,  # noqa: E402
                   host_find, host_read, image)


def main():
    c = Checks("ops")
    work = scratch()
    hd = hd_with_program(os.path.join(work, "hd"))
    disk = copy_disk(DISK, work)
    orig = image(DISK)
    st = TOSFC(disk=disk, gemdos=hd)
    try:
        st.boot()
        c.check(st.panel(0)["path"] == "C:\\", "boots from C: with C:\\ on the left")

        # ---- copier deux fichiers marques de A: vers C: ----
        st.go(1, "A:\\DEMO\\TEXTS\\")
        st.tag(1, "LOREM.TXT", "SHORT.TXT")
        p = st.panel(1)
        c.check(sorted(p["tagged"]) == ["LOREM.TXT", "SHORT.TXT"], "two files tagged")
        c.check(p["info"].startswith("2 tagged"), "info line counts the tags (%s)" % p["info"])
        st.letter("c")
        d = st.dialog()
        c.check(d and d[0] == "Copy" and "to C:\\" in "\n".join(d[1]), "copy asks for confirmation")
        st.hit("RETURN")
        st.wait_idle()
        lorem = orig.read("A:\\DEMO\\TEXTS\\LOREM.TXT")
        c.check(host_read(hd, "C:\\LOREM.TXT") == lorem, "LOREM.TXT copied byte for byte")
        c.check(host_read(hd, "C:\\SHORT.TXT") == orig.read("A:\\DEMO\\TEXTS\\SHORT.TXT"),
                "SHORT.TXT copied byte for byte")
        left = st.panel(0)
        c.check("LOREM.TXT" in left["entries"], "left panel re-read after the copy")
        c.check(st.panel(1)["tagged"] == [], "tags cleared after the copy")
        row = [r for r in st.screen()[2:21] if "LOREM" in r[:40]]
        c.check(row and "14/03/87" in row[0], "date kept on the copy (%s)" % (row[0][:40] if row else "?"))

        # ---- fichier vide ----
        st.select(1, "EMPTY.TXT")
        st.letter("c")
        st.hit("RETURN")
        st.wait_idle()
        c.check(host_read(hd, "C:\\EMPTY.TXT") == b"", "empty file copied")

        # ---- ecraser : non, puis oui ----
        with open(host_find(hd, "C:\\LOREM.TXT"), "wb") as f:
            f.write(b"CHANGED ON THE HOST")
        st.select(1, "LOREM.TXT")
        st.letter("c")
        st.hit("RETURN")
        d = st.wait_dialog("File exists")
        c.check(any("Existing:" in l and "19 bytes" in l for l in d[1]),
                "overwrite prompt shows the existing file")
        st.letter("n")
        st.wait_idle()
        c.check(host_read(hd, "C:\\LOREM.TXT") == b"CHANGED ON THE HOST", "N keeps the existing file")
        st.letter("c")
        st.hit("RETURN")
        st.wait_dialog("File exists")
        st.letter("y")
        st.wait_idle()
        c.check(host_read(hd, "C:\\LOREM.TXT") == lorem, "Y replaces it with the new copy")
        c.check(host_find(hd, "C:\\TOSFC.BAK") is None, "no TOSFC.BAK left after a good copy")

        # ---- arborescence ----
        st.hit("ESC")
        c.check(st.panel(1)["path"] == "A:\\DEMO\\", "ESC goes up to A:\\DEMO\\")
        c.check(st.panel(1)["entries"][st.panel(1)["cursor"]] == "TEXTS",
                "the cursor lands on the folder we came from")
        st.select(1, "NESTED")
        st.letter("c")
        st.hit("RETURN")
        st.wait_idle()
        c.check(host_read(hd, "C:\\NESTED\\LEVEL1\\LEVEL2\\DEEP.TXT") == b"Bottom of the tree.\r\n",
                "a tree is copied down to its deepest file")
        c.check(host_read(hd, "C:\\NESTED\\TOP.TXT") == b"Top of the tree.\r\n", "and its top file")

        # ---- renommer, creer un dossier ----
        st.select(0, "SHORT.TXT")
        st.letter("r")
        d = st.dialog()
        c.check(d and d[0] == "Rename", "R opens the rename box")
        for _ in range(9):
            st.press("BACKSPACE")
        st.type("note.txt")
        st.hit("RETURN")
        c.check(host_find(hd, "C:\\NOTE.TXT") and not host_find(hd, "C:\\SHORT.TXT"),
                "file renamed on the disk")
        p = st.panel(0)
        c.check(p["entries"][p["cursor"]] == "NOTE.TXT", "cursor follows the renamed file")
        st.letter("r")
        for _ in range(8):
            st.press("BACKSPACE")
        st.type("lorem.txt")
        st.hit("RETURN")
        d = st.dialog()
        c.check(d and d[0] == "Error" and "Name already exists" in d[1],
                "renaming onto an existing name is refused")
        st.hit("RETURN")
        c.check(host_find(hd, "C:\\NOTE.TXT") is not None, "and nothing moved")
        st.letter("k")
        st.type("work")
        st.hit("RETURN")
        c.check(os.path.isdir(host_find(hd, "C:\\WORK") or ""), "K makes a folder")
        p = st.panel(0)
        c.check(p["entries"][p["cursor"]] == "WORK", "cursor on the new folder")

        # ---- deplacer vers un autre lecteur ----
        st.select(0, "NOTE.TXT")
        st.letter("v")
        d = st.dialog()
        c.check(d and d[0] == "Move", "V asks before moving")
        st.hit("RETURN")
        st.wait_idle()
        c.check(host_find(hd, "C:\\NOTE.TXT") is None, "moved file left the source drive")
        c.check("NOTE.TXT" in st.panel(1)["entries"], "and appears on A:\\DEMO\\")

        # ---- deplacer sur le meme lecteur ----
        st.go(1, "C:\\WORK\\")
        st.select(0, "LOREM.TXT")
        st.letter("v")
        st.hit("RETURN")
        st.wait_idle()
        c.check(host_read(hd, "C:\\WORK\\LOREM.TXT") == lorem and not host_find(hd, "C:\\LOREM.TXT"),
                "move within C: renames into WORK")

        # ---- attributs et suppression refusee ----
        st.go(1, "A:\\DEMO\\TEXTS\\")
        st.select(1, "EMPTY.TXT")
        st.letter("a")
        d = st.dialog()
        c.check(d and d[0] == "Attributes" and "[ ] Read-only" in d[1], "A shows the attributes")
        st.letter("r")
        c.check("[x] Read-only" in st.dialog()[1], "R toggles read-only")
        st.letter("o")
        p = st.panel(1)
        c.check("Read-only" in p["info"], "info line shows Read-only (%s)" % p["info"])
        st.letter("d")
        d = st.dialog()
        c.check(d and d[0] == "Delete", "D asks before deleting")
        st.letter("d")
        d = st.wait_dialog("Error")
        c.check("File is read-only" in d[1], "a read-only file is not deleted")
        st.hit("RETURN")
        c.check("EMPTY.TXT" in st.panel(1)["entries"], "and is still listed")

        # ---- supprimer une arborescence ----
        st.select(0, "NESTED")
        st.letter("d")
        d = st.dialog()
        c.check(d and any("everything in them" in l for l in d[1]), "folder deletion warns")
        st.hit("RETURN")                     # le bouton par defaut est Cancel
        c.check(host_find(hd, "C:\\NESTED") is not None, "RETURN on the Delete box cancels")
        st.letter("d")
        st.letter("d")
        st.wait_idle()
        c.check(host_find(hd, "C:\\NESTED") is None, "tree deleted")

        # ---- ESC pendant une copie ----
        st.go(1, "A:\\DEMO\\")
        st.select(1, "BIG.BIN")
        st.letter("c")
        st.press("RETURN")
        st.wait_dialog("Copying")
        st.run(5)
        st.press("ESC")
        st.wait_idle()
        d = st.dialog()
        if d and d[0] == "Done":
            st.hit("RETURN")
        c.check(host_find(hd, "C:\\BIG.BIN") is None, "ESC stops the copy and removes the partial file")

        # ---- tri ----
        st.focus(0)
        st.letter("s")
        rows = st.screen()
        c.check("Ext↓" in rows[1][:40], "S sorts by extension")
        st.letter("s")
        c.check("Size↓" in st.screen()[1][:40], "then by size")

        # ---- relecture de la disquette ----
        st.letter("q")
        st.hit("RETURN")
        st.run(200)
    finally:
        st.close()
    img = image(disk)
    c.check(img.fsck() == [], "floppy is consistent after all this (%s)" % img.fsck())
    c.check(img.read("A:\\DEMO\\NOTE.TXT") == b"Just one line.\r\n", "moved file is on the floppy")
    c.check(img.find("A:\\DEMO\\TEXTS\\EMPTY.TXT")["attr"] & 1, "read-only bit written to the floppy")
    c.check(img.read("A:\\DEMO\\BIG.BIN") == orig.read("A:\\DEMO\\BIG.BIN"), "source of the cancelled copy intact")
    return c.done()


if __name__ == "__main__":
    sys.exit(main())
