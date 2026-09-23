#!/usr/bin/env python3
"""viewers.py -- les visionneuses, verifiees au pixel et a la ligne.

Images (moniteur couleur puis monochrome) : pour chaque image de
DEMO\\PICTURES, la memoire video doit etre exactement le bitmap du fichier
(ou sa conversion, recalculee ici par tools/stpic.py), la resolution celle
de l'image, et chaque pixel de la capture NeoST la couleur de la palette.
L'album (fleches) fait suivre la selection du panneau ; ESC rend l'ecran
texte. Des images abimees donnent une erreur et sont sautees par l'album.

Texte : 1st Word sans ses codes, pages, fin, recherche, hexadecimal,
fichier vide, fichier binaire. Les visionneuses n'ecrivent rien : la
disquette est identique octet pour octet a la fin.
"""
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tosfc import (TOSFC, Checks, DISK, ROOT, scratch, copy_disk, image, fat12)  # noqa: E402

sys.path.insert(0, os.path.join(ROOT, "tools"))
import stpic  # noqa: E402
from ppm2png import read_ppm  # noqa: E402

PICS = ["MANDEL.PC3", "RINGS.PC1", "STRIPES.PI2", "SUNSET.NEO", "TESTCARD.PI1"]


def screen_ram(st):
    return st.peek(st.long(0x44E), 32000)


def shot_matches(st, res, pal, bm, work, name):
    """Chaque pixel visible a la couleur attendue (bordure basse res. :
    48 points a gauche, 29 lignes en haut dans NeoST)."""
    path = os.path.join(work, name + ".ppm")
    st.shot(path)
    w, h, px = read_ppm(path)
    rows = stpic.unplanar(res, bm)
    x0, y0 = (48, 29) if res == 0 else (0, 0)
    rgb = [bytes(stpic.st_rgb(c)) for c in pal]
    bad = 0
    for y in range(0, len(rows), 3):
        line = rows[y]
        for x in range(0, len(line), 3):
            i = ((y + y0) * w + x + x0) * 3
            if px[i:i + 3] != rgb[line[x]]:
                bad += 1
    return bad == 0


def pictures_colour(c, disk, work):
    img = image(DISK)
    st = TOSFC(disk=disk)
    try:
        st.boot()
        st.go(0, "A:\\DEMO\\PICTURES\\")
        st.select(0, PICS[0])
        st.letter("i")
        for k, name in enumerate(PICS):
            if k:
                st.hit("RIGHT")
            res, pal, bm = stpic.decode(name, img.read("A:\\DEMO\\PICTURES\\" + name))
            ram = screen_ram(st)
            shift = st.peek(0x44C, 1)[0]
            if res == 2:
                want, want_shift, want_pal = stpic.mono_to_medium(bm), 1, stpic.GRAY_PAL * 4
                c.check(ram == want and shift == 1,
                        "colour: %s (monochrome) shown in grey, medium resolution" % name)
                c.check(shot_matches(st, 1, want_pal, want, work, name),
                        "colour: %s pixels are white, grey and black" % name)
            else:
                c.check(ram == bm and shift == res,
                        "colour: %s bitmap on screen, resolution %d" % (name, res))
                c.check(shot_matches(st, res, pal, bm, work, name),
                        "colour: %s every pixel has its palette colour" % name)
        st.hit("RIGHT")                      # apres la derniere : on y reste
        c.check(screen_ram(st) == stpic.decode(PICS[-1], img.read(
            "A:\\DEMO\\PICTURES\\" + PICS[-1]))[2], "the album stops at the last picture")
        st.hit("ESC")
        p = st.panel(0)
        c.check(st.peek(0x44C, 1)[0] == 1 and p["path"] == "A:\\DEMO\\PICTURES\\",
                "ESC brings back the panels in medium resolution")
        c.check(p["entries"][p["cursor"]] == PICS[-1], "the selection followed the album")
        st.select(0, "RINGS.PC1")
        st.hit("RETURN")
        res, pal, bm = stpic.decode("RINGS.PC1", img.read("A:\\DEMO\\PICTURES\\RINGS.PC1"))
        c.check(screen_ram(st) == bm, "RETURN on a picture shows it")
        st.hit("LEFT")
        res, pal, bm = stpic.decode(PICS[0], img.read("A:\\DEMO\\PICTURES\\" + PICS[0]))
        c.check(screen_ram(st) == stpic.mono_to_medium(bm), "Left goes to the previous picture")
        st.hit("ESC")
    finally:
        st.close()


