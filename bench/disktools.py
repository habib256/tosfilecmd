#!/usr/bin/env python3
"""disktools.py -- outils de disquette (touche F) dans NeoST.

Premier passage : TOSFC demarre de C: (disque hote) ; A: et B: sont des
disquettes jetables.

  - lire B: en image sur C: : octet pour octet la disquette ;
  - formater B: (720 Ko) : le panneau ouvert sur B: la voit vide aussitot
    (GEMDOS a bien ete force a relire), l'image relue passe fsck ;
  - ecrire une image .MSA de C: sur B: : les fichiers apparaissent, la relecture
    est identique a l'image d'origine ;
  - 800 Ko sur une image 720 Ko : NeoST (comme Hatari) refuse de formater
    une geometrie differente de l'image, TOSFC le dit et n'en fait pas plus ;
  - copier A: sur B: : B: devient A: ; recommencer ecrase la copie (deux
    lecteurs : ce ne peut pas etre la meme disquette).
Second passage, sans C: : TOSFC demarre de A:, la disquette du programme ;
l'ecraser par une image, la formater, copier dessus : refuse, octet pour
octet intacte.
La copie a un seul lecteur (echanges) n'est verifiee que sur l'hote
(test_disk) : le protocole de NeoST ne sait pas changer de disquette.
"""
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tosfc import (TOSFC, Checks, DISK, ROOT, scratch, copy_disk,  # noqa: E402
                   hd_with_program, host_read, fat12)


def data_disk():
    b = fat12.Builder("720k", label="DATA")
    b.add("HELLO.TXT", b"A data floppy.\r\n")
    b.mkdir("WORK")
    b.add("WORK\\NOTES.TXT", bytes(range(256)) * 40)
    return b.build()


def sample_disk():
    b = fat12.Builder("720k", label="SAMPLE")
    b.add("SAMPLE.TXT", b"Written from an MSA image.\r\n")
    b.add("RANDOM.BIN", bytes((i * 7919) & 0xFF for i in range(50000)))
    return b.build()


def menu(st, button, drive=None):
    st.letter("f")
    d = st.dialog()
    if not d or d[0] != "Floppy":
        raise RuntimeError("no Floppy dialog: %s" % (d,))
    st.letter(button)
    if drive:
        st.letter(drive)


def read_to(st, drive, name):
    menu(st, "r", drive)
    d = st.dialog()
    if d and d[0] == "Read floppy":
        for _ in range(8):
            st.press("BACKSPACE")
        st.type(name)
        st.hit("RETURN")
    st.wait_idle()


