#!/usr/bin/env python3
"""mkdisk.py -- les disquettes de TOS File Cmd.

    mkdisk.py VERSION build/TOSFC.PRG dist/ [build/]

(build/ : la musique de demonstration fabriquee par le Makefile.)

fabrique :
  dist/TOSFC-<v>.st      720 Ko double face : TOSFC.PRG dans AUTO\\ (il demarre
                         avec la disquette) et a la racine (pour le relancer
                         depuis le bureau), READ_ME.TXT et le dossier DEMO\\ ;
  dist/TOSFC-<v>-SS.st   360 Ko simple face, pour le lecteur SF354 d'origine
                         du 520 ST : le meme contenu, sans BIG.BIN, MANUAL.TXT, LONG.TXT
                         ni deux des images.

Tout le contenu de demonstration est genere ici : aucun fichier tiers.
"""
import io
import os
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arcpack  # noqa: E402
import fat12  # noqa: E402
import lha  # noqa: E402
import stpic  # noqa: E402
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


def letter_doc():
    """Un court document 1st Word : ligne de format, styles, espaces
    elastiques ; TOSFC les retire a l'affichage."""
    ruler = b"\x1f9[" + b"." * 64 + b"]001\r\n"
    return (ruler +
            b"Dear Atari user,\r\n\r\n"
            b"This letter was written with \x1b\x811st Word Plus\x1b\x80, the word\r\n"
            b"processor\x1e\x1ethat came with many STs. Its files mix the text with\r\n"
            b"formatting codes: TOS File Cmd hides them and shows the words.\r\n\r\n"
            b"\x1f9[" + b"." * 40 + b"]001\r\n"
            b"Yours sincerely,\r\n    TOS File Cmd\r\n")


def long_text():
    lines = ["Line %05d of a long text file: scroll with the arrows, page with" % i +
             " Left/Right, jump with Home and Shift+Home." for i in range(1, 601)]
    return ("\r\n".join(lines) + "\r\n").encode("ascii")


def manual_text():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    text = open(os.path.join(here, "docs", "MANUAL.md"), encoding="utf-8").read()
    # Cadres et fleches Unicode du manuel -> ASCII lisible sur le ST.
    table = {"═": "=", "║": "|", "╔": "+", "╗": "+", "╚": "+", "╝": "+", "╤": "+",
             "╧": "+", "╟": "+", "╢": "+", "│": "|", "─": "-", "┴": "+", "✓": "*",
             "↓": "v", "↑": "^", "—": "--", "’": "'", "→": "->"}
    text = "".join(table.get(ch, ch) for ch in text)
    return text.encode("ascii", "replace").replace(b"\n", b"\r\n")


def demo_lzh():
    members = [("readme.txt", b"This LHA archive was opened like a folder.\r\n"
                              b"Copy files out of it with F5: they are read back.\r\n"),
               ("texts\\lorem.txt", LOREM.encode("ascii") * 6),
               ("texts\\letter.doc", letter_doc()),
               ("long.txt", long_text()[:20000])]
    return b"".join(lha.archive(n, d)[:-1] for n, d in members) + b"\0"


def demo_zip(build_dir):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for name, fmt, res, gen in (("Test Card.pi1", stpic.degas, 0, stpic.testcard),
                                    ("pictures/rings.pc1", stpic.degas_elite, 0, stpic.rings),
                                    ("pictures/sunset.neo", stpic.neochrome, 0, stpic.sunset)):
            pal, rows = gen()
            z.writestr(zipfile.ZipInfo(name, (1990, 1, 1, 12, 0, 0)), fmt(res, pal, rows),
                       zipfile.ZIP_DEFLATED)
        z.writestr(zipfile.ZipInfo("pictures/", (1990, 1, 1, 12, 0, 0)), b"")
        if build_dir:
            z.writestr(zipfile.ZipInfo("welcome.ym", (2026, 9, 23, 12, 0, 0)),
                       open(os.path.join(build_dir, "WELCOME.YM"), "rb").read(), zipfile.ZIP_STORED)
    return buf.getvalue()


def demo_arc():
    return arcpack.archive([("READ.ME", b"An ARC archive from the BBS days.\r\n", 2),
                            ("LOREM.TXT", LOREM.encode("ascii") * 5, 4),
                            ("LONG.TXT", long_text()[:30000], 8),
                            ("SHORT.TXT", b"Just one line.\r\n", 3)])


