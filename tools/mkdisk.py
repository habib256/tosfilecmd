#!/usr/bin/env python3
"""mkdisk.py -- les disquettes de TOS File Cmd.

    mkdisk.py VERSION build/TOSFC.PRG dist/

fabrique :
  dist/TOSFC-<v>.st      720 Ko double face : TOSFC.PRG dans AUTO\\ (il demarre
                         avec la disquette) et a la racine (pour le relancer
                         depuis le bureau), READ_ME.TXT et le dossier DEMO\\ ;
  dist/TOSFC-<v>-SS.st   360 Ko simple face, pour le lecteur SF354 d'origine
                         du 520 ST : le meme contenu, sans BIG.BIN.

Tout le contenu de demonstration est genere ici : aucun fichier tiers.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fat12  # noqa: E402
from fat12 import ATTR_HIDDEN, ATTR_RO, dos_date, dos_time  # noqa: E402

LOREM = (
    "The Atari ST was introduced in 1985. Its GEMDOS stores files on FAT12\r\n"
    "floppies, in directories of 8.3 names, with four attribute bits: read-only,\r\n"
    "hidden, system and archive.\r\n\r\n"
    "TOS File Cmd shows two folders side by side. Tag files with SPACE, then copy\r\n"
    "them to the other panel with C, or move them with V. Every copy is read back\r\n"
    "and compared with its source before a move deletes anything.\r\n"
)


def readme(version):
    return (
        "TOS File Cmd %s\r\n"
        "===================\r\n\r\n"
        "Two panels. One Atari ST.\r\n\r\n"
        "This disk starts TOS File Cmd from its AUTO folder. Quit with Q to reach\r\n"
        "the desktop; TOSFC.PRG at the root starts it again.\r\n\r\n"
        "  TAB      switch panels          RETURN  open a folder\r\n"
        "  ESC      go up                  SPACE   tag a file\r\n"
        "  C / V    copy / move            R       rename\r\n"
        "  D        delete                 K       make a folder\r\n"
        "  A        attributes             S       sort\r\n"
        "  ?        help                   Q       quit\r\n\r\n"
        "The DEMO folder is there to be copied, moved, renamed and deleted.\r\n"
        "Free software under the GNU GPL v3.\r\n" % version
    ).encode("ascii")


def build(version, prg, geometry):
    b = fat12.Builder(geometry, label="TOSFC")
    b.mkdir("AUTO")
    b.add("AUTO\\TOSFC.PRG", prg)
    b.add("TOSFC.PRG", prg)
    b.add("READ_ME.TXT", readme(version))
    b.mkdir("DEMO")
    b.mkdir("DEMO\\TEXTS")
    b.add("DEMO\\TEXTS\\LOREM.TXT", LOREM.encode("ascii") * 4,
          date=dos_date(1987, 3, 14), time=dos_time(9, 30))
    b.add("DEMO\\TEXTS\\SHORT.TXT", b"Just one line.\r\n",
          date=dos_date(1992, 11, 2), time=dos_time(18, 5))
    b.add("DEMO\\TEXTS\\EMPTY.TXT", b"", date=dos_date(2001, 1, 1))
    b.add("DEMO\\LOCKED.TXT", b"This file is read-only.\r\n", attr=ATTR_RO,
          date=dos_date(1989, 6, 1))
    b.add("DEMO\\HIDDEN.TXT", b"This file is hidden.\r\n", attr=ATTR_HIDDEN,
          date=dos_date(1990, 7, 8))
    b.mkdir("DEMO\\MANY")
    for i in range(1, 41):
        b.add("DEMO\\MANY\\FILE%02d.DAT" % i,
              bytes((i * 13 + k) & 0xFF for k in range(i * 37)),
              date=dos_date(1986 + i % 20, 1 + i % 12, 1 + i % 28),
              time=dos_time(i % 24, i % 60))
    b.mkdir("DEMO\\NESTED")
    b.mkdir("DEMO\\NESTED\\LEVEL1")
    b.mkdir("DEMO\\NESTED\\LEVEL1\\LEVEL2")
    b.add("DEMO\\NESTED\\TOP.TXT", b"Top of the tree.\r\n")
    b.add("DEMO\\NESTED\\LEVEL1\\MIDDLE.TXT", b"Middle of the tree.\r\n")
    b.add("DEMO\\NESTED\\LEVEL1\\LEVEL2\\DEEP.TXT", b"Bottom of the tree.\r\n")
    if geometry == "720k":
        b.add("DEMO\\BIG.BIN", bytes((k * 31 + (k >> 8)) & 0xFF for k in range(120000)),
              date=dos_date(1995, 5, 5))
    return b.build()


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    version, prg_path, out = sys.argv[1:]
    prg = open(prg_path, "rb").read()
    os.makedirs(out, exist_ok=True)
    for geometry, suffix in (("720k", ""), ("360k", "-SS")):
        img = build(version, prg, geometry)
        problems = fat12.Image(img).fsck()
        if problems:
            sys.exit("mkdisk: %s image is inconsistent: %s" % (geometry, problems))
        path = os.path.join(out, "TOSFC-%s%s.st" % (version, suffix))
        with open(path, "wb") as f:
            f.write(img)
        print("%s: %d bytes, fsck clean" % (path, len(img)))


if __name__ == "__main__":
    main()