def main():
    c = Checks("disktools")
    work = scratch()
    hd = hd_with_program(os.path.join(work, "hd"))
    disk_a = copy_disk(DISK, work, "A.ST")
    disk_b = os.path.join(work, "B.ST")
    data = data_disk()
    open(disk_b, "wb").write(data)
    sample = sample_disk()
    open(os.path.join(hd, "SAMPLE.MSA"), "wb").write(fat12.msa(sample))
    a_before = open(disk_a, "rb").read()

    # FDC a vitesse reelle : avec --fastfdc, une ecriture qui suit un
    # formatage lit une memoire alteree (signale a NeoST, voir TODO.md et
    # bench/repro/fastfdc_format.py).
    st = TOSFC(disk=disk_a, diskb=disk_b, gemdos=hd, fastfdc=False)
    try:
        st.boot()
        st.go(0, "C:\\")
        st.go(1, "A:\\")

        # ---- disquette -> image ----
        read_to(st, "b", "DISK.ST")
        c.check(st.dialog() is None, "reading B: gives no error (%s)" % st.dialog_text().replace("\n", " | "))
        c.check(host_read(hd, "C:\\DISK.ST") == data, "B: read to C:\\DISK.ST byte for byte")
        c.check("DISK.ST" in st.panel(0)["entries"], "the image shows up in the other panel")

        # ---- formatage, vu aussitot par GEMDOS ----
        st.go(1, "B:\\")
        c.check("HELLO.TXT" in st.panel(1)["entries"], "B: lists its files before formatting")
        menu(st, "f", "b")
        st.hit("RETURN")                         # 720K
        d = st.dialog()
        c.check(d and d[0] == "Format" and "Everything on it is lost." in d[1],
                "format asks for confirmation (%s)" % (d,))
        st.letter("f")
        st.wait_idle()
        c.check(st.dialog() is None, "format gives no error (%s)" % st.dialog_text().replace("\n", " | "))
        c.check(st.panel(1)["path"] == "B:\\" and st.panel(1)["entries"] == [],
                "the panel on B: is empty right after (%s)" % st.panel(1)["entries"])
        st.focus(1)
        read_to(st, "b", "FMT.ST")
        fmt = host_read(hd, "C:\\FMT.ST")
        img = fat12.Image(fmt) if fmt else None
        c.check(img is not None and not img.fsck() and img.spt == 9 and img.heads == 2,
                "the formatted floppy is a clean 720 KB FAT12 disk")
        c.check(img is not None and [p for p, e in img.walk()] == [], "and it is empty")
        c.check(fmt and fmt[8:11] != data[8:11], "with a new serial number")

        # ---- image .MSA -> disquette ----
        st.focus(0)
        st.select(0, "SAMPLE.MSA")
        menu(st, "w", "b")
        d = st.dialog()
        c.check(d and d[0] == "Write floppy" and "Everything on that floppy is replaced." in d[1],
                "writing asks for confirmation (%s)" % (d,))
        st.letter("w")
        st.wait_idle()
        c.check(st.dialog() is None, "writing gives no error (%s)" % st.dialog_text().replace("\n", " | "))
        c.check(sorted(st.panel(1)["entries"]) == ["RANDOM.BIN", "SAMPLE.TXT"],
                "B: shows the image's files (%s)" % st.panel(1)["entries"])
        st.focus(1)
        read_to(st, "b", "BACK.ST")
        c.check(host_read(hd, "C:\\BACK.ST") == sample, "B: read back equals the MSA image")

        # ---- geometrie refusee par le lecteur ----
        menu(st, "f", "b")
        st.hit("RIGHT")                          # 800K
        st.hit("RETURN")
        st.letter("f")
        st.wait_idle()
        d = st.dialog()
        c.check(d and "Floppy not formatted" in d[1] and "incomplete" in " ".join(d[1]),
                "800 KB on a 720 KB image: the error is shown (%s)" % (d,))
        st.hit("RETURN")

        # ---- copie A: -> B: ----
        menu(st, "d")
        st.letter("a")                           # A: to B:
        d = st.dialog()
        c.check(d and d[0] == "Disk copy" and "From drive A: to drive B:" in d[1],
                "disk copy names both drives (%s)" % (d,))
        st.letter("d")
        st.wait_idle()
        c.check(st.dialog() is None, "copy gives no error (%s)" % st.dialog_text().replace("\n", " | "))
        c.check("TOSFC.PRG" in st.panel(1)["entries"], "B: now lists the program disk's files")
        # Deux lecteurs : la cible peut etre une ancienne copie, on l'ecrase.
        menu(st, "d")
        st.letter("a")
        st.letter("d")
        st.wait_idle()
        c.check(st.dialog() is None, "copying again onto the copy works (%s)"
                % st.dialog_text().replace("\n", " | "))
    finally:
        st.close()
    c.check(open(disk_a, "rb").read() == a_before, "the source floppy A: is unchanged byte for byte")
    c.check(open(disk_b, "rb").read() == a_before, "B: is an exact copy of A:")
    program_disk(c, work)
    shutil.rmtree(work, ignore_errors=True)
    return c.done()


def program_disk(c, work):
    """Sans C: : TOSFC demarre du dossier AUTO de A:, qui est donc la
    disquette du programme. L'ecraser, la formater ou copier dessus : refuse."""
    disk_a = copy_disk(DISK, work, "PROG.ST")
    before = open(disk_a, "rb").read()
    b = fat12.Builder("720k", label="IMAGES")
    b.add("SAMPLE.MSA", fat12.msa(sample_disk()))
    disk_b = os.path.join(work, "IMAGES.ST")
    open(disk_b, "wb").write(b.build())
    st = TOSFC(disk=disk_a, diskb=disk_b, fastfdc=False)
    try:
        st.boot(frames=30000)
        st.go(0, "B:\\")
        st.select(0, "SAMPLE.MSA")
        menu(st, "w", "a")
        st.letter("w")
        st.wait_idle()
        d = st.dialog()
        c.check(d and "This is the TOS File Cmd disk" in " ".join(d[1]),
                "writing an image over the program disk is refused (%s)" % (d,))
        st.hit("RETURN")
        menu(st, "f", "a")
        st.hit("RETURN")
        st.letter("f")
        st.wait_idle()
        d = st.dialog()
        c.check(d and "This is the TOS File Cmd disk" in " ".join(d[1]),
                "formatting the program disk is refused (%s)" % (d,))
        st.hit("RETURN")
        menu(st, "d")
        st.letter("b")                           # B: to A:
        st.letter("d")
        st.wait_idle()
        d = st.dialog()
        c.check(d and "This is the TOS File Cmd disk" in " ".join(d[1]),
                "copying onto the program disk is refused (%s)" % (d,))
        st.hit("RETURN")
    finally:
        st.close()
    c.check(open(disk_a, "rb").read() == before, "the program disk is unchanged byte for byte")


if __name__ == "__main__":
    sys.exit(main())