def demo_floppy():
    """Une disquette simple face compressee en MSA, avec un dossier."""
    f = fat12.Builder("360k", label="OLDDISK")
    f.add("README.TXT", b"A floppy image, opened like a folder.\r\n",
          date=dos_date(1988, 5, 5), time=dos_time(10, 0))
    f.mkdir("GAMES")
    f.add("GAMES\\SCORES.TXT", b"1. TOSFC  99999\r\n2. ST     12345\r\n",
          date=dos_date(1989, 12, 24))
    f.add("GAMES\\HISCORE.DOC", b"Press RETURN on a file to read it from the image.\r\n",
          date=dos_date(1989, 12, 24))
    return fat12.msa(f.build(), spt=9, sides=1)


def build(version, prg, geometry, build_dir=None):
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
    b.add("DEMO\\TEXTS\\LETTER.DOC", letter_doc(), date=dos_date(1988, 2, 29),
          time=dos_time(11, 11))
    b.mkdir("DEMO\\PICTURES")
    pics = [("TESTCARD.PI1", stpic.degas, 0, stpic.testcard),
            ("RINGS.PC1", stpic.degas_elite, 0, stpic.rings),
            ("MANDEL.PC3", stpic.degas_elite, 2, stpic.mandel)]
    if geometry == "720k":
        pics += [("SUNSET.NEO", stpic.neochrome, 0, stpic.sunset),
                 ("STRIPES.PI2", stpic.degas, 1, stpic.medium)]
    for name, fmt, res, gen in pics:
        pal, rows = gen()
        b.add("DEMO\\PICTURES\\" + name, fmt(res, pal, rows), date=dos_date(1990, 1, 1))
    if build_dir:
        b.mkdir("DEMO\\MUSIC")
        music = ["WELCOME.YM", "TOSFC.SND"] + (["PLAIN.SND"] if geometry == "720k" else [])
        for name in music:
            b.add("DEMO\\MUSIC\\" + name, open(os.path.join(build_dir, name), "rb").read(),
                  date=dos_date(2026, 9, 23))
    if geometry == "720k":
        bm, spal = stpic.spectrum_demo()
        b.add("DEMO\\PICTURES\\RAINBOW.SPC", stpic.spc(bm, spal), date=dos_date(1991, 4, 1))
        b.add("DEMO\\PICTURES\\RAINBOW.SPU", stpic.spu(bm, spal), date=dos_date(1991, 4, 1))
    b.mkdir("DEMO\\ARCHIVES")
    arcdate = dict(date=dos_date(1994, 6, 6), time=dos_time(12, 0))
    b.add("DEMO\\ARCHIVES\\TEXTS.LZH", demo_lzh(), **arcdate)
    b.add("DEMO\\ARCHIVES\\OLDIES.ARC", demo_arc(), **arcdate)
    if geometry == "720k":
        b.add("DEMO\\ARCHIVES\\PICTURES.ZIP", demo_zip(build_dir), **arcdate)
        b.add("DEMO\\ARCHIVES\\OLDDISK.MSA", demo_floppy(), **arcdate)
    if geometry == "720k":
        b.add("DEMO\\TEXTS\\MANUAL.TXT", manual_text())
        b.add("DEMO\\TEXTS\\LONG.TXT", long_text())
        b.add("DEMO\\BIG.BIN", bytes((k * 31 + (k >> 8)) & 0xFF for k in range(90000)),
              date=dos_date(1995, 5, 5))
    return b.build()


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    version, prg_path, out = sys.argv[1:4]
    build_dir = sys.argv[4] if len(sys.argv) == 5 else None
    prg = open(prg_path, "rb").read()
    os.makedirs(out, exist_ok=True)
    for geometry, suffix in (("720k", ""), ("360k", "-SS")):
        img = build(version, prg, geometry, build_dir)
        problems = fat12.Image(img).fsck()
        if problems:
            sys.exit("mkdisk: %s image is inconsistent: %s" % (geometry, problems))
        path = os.path.join(out, "TOSFC-%s%s.st" % (version, suffix))
        with open(path, "wb") as f:
            f.write(img)
        print("%s: %d bytes, fsck clean" % (path, len(img)))


if __name__ == "__main__":
    main()