def pictures_mono(c, disk):
    img = image(DISK)
    st = TOSFC(disk=disk, mono=True)
    try:
        st.boot()
        st.go(0, "A:\\DEMO\\PICTURES\\")
        for name in ("TESTCARD.PI1", "STRIPES.PI2", "MANDEL.PC3"):
            st.select(0, name)
            st.hit("RETURN")
            res, pal, bm = stpic.decode(name, img.read("A:\\DEMO\\PICTURES\\" + name))
            want = bm if res == 2 else stpic.to_mono(res, pal, bm)
            c.check(screen_ram(st) == want,
                    "mono: %s %s" % (name, "as is" if res == 2 else "dithered like the reference"))
            st.hit("ESC")
        c.check(st.panel(0)["path"] == "A:\\DEMO\\PICTURES\\", "mono: back on the panels")
    finally:
        st.close()


def broken(c, work):
    """Une disquette B: avec des images abimees entre deux bonnes."""
    good = image(DISK).read("A:\\DEMO\\PICTURES\\TESTCARD.PI1")
    rings = image(DISK).read("A:\\DEMO\\PICTURES\\RINGS.PC1")
    b = fat12.Builder("360k")
    b.add("A.PI1", good)
    b.add("B.PC1", rings[:5000])                 # tronquee
    b.add("C.PI1", b"not a picture at all\r\n")  # pas une image
    b.add("D.PC1", rings)
    path = os.path.join(work, "BROKEN.ST")
    with open(path, "wb") as f:
        f.write(b.build())
    st = TOSFC(disk=copy_disk(DISK, work), diskb=path)
    try:
        st.boot()
        st.go(0, "B:\\")
        st.select(0, "B.PC1")
        st.hit("RETURN")
        d = st.dialog()
        c.check(d and "Picture file is damaged or truncated" in d[1], "a truncated PC1 is refused")
        st.hit("RETURN")
        c.check(st.panel(0)["path"] == "B:\\" and st.peek(0x44C, 1)[0] == 1,
                "and the panels come back")
        st.select(0, "C.PI1")
        st.hit("RETURN")
        d = st.dialog()
        c.check(d and "Not a Degas or NEOchrome picture" in d[1], "a text file named .PI1 is refused")
        st.hit("RETURN")
        st.select(0, "A.PI1")
        st.hit("RETURN")
        st.hit("RIGHT")
        res, pal, bm = stpic.decode("D.PC1", rings)
        c.check(screen_ram(st) == bm, "the album skips the damaged pictures")
        st.hit("ESC")
        p = st.panel(0)
        c.check(p["entries"][p["cursor"]] == "D.PC1", "and the selection lands on the one shown")
    finally:
        st.close()


def rows(st, a, b):
    return [r.rstrip() for r in st.screen()[a:b]]


