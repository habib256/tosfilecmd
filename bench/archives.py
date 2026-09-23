#!/usr/bin/env python3
"""archives.py -- images disque et archives ouvertes comme des dossiers.

Sur la disquette de demonstration (DEMO\\ARCHIVES) : TEXTS.LZH, PICTURES.ZIP,
OLDIES.ARC et OLDDISK.MSA s'ouvrent par RETURN, montrent leur contenu en
8.3, se referment par ESC (la selection revient sur le fichier). On en
copie des fichiers et des dossiers vers C: : octet pour octet ce que
contiennent les archives (relu ici par zipfile, lha.py, arcpack.py et
fat12.py). Les visionneuses et la musique lisent directement dedans.
Ecrire dans une archive est refuse ; une archive abimee donne une erreur,
et un fichier au CRC faux n'est pas laisse a moitie ecrit. La disquette
n'est pas modifiee.
"""
import io
import os
import shutil
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tosfc import (TOSFC, Checks, DISK, ROOT, scratch, copy_disk, image,  # noqa: E402
                   hd_with_program, host_read, host_find)

sys.path.insert(0, os.path.join(ROOT, "tools"))
import stpic  # noqa: E402

ARCH = "A:\\DEMO\\ARCHIVES\\"


def screen_ram(st):
    return st.peek(st.long(0x44E), 32000)


def zip_member(data, name):
    return zipfile.ZipFile(io.BytesIO(data)).read(name)


