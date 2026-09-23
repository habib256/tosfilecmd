#!/usr/bin/env python3
"""stpic.py -- images Atari ST : fabrication (demo) et lecture (bancs).

Une image est une grille d'indices de couleur (liste de lignes) et une
palette de 16 registres ST. Formats : Degas .PI1/.PI2/.PI3, Degas Elite
compresse .PC1/.PC2/.PC3 (PackBits par ligne et par plan), NEOchrome .NEO.

    python3 tools/stpic.py show FILE      # resolution, palette, format
"""
import math
import struct
import sys

GEOM = {0: (320, 200, 4), 1: (640, 200, 2), 2: (640, 400, 1)}


def planar(res, pixels):
    """Grille d'indices -> 32000 octets planaires entrelaces."""
    w, h, planes = GEOM[res]
    out = bytearray(32000)
    for y in range(h):
        row = pixels[y]
        for g in range(w // 16):
            words = [0] * planes
            for i in range(16):
                v = row[g * 16 + i]
                for p in range(planes):
                    if v >> p & 1:
                        words[p] |= 0x8000 >> i
            if planes == 1:
                struct.pack_into(">H", out, y * 80 + g * 2, words[0])
            else:
                for p in range(planes):
                    struct.pack_into(">H", out, y * 160 + g * planes * 2 + p * 2, words[p])
    return bytes(out)


def unplanar(res, bm):
    w, h, planes = GEOM[res]
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            if planes == 1:
                row.append(bm[y * 80 + x // 8] >> (7 - x % 8) & 1)
                continue
            base = y * 160 + (x // 16) * planes * 2
            bit = 0x8000 >> (x % 16)
            v = 0
            for p in range(planes):
                if struct.unpack_from(">H", bm, base + p * 2)[0] & bit:
                    v |= 1 << p
            row.append(v)
        rows.append(row)
    return rows


def packbits(data):
    out = bytearray()
    i = 0
    while i < len(data):
        run = 1
        while i + run < len(data) and run < 128 and data[i + run] == data[i]:
            run += 1
        if run >= 3:
            out += bytes([257 - run, data[i]])
            i += run
            continue
        j = i
        lit = bytearray()
        while j < len(data) and len(lit) < 128:
            if j + 2 < len(data) and data[j] == data[j + 1] == data[j + 2]:
                break
            lit.append(data[j])
            j += 1
        out += bytes([len(lit) - 1]) + lit
        i = j
    return bytes(out)


def degas(res, pal, pixels):
    return struct.pack(">H16H", res, *pal) + planar(res, pixels)


def degas_elite(res, pal, pixels):
    bm = planar(res, pixels)
    w, h, planes = GEOM[res]
    out = bytearray(struct.pack(">H16H", 0x8000 | res, *pal))
    for y in range(h):
        line = bytearray()
        if planes == 1:
            line = bm[y * 80:(y + 1) * 80]
        else:
            for p in range(planes):
                for g in range(160 // (planes * 2)):
                    off = y * 160 + g * planes * 2 + p * 2
                    line += bm[off:off + 2]
        out += packbits(bytes(line))
    return bytes(out) + bytes(32)          # zones d'animation, vides


def neochrome(res, pal, pixels):
    hdr = bytearray(128)
    struct.pack_into(">HH16H", hdr, 0, 0, res, *pal)
    hdr[36:48] = b"TOSFC DEMO  "
    return bytes(hdr) + planar(res, pixels)


def unpackbits(src, pos, need):
    out = bytearray()
    while len(out) < need:
        c = src[pos]
        pos += 1
        if c < 128:
            out += src[pos:pos + c + 1]
            pos += c + 1
        elif c != 128:
            out += bytes([src[pos]]) * (257 - c)
            pos += 1
    return bytes(out), pos


def decode(name, data):
    """-> (res, palette, bitmap planaire de 32000 octets)."""
    ext = name.upper().rsplit(".", 1)[-1]
    if ext == "NEO":
        res = struct.unpack_from(">H", data, 2)[0]
        pal = list(struct.unpack_from(">16H", data, 4))
        return res, [c & 0xFFF for c in pal], data[128:128 + 32000]
    res = struct.unpack_from(">H", data, 0)[0]
    pal = [c & 0xFFF for c in struct.unpack_from(">16H", data, 2)]
    if not res & 0x8000:
        return res, pal, data[34:34 + 32000]
    res &= 3
    w, h, planes = GEOM[res]
    bm = bytearray(32000)
    pos = 34
    for y in range(h):
        line, pos = unpackbits(data, pos, 80 if planes == 1 else 160)
        if planes == 1:
            bm[y * 80:(y + 1) * 80] = line
            continue
        bpp = 160 // planes
        for p in range(planes):
            for g in range(bpp // 2):
                off = y * 160 + g * planes * 2 + p * 2
                bm[off:off + 2] = line[p * bpp + g * 2:p * bpp + g * 2 + 2]
    return res, pal, bytes(bm)


def st_rgb(c):
    """Registre ST/STE -> (r, g, b) 8 bits, comme NeoST (et Hatari)."""
    def ex(c4):
        v = ((c4 & 7) << 1) | ((c4 & 8) >> 3)
        return v | (v << 4)
    return ex(c >> 8 & 15), ex(c >> 4 & 15), ex(c & 15)


# ---- references des conversions de TOSFC (src/picture.c) ----

BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def luma(c):
    r, g, b = c >> 8 & 7, c >> 4 & 7, c & 7
    return (r * 5 + g * 9 + b * 2) * 15 // 112


def to_mono(res, pal, bm):
    """Image couleur -> 32000 octets monochromes, trame de Bayer 4 x 4."""
    rows = unplanar(res, bm)
    lum = [luma(c) for c in pal]
    out = bytearray(32000)
    for y in range(400):
        src = rows[y // 2]
        for x in range(640):
            sx = x // 2 if res == 0 else x
            if lum[src[sx]] * 32 < (2 * BAYER[y & 3][x & 3] + 1) * 15:
                out[y * 80 + x // 8] |= 0x80 >> (x % 8)
    return bytes(out)


def mono_to_medium(bm):
    """Image monochrome -> moyenne resolution : 0, 1 ou 2 points noirs par
    paire de lignes donnent les couleurs 0, 1 et 3."""
    rows = unplanar(2, bm)
    pix = [[rows[2 * y][x] + rows[2 * y + 1][x] for x in range(640)] for y in range(200)]
    pix = [[3 if v == 2 else v for v in row] for row in pix]
    return planar(1, pix)


GRAY_PAL = [0x777, 0x444, 0x444, 0x000]


# ---- Spectrum 512 ----

def spectrum_slot(x, c):
    x1 = 10 * c + (-5 if c & 1 else 1)
    if x >= x1 + 160:
        return c + 32
    if x >= x1:
        return c + 16
    return c


def spectrum_rgb(bm, pals):
    """Image attendue a l'ecran : 200 lignes de 320 (r, g, b), ligne 0 noire."""
    rows = unplanar(0, bm)
    out = [[(0, 0, 0)] * 320]
    for y in range(1, 200):
        pal = pals[(y - 1) * 48:y * 48]
        out.append([st_rgb(pal[spectrum_slot(x, c)]) for x, c in enumerate(rows[y])])
    return out


def spectrum_to_mono(bm, pals):
    """Reference de pic_spectrum_to_mono : chaque point prend la luminance
    de sa couleur Spectrum, tramage de Bayer 4 x 4 ; lignes 0-1 noires."""
    rows = unplanar(0, bm)
    out = bytearray(32000)
    for y in range(1, 200):
        pal = pals[(y - 1) * 48:y * 48]
        lum = [luma(c) for c in pal]
        for k in range(2):
            oy = y * 2 + k
            for x in range(640):
                sx = x // 2
                if lum[spectrum_slot(sx, rows[y][sx])] * 32 < (2 * BAYER[oy & 3][x & 3] + 1) * 15:
                    out[oy * 80 + x // 8] |= 0x80 >> (x % 8)
    out[0:160] = b"\xff" * 160
    return bytes(out)


def spu(bm, pals):
    return bm[:160] and bytes(160) + bm[160:32000] + struct.pack(">%dH" % len(pals), *pals)


def spc(bm, pals):
    """Compresse comme Spectrum 512 : RLE sur les plans, masques de palette."""
    stream = bytearray()
    for p in range(4):
        for off in range(160 + p * 2, 32000, 8):
            stream += bm[off:off + 2]
    packed = bytearray()
    i = 0
    while i < len(stream):
        run = 1
        while i + run < len(stream) and run < 129 and stream[i + run] == stream[i]:
            run += 1
        if run >= 3:
            packed += bytes([258 - run, stream[i]])
            i += run
            continue
        j = i
        while j < len(stream) and j - i < 128:
            if j + 2 < len(stream) and stream[j] == stream[j + 1] == stream[j + 2]:
                break
            j += 1
        packed += bytes([j - i - 1]) + stream[i:j]
        i = j
    if len(packed) & 1:
        packed.append(0)
    colours = bytearray()
    for k in range(597):
        pal = pals[k * 16:(k + 1) * 16]
        mask = 0
        for c in range(15):                # bit 15 : jamais (le format l'interdit)
            if pal[c]:
                mask |= 1 << c
        colours += struct.pack(">H", mask)
        for c in range(15):
            if mask >> c & 1:
                colours += struct.pack(">H", pal[c])
    return (b"SP" + bytes(2) + struct.pack(">II", len(packed), len(colours))
            + bytes(packed) + bytes(colours))


def decode_spectrum(name, data):
    """-> (bitmap 32000 octets, 199*48 couleurs)."""
    if name.upper().endswith(".SPU"):
        pals = list(struct.unpack_from(">%dH" % (199 * 48), data, 32000))
        return bytes(160) + data[160:32000], [c & 0xFFF for c in pals]
    plen = struct.unpack_from(">I", data, 4)[0]
    pos, stream, src = 0, bytearray(), data[12:12 + plen]
    while len(stream) < 31840:
        b = src[pos]
        pos += 1
        if b < 128:
            stream += src[pos:pos + b + 1]
            pos += b + 1
        else:
            stream += bytes([src[pos]]) * (258 - b)
            pos += 1
    bm = bytearray(32000)
    k = 0
    for p in range(4):
        for off in range(160 + p * 2, 32000, 8):
            bm[off:off + 2] = stream[k:k + 2]
            k += 2
    pos = 12 + plen
    pals = []
    for _ in range(597):
        mask = struct.unpack_from(">H", data, pos)[0] & 0x7FFF
        pos += 2
        for c in range(16):
            if mask >> c & 1:
                pals.append(struct.unpack_from(">H", data, pos)[0] & 0xFFF)
                pos += 2
            else:
                pals.append(0)
    return bytes(bm), pals


def spectrum_demo():
    """Un degrade de 512 teintes : l'ecran en bandes de 10 points, chaque
    bande prend sa couleur dans la palette de sa ligne (indice c = bande
    modulo 16, jeu 2 pour la moitie gauche, jeu 3 pour la droite). Rouge
    selon la bande, vert selon la ligne, bleu en diagonale. Les indices 0
    et 15 restent noirs, comme le veut le format compresse."""
    def colour(band, y):
        r = band * 7 // 31
        g = y * 7 // 198
        b = ((band + y // 7) // 4) % 8
        return r << 8 | g << 4 | b
    pals = []
    for y in range(1, 200):
        line = [0] * 48
        for c in range(1, 15):
            line[c] = colour(c, y)            # avant la fenetre : comme le jeu 2
            line[16 + c] = colour(c, y)       # bandes 0-15
            line[32 + c] = colour(16 + c, y)  # bandes 16-31
        pals += line
    def index(x):
        c = (x // 10) % 16
        # A droite, le premier point d'une bande paire est encore dans la
        # fenetre du jeu 2 : on lui donne l'indice de la bande voisine.
        if x >= 160 and x % 10 == 0 and c % 2 == 0 and c > 0:
            return c - 1
        return c
    row = [index(x) for x in range(320)]
    rows = [[0] * 320] + [row] * 199
    return planar(0, rows), pals


# ---- les images de la disquette de demonstration ----

def testcard():
    """Basse resolution : seize barres, puis des anneaux dans les 16 couleurs."""
    pal = [0x000, 0x700, 0x730, 0x770, 0x370, 0x070, 0x073, 0x077,
           0x037, 0x007, 0x307, 0x707, 0x703, 0x777, 0x555, 0x333]
    rows = []
    for y in range(200):
        if y < 60:
            rows.append([x // 20 for x in range(320)])
        else:
            rows.append([int(math.hypot(x - 160, y - 130) / 6) % 16 for x in range(320)])
    return pal, rows


def rings():
    """Basse resolution, degrade bleu -> blanc : se compresse bien (PC1)."""
    pal = [0x001, 0x002, 0x003, 0x004, 0x005, 0x006, 0x007, 0x117,
           0x227, 0x337, 0x447, 0x557, 0x667, 0x777, 0x776, 0x775]
    rows = [[(x // 8 + y // 8) % 2 * 8 + int(math.hypot(x - 160, y - 100) / 16) % 8
             for x in range(320)] for y in range(200)]
    return pal, rows


def sunset():
    """Basse resolution : ciel en bandes, soleil, mer (NEOchrome)."""
    pal = [0x001, 0x102, 0x203, 0x304, 0x405, 0x506, 0x607, 0x710,
           0x720, 0x730, 0x740, 0x750, 0x760, 0x770, 0x003, 0x006]
    rows = []
    for y in range(200):
        row = []
        for x in range(320):
            if y >= 140:
                row.append(14 if (x + y * 3) % 23 < 18 else 15)
            elif math.hypot(x - 160, y - 140) < 50:
                row.append(7 + min(6, (140 - y) // 8))
            else:
                row.append(min(6, y // 20))
        rows.append(row)
    return pal, rows


def medium():
    """Moyenne resolution, 4 couleurs : rayures et damier."""
    pal = [0x777, 0x700, 0x070, 0x007] + [0] * 12
    rows = [[((x // 40 + y // 25) % 2) * 2 + ((x + y) // 16) % 2 for x in range(640)]
            for y in range(200)]
    return pal, rows


def mandel():
    """Haute resolution monochrome : l'ensemble de Mandelbrot."""
    pal = [0x777, 0x000] + [0] * 14
    rows = []
    for y in range(400):
        row = []
        ci = (y - 200) / 160.0
        for x in range(640):
            cr = (x - 420) / 160.0
            zr = zi = 0.0
            n = 0
            while n < 24 and zr * zr + zi * zi < 4.0:
                zr, zi = zr * zr - zi * zi + cr, 2 * zr * zi + ci
                n += 1
            row.append(1 if n == 24 or n % 2 else 0)
        rows.append(row)
    return pal, rows


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "show":
        res, pal, bm = decode(sys.argv[2], open(sys.argv[2], "rb").read())
        print("resolution %d, palette %s" % (res, " ".join("%03x" % c for c in pal)))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