def texts(c, disk):
    st = TOSFC(disk=disk)
    try:
        st.boot()
        st.go(0, "A:\\DEMO\\TEXTS\\")
        st.select(0, "LETTER.DOC")
        st.hit("RETURN")
        r = rows(st, 0, 9)
        size = len(image(DISK).read("A:\\DEMO\\TEXTS\\LETTER.DOC"))
        c.check("1st Word" in r[0] and "%d bytes" % size in r[0], "1st Word document recognised")
        c.check(r[1] == "Dear Atari user," and "\x1f" not in "".join(r) and "·" not in "".join(r),
                "format lines and control codes are hidden")
        c.check(r[3] == "This letter was written with 1st Word Plus, the word",
                "styles removed, words kept (%r)" % r[3])
        st.hit("ESC")

        st.select(0, "LONG.TXT")
        st.hit("RETURN")
        first = rows(st, 1, 24)
        c.check(first[0].startswith("Line 00001 of a long text file"), "LONG.TXT opens at its first line")
        c.check(all(len(x) <= 80 for x in first), "lines fit the 80 columns")
        st.hit("RIGHT")
        page2 = rows(st, 1, 24)
        c.check(page2[0] == first[22], "a page moves 22 lines, one line of overlap")
        st.hit("LEFT")
        c.check(rows(st, 1, 24) == first, "Left comes back to the same first page")
        st.hit("DOWN")
        c.check(rows(st, 1, 24)[0] == first[1], "Down scrolls one line")
        st.hit("UP")
        c.check(rows(st, 1, 24) == first, "Up scrolls back")
        st.hit("HOME", shift=True)
        r = st.screen()
        c.check("End" in r[0] and any("Line 00600" in x for x in r[1:24]), "Shift+Home shows the end")
        st.hit("HOME")
        c.check(rows(st, 1, 24) == first, "Home goes back to the top")
        st.letter("f")
        st.type("line 00300")
        st.hit("RETURN")
        c.check(rows(st, 1, 2)[0].startswith("Line 00300"), "F finds text, any case")
        st.letter("f")
        for _ in range(12):
            st.press("BACKSPACE")
        st.type("zzz")
        st.hit("RETURN")
        c.check("Not found: ZZZ" in st.screen()[24], "a missing text is reported")
        c.check(rows(st, 1, 2)[0].startswith("Line 00300"), "and the page does not move")
        st.letter("h")
        r = st.screen()
        # Ligne 300 : 299 lignes de 110 octets (108 + CRLF) avant elle.
        at = 299 * 110
        c.check("hex" in r[0] and r[1].startswith("%08X" % (at & ~15)),
                "H switches to hex at the same place (%s)" % r[1][:8])
        c.check("Line 00300" in r[1][61:77] + r[2][61:77], "hex shows the same bytes")
        st.letter("h")
        c.check(rows(st, 1, 2)[0].startswith("Line 00300"), "H again: back to text at the same line")
        st.hit("ESC")

        st.select(0, "EMPTY.TXT")
        st.hit("RETURN")
        r = st.screen()
        c.check("0 bytes" in r[0] and "End" in r[0], "an empty file opens")
        st.hit("ESC")

        st.go(0, "A:\\")
        st.select(0, "TOSFC.PRG")
        st.hit("RETURN")
        r = st.screen()
        c.check("TOSFC.PRG" in r[0] and r[1].startswith("`"), "a program is shown as text")
        for _ in range(3):
            st.hit("RIGHT")
        st.hit("HOME", shift=True)
        for _ in range(5):
            st.hit("UP")
        c.check("End" in st.screen()[0] or "%" in st.screen()[0], "binary text scrolls without trouble")
        st.letter("h")
        c.check(st.screen()[1][:8].isalnum(), "hex on a binary file")
        st.hit("ESC")
        c.check(st.panel(0)["path"] == "A:\\", "ESC returns to the panels")
    finally:
        st.close()


def main():
    c = Checks("viewers")
    work = scratch()
    disk = copy_disk(DISK, work)
    before = hashlib.sha256(open(disk, "rb").read()).hexdigest()
    pictures_colour(c, disk, work)
    pictures_mono(c, disk)
    texts(c, disk)
    c.check(hashlib.sha256(open(disk, "rb").read()).hexdigest() == before,
            "viewing wrote nothing: the disk is unchanged, byte for byte")
    broken(c, work)
    return c.done()


if __name__ == "__main__":
    sys.exit(main())