def main():
    c = Checks("archives")
    work = scratch()
    hd = hd_with_program(os.path.join(work, "hd"))
    for sub in ("LZH", "ZIP", "ARC", "MSA", "BAD"):
        os.makedirs(os.path.join(hd, sub))
    disk = copy_disk(DISK, work)
    before = open(disk, "rb").read()
    img = image(DISK)
    lzh = img.read(ARCH + "TEXTS.LZH")
    zdata = img.read(ARCH + "PICTURES.ZIP")
    arc = img.read(ARCH + "OLDIES.ARC")
    msa = img.read(ARCH + "OLDDISK.MSA")

    # Une archive abimee et une au CRC faux, deposees sur C: (disque hote).
    open(os.path.join(hd, "BAD", "BROKEN.ZIP"), "wb").write(zdata[:len(zdata) // 3])
    crc = bytearray(lzh)
    crc[len(crc) - 400] ^= 0x55                    # dans les donnees de LONG.TXT
    open(os.path.join(hd, "BAD", "BADCRC.LZH"), "wb").write(bytes(crc))

    st = TOSFC(disk=disk, gemdos=hd)
    try:
        st.boot()
        # ---- LZH : ouverture, contenu, description ----
        st.go(0, "C:\\LZH\\")
        st.go(1, ARCH)
        st.open(1, "TEXTS.LZH")
        p = st.panel(1)
        c.check(p["path"] == ARCH + "TEXTS.LZH\\", "RETURN opens TEXTS.LZH as a folder (%s)" % p["path"])
        c.check(sorted(p["entries"]) == sorted(["..", "TEXTS", "LONG.TXT", "README.TXT"]),
                "the archive lists its files and folder (%s)" % p["entries"])
        c.check(p["free"] == "Read-only", "the panel says read-only (%s)" % p["free"])
        st.select(1, "..")
        c.check("LHA archive" in st.panel(1)["info"], "the info line describes the archive (%s)"
                % st.panel(1)["info"])

        # ---- copie de fichiers et d'un dossier vers C: ----
        st.tag(1, "README.TXT", "LONG.TXT")
        st.letter("c")
        c.check(st.dialog() and st.dialog()[0] == "Copy", "copy out of an archive asks first")
        st.hit("RETURN")
        st.wait_idle()
        c.check(st.dialog() is None, "no error while copying out of the LZH")
        want = {"README.TXT": b"This LHA archive was opened like a folder.\r\n"
                              b"Copy files out of it with F5: they are read back.\r\n"}
        c.check(host_read(hd, "C:\\LZH\\README.TXT") == want["README.TXT"], "README.TXT extracted exactly")
        c.check(len(host_read(hd, "C:\\LZH\\LONG.TXT") or b"") == 20000, "LONG.TXT extracted (20,000 bytes)")
        st.select(1, "TEXTS")
        st.letter("c")
        st.hit("RETURN")
        st.wait_idle()
        c.check(host_find(hd, "C:\\LZH\\TEXTS\\LOREM.TXT") is not None
                and host_find(hd, "C:\\LZH\\TEXTS\\LETTER.DOC") is not None,
                "a folder of the archive is copied with its files")
        lorem = img.read("A:\\DEMO\\TEXTS\\LOREM.TXT")
        c.check(host_read(hd, "C:\\LZH\\TEXTS\\LOREM.TXT") == lorem[:len(lorem) // 4] * 6,
                "LOREM.TXT from the archive is byte for byte the original")
        c.check(host_read(hd, "C:\\LZH\\TEXTS\\LETTER.DOC") == img.read("A:\\DEMO\\TEXTS\\LETTER.DOC"),
                "LETTER.DOC from the archive is byte for byte the original")

        # ---- ecriture refusee ----
        for key, title in (("d", "Delete"), ("r", "Rename"), ("k", "Make folder"), ("a", "Attributes"),
                           ("e", "Edit")):
            st.select(1, "README.TXT")
            st.letter(key)
            d = st.dialog()
            c.check(d and d[0] == title and "read-only" in " ".join(d[1]),
                    "%s is refused inside an archive" % title)
            st.hit("RETURN")
        st.focus(0)
        st.select(0, "README.TXT")
        st.letter("c")
        d = st.dialog()
        c.check(d and "read-only" in " ".join(d[1]), "copy into an archive is refused")
        st.hit("RETURN")
        st.focus(1)
        st.select(1, "README.TXT")
        st.letter("v")
        d = st.dialog()
        c.check(d and "read-only" in " ".join(d[1]), "move out of an archive is refused")
        st.hit("RETURN")

        # ---- visionneuse de texte dans l'archive ----
        st.select(1, "README.TXT")
        st.hit("RETURN")
        c.check("This LHA archive was opened like a folder." in st.text(),
                "the text viewer reads a file inside the archive")
        st.hit("ESC")

        # ---- ESC referme l'archive ----
        st.hit("ESC")
        p = st.panel(1)
        c.check(p["path"] == ARCH and p["entries"][p["cursor"]] == "TEXTS.LZH",
                "ESC at the top closes the archive, TEXTS.LZH selected")

        # ---- ZIP : sous-dossier, image, musique ----
        st.go(0, "C:\\ZIP\\")
        st.open(1, "PICTURES.ZIP")
        p = st.panel(1)
        c.check(sorted(p["entries"]) == sorted(["..", "PICTURES", "TESTCARD.PI1", "WELCOME.YM"]),
                "ZIP listing, long name made 8.3 (%s)" % p["entries"])
        st.open(1, "PICTURES")
        c.check(st.panel(1)["path"].endswith("PICTURES.ZIP\\PICTURES\\"),
                "a folder inside the ZIP opens (%s)" % st.panel(1)["path"])
        st.select(1, "RINGS.PC1")
        st.hit("RETURN")
        res, pal, bm = stpic.decode("RINGS.PC1", zip_member(zdata, "pictures/rings.pc1"))
        c.check(screen_ram(st) == bm, "a picture inside the ZIP is shown exactly")
        st.hit("ESC")
        st.select(1, "SUNSET.NEO")
        st.letter("c")
        st.hit("RETURN")
        st.wait_idle()
        c.check(host_read(hd, "C:\\ZIP\\SUNSET.NEO") == zip_member(zdata, "pictures/sunset.neo"),
                "a deflated file is extracted byte for byte")
        st.hit("ESC")
        st.select(1, "TESTCARD.PI1")
        st.letter("c")
        st.hit("RETURN")
        st.wait_idle()
        c.check(host_read(hd, "C:\\ZIP\\TESTCARD.PI1") == zip_member(zdata, "Test Card.pi1"),
                "'Test Card.pi1' extracted as TESTCARD.PI1")
        st.select(1, "WELCOME.YM")
        st.hit("RETURN")
        st.run(100)
        c.check(st.dialog() is None and st.screen()[24][79] == "♪",
                "a YM tune plays straight from the ZIP")
        st.letter("p")
        st.hit("ESC")

        # ---- ARC : les quatre methodes ----
        st.go(0, "C:\\ARC\\")
        st.open(1, "OLDIES.ARC")
        p = st.panel(1)
        names = ["READ.ME", "LOREM.TXT", "LONG.TXT", "SHORT.TXT"]
        c.check(sorted(p["entries"]) == sorted([".."] + names), "ARC listing (%s)" % p["entries"])
        st.letter("+")
        st.letter("c")
        st.hit("RETURN")
        st.wait_idle()
        c.check(st.dialog() is None, "no error while copying out of the ARC (%s)"
                % st.dialog_text().replace("\n", " | "))
        lng = image(DISK).read("A:\\DEMO\\TEXTS\\LONG.TXT")
        want = {"READ.ME": b"An ARC archive from the BBS days.\r\n",
                "LOREM.TXT": lorem[:len(lorem) // 4] * 5,
                "LONG.TXT": lng[:30000],
                "SHORT.TXT": b"Just one line.\r\n"}
        for n in names:
            c.check(host_read(hd, "C:\\ARC\\" + n) == want[n], "ARC: %s extracted exactly" % n)
        st.hit("ESC")

        # ---- MSA : une disquette dans un fichier ----
        st.go(0, "C:\\MSA\\")
        st.open(1, "OLDDISK.MSA")
        p = st.panel(1)
        c.check(sorted(p["entries"]) == sorted(["..", "GAMES", "README.TXT"]),
                "MSA image listing (%s)" % p["entries"])
        st.select(1, "..")
        c.check("image" in st.panel(1)["info"].lower(), "the info line describes the image (%s)"
                % st.panel(1)["info"])
        st.select(1, "GAMES")
        st.letter("c")
        st.hit("RETURN")
        st.wait_idle()
        c.check(host_read(hd, "C:\\MSA\\GAMES\\SCORES.TXT") == b"1. TOSFC  99999\r\n2. ST     12345\r\n",
                "a folder of the floppy image is copied out")
        st.select(1, "README.TXT")
        st.hit("RETURN")
        c.check("A floppy image, opened like a folder." in st.text(),
                "the text viewer reads a file on the floppy image")
        st.hit("ESC")
        st.hit("ESC")
        c.check(st.panel(1)["path"] == ARCH, "ESC closes the image")

        # ---- archives abimees ----
        st.go(1, "C:\\BAD\\")
        st.go(0, "C:\\MSA\\")
        st.open(1, "BROKEN.ZIP")
        d = st.dialog()
        c.check(d is not None and "damaged" in " ".join(d[1]).lower(),
                "a truncated ZIP gives an error (%s)" % (d,))
        st.hit("RETURN")
        c.check(st.panel(1)["path"] == "C:\\BAD\\", "and the panel stays where it was")
        st.open(1, "BADCRC.LZH")
        st.select(1, "LONG.TXT")
        st.letter("c")
        st.hit("RETURN")
        st.wait_idle()
        d = st.dialog()
        c.check(d is not None, "a CRC error while copying out is reported (%s)" % (d,))
        while st.dialog():
            st.hit("RETURN")
            st.wait_idle()
        c.check(host_find(hd, "C:\\MSA\\LONG.TXT") is None, "and no partial file is left behind")
        st.hit("ESC")
    finally:
        st.close()
    c.check(open(disk, "rb").read() == before, "the floppy is unchanged byte for byte")
    shutil.rmtree(work, ignore_errors=True)
    return c.done()


if __name__ == "__main__":
    sys.exit(main())
