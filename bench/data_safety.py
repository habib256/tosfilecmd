#!/usr/bin/env python3
"""data_safety.py -- les refus et les pannes, sur la vraie machine emulee.

Chaque cas verifie les octets conserves, pas seulement le message :
  - un TOSFC.BAK deja present bloque l'ecrasement, sans rien toucher ;
  - disque plein pendant un ecrasement : l'ancienne version revient, aucun
    reste, la disquette reste coherente (fsck) ;
  - disquette protegee en ecriture : la boite d'erreur critique de TOSFC
    (pas celle du GEM), Cancel, et l'image n'a pas change d'un octet ;
  - lecteur vide : "Drive not ready", Retry puis Cancel, retour aux lecteurs ;
  - un dossier copie dans son propre sous-dossier est refuse avant d'ecrire.
Volumes jetables uniquement.
"""
import hashlib
import os
import stat
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tosfc import (TOSFC, Checks, DISK, scratch, copy_disk, hd_with_program,  # noqa: E402
                   host_find, host_read, image, fat12)


def small_full_disk(path, free_target):
    """Une 360 Ko presque pleine, avec un petit BIG.BIN a ecraser."""
    b = fat12.Builder("360k", label="FULL")
    b.add("BIG.BIN", b"small original\r\n")
    total = 720 * 512 - (1 + 10 + 7) * 512 - 1024
    b.add("FILLER.DAT", bytes(total - free_target))
    with open(path, "wb") as f:
        f.write(b.build())
    return path


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def main():
    c = Checks("data_safety")
    work = scratch()
    hd = hd_with_program(os.path.join(work, "hd"))
    disk = copy_disk(DISK, work)
    orig = image(DISK)
    diskb = small_full_disk(os.path.join(work, "B.ST"), 40 * 1024)
    free_b = image(diskb)
    st = TOSFC(disk=disk, diskb=diskb, gemdos=hd)
    try:
        st.boot()

        # ---- TOSFC.BAK deja present ----
        with open(os.path.join(hd, "LOREM.TXT"), "wb") as f:
            f.write(b"host version")
        with open(os.path.join(hd, "TOSFC.BAK"), "wb") as f:
            f.write(b"someone's backup")
        st.hit("TAB")
        st.letter("l")                      # ramene le panneau gauche a C:\
        st.go(1, "A:\\DEMO\\TEXTS\\")
        st.go(0, "C:\\")
        st.select(1, "LOREM.TXT")
        st.letter("c")
        st.hit("RETURN")
        st.wait_dialog("File exists")
        st.letter("y")
        d = st.wait_dialog("Error")
        c.check("TOSFC.BAK already exists here" in d[1], "existing TOSFC.BAK blocks the overwrite")
        st.hit("RETURN")
        c.check(host_read(hd, "C:\\LOREM.TXT") == b"host version", "destination untouched")
        c.check(host_read(hd, "C:\\TOSFC.BAK") == b"someone's backup", "the old TOSFC.BAK untouched")

        # ---- disque plein pendant un ecrasement ----
        st.go(1, "A:\\DEMO\\")
        st.go(0, "B:\\")
        st.select(1, "BIG.BIN")
        st.letter("c")
        st.hit("RETURN")
        st.wait_dialog("File exists")
        st.letter("y")
        d = st.wait_dialog("Error")
        c.check("Disk full" in d[1], "disk full reported (%s)" % d[1])
        st.hit("RETURN")
        p = st.panel(0)
        c.check(p["entries"] == ["BIG.BIN", "FILLER.DAT"], "B: lists only its two files (%s)" % p["entries"])

        # ---- dossier dans lui-meme ----
        os.makedirs(os.path.join(hd, "T", "IN"))
        with open(os.path.join(hd, "T", "F.TXT"), "wb") as f:
            f.write(b"f")
        st.go(0, "C:\\")
        st.go(1, "C:\\T\\IN\\")
        st.select(0, "T")
        st.letter("c")
        st.hit("RETURN")
        d = st.wait_dialog("Error")
        c.check("Source and destination overlap" in d[1], "a folder cannot be copied into itself")
        st.hit("RETURN")
        c.check(os.listdir(os.path.join(hd, "T", "IN")) == [], "nothing was written inside it")

        # ---- lecteur vide ---- (fin de la premiere session : B: va changer)
        st.letter("q")
        st.hit("RETURN")
        st.run(100)
    finally:
        st.close()

    img = image(diskb)
    c.check(img.read("A:\\BIG.BIN") == b"small original\r\n", "old B:\\BIG.BIN restored byte for byte")
    c.check(img.find("A:\\TOSFC.BAK") is None, "no TOSFC.BAK left on B:")
    c.check(img.fsck() == [], "B: is consistent (%s)" % img.fsck())
    c.check(img.read("A:\\FILLER.DAT") == free_b.read("A:\\FILLER.DAT"), "other files on B: untouched")
    c.check(image(disk).read("A:\\DEMO\\BIG.BIN") == orig.read("A:\\DEMO\\BIG.BIN"), "source intact")

    # ---- disquette protegee en ecriture ----
    wp = copy_disk(DISK, work, "WP.ST")
    os.chmod(wp, stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)
    before = sha(wp)
    hd2 = hd_with_program(os.path.join(work, "hd2"))
    with open(os.path.join(hd2, "NEW.TXT"), "wb") as f:
        f.write(b"to be copied\r\n")
    st = TOSFC(disk=wp, gemdos=hd2)
    try:
        st.boot()
        st.go(1, "A:\\")
        st.select(0, "NEW.TXT")
        st.letter("c")
        st.press("RETURN")
        d = st.wait_dialog("Disk error")
        c.check("Disk is write-protected" in d[1] and "Drive A:" in d[1],
                "TOSFC's own critical-error box names the problem and the drive")
        st.letter("c")                      # Cancel
        d = st.wait_dialog("Error")
        c.check("write-protected" in " ".join(d[1]), "the copy then reports the failure (%s)" % d[1])
        st.hit("RETURN")
        c.check("NEW.TXT" not in st.panel(1)["entries"], "nothing appears on A:")

        # ---- lecteur vide : Retry puis Cancel ----
        st.focus(1)
        st.letter("l")
        st.select(1, "B:")
        st.press("RETURN")
        d = st.wait_dialog("Disk error")
        c.check("Drive not ready" in d[1] and "Drive B:" in d[1], "empty drive: Drive not ready")
        st.letter("r")
        d = st.wait_dialog("Disk error")
        c.check(d is not None, "Retry asks again while the drive is empty")
        st.letter("c")
        st.idle()
        d = st.dialog()
        if d and d[0] == "Error":
            st.hit("RETURN")
        c.check(st.panel(1)["path"] == "Drives", "back on the drive list")
        st.letter("q")
        st.hit("RETURN")
        st.run(100)
    finally:
        st.close()
    c.check(sha(wp) == before, "write-protected image unchanged, byte for byte")
    return c.done()


if __name__ == "__main__":
    sys.exit(main())
