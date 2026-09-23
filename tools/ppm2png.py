#!/usr/bin/env python3
"""ppm2png.py -- capture PPM (P6) de NeoST vers PNG, sans dependance.

    ppm2png.py in.ppm out.png [--scale N] [--crop X Y W H]

--crop s'applique avant l'agrandissement. En moyenne resolution (640 x 200),
--yscale 2 redonne les proportions de l'ecran.
"""
import argparse
import struct
import zlib


def read_ppm(path):
    data = open(path, "rb").read()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    pos += 1
    if fields[0] != b"P6":
        raise ValueError("not a P6 PPM")
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[pos:pos + w * h * 3]


def write_png(path, w, h, rgb):
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    open(path, "wb").write(png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--scale", type=int, default=1)
    ap.add_argument("--yscale", type=int, default=1)
    ap.add_argument("--crop", type=int, nargs=4)
    a = ap.parse_args()
    w, h, px = read_ppm(a.src)
    x0, y0, cw, ch = a.crop if a.crop else (0, 0, w, h)
    rows = []
    for y in range(y0, y0 + ch):
        line = px[(y * w + x0) * 3:(y * w + x0 + cw) * 3]
        if a.scale > 1:
            line = b"".join(line[i:i + 3] * a.scale for i in range(0, len(line), 3))
        rows.extend([line] * (a.scale * a.yscale))
    write_png(a.dst, cw * a.scale, ch * a.scale * a.yscale, b"".join(rows))


if __name__ == "__main__":
    main()
