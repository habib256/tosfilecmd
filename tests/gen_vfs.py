#!/usr/bin/env python3
"""gen_vfs.py -- jeux de test de src/vfs.c : une image FAT12 (.ST et .MSA),
des archives LHA, ZIP et ARC, et pour chacune la liste attendue.

    gen_vfs.py DIR

Ecrit DIR/<conteneur> et DIR/<conteneur>.expect : une ligne par entree,
"CHEMIN\tTAILLE\tFICHIER" (FICHIER : le contenu attendu, ecrit a cote),
ou "CHEMIN\\\t-\t-" pour un dossier. Les chemins sont ceux que TOSFC doit
montrer : noms ramenes en 8.3 (name83 ci-dessous reproduit vfs_name83).
"""
import os
import random
import struct
import sys
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))
import fat12  # noqa: E402
import lha  # noqa: E402
import arcpack  # noqa: E402

VALID = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-!#$%&'()@^`{}~")


def name83(raw, taken):
    s = raw.replace("/", "\\").split("\\")[-1].lstrip(".")
    dot = s.rfind(".")
    b, e = (s[:dot], s[dot + 1:]) if dot >= 0 else (s, "")
    conv = lambda t, n: "".join(c if c in VALID else "_" for c in t.upper().replace(" ", ""))[:n]
    base, ext = conv(b, 8) or "_", conv(e, 3)
    for k in range(100):
        cand = base if k == 0 else base[:8 - 1 - len(str(k))] + "~" + str(k)
        if ext:
            cand += "." + ext
        if cand not in taken:
            return cand
    return cand


def files():
    random.seed(11)
    text = open(os.path.join(os.path.dirname(HERE), "docs", "MANUAL.md"), "rb").read()
    return {
        "readme": b"Hello from inside the container.\r\n",
        "empty": b"",
        "text": text,
        "random": bytes(random.randrange(256) for _ in range(9000)),
        "big": bytes((i * 11 ^ (i >> 7)) & 0xFF for i in range(70000)),
    }


class Expect:
    def __init__(self, out, container):
        self.out, self.container = out, container
        self.lines, self.taken, self.k = [], {}, 0

    def add(self, dirpath, raw, data=None):
        """dirpath : chemin 8.3 deja converti ('' ou 'DIR\\SUB')."""
        taken = self.taken.setdefault(dirpath, set())
        n = name83(raw, taken)
        taken.add(n)
        path = (dirpath + "\\" if dirpath else "") + n
        if data is None:
            self.lines.append("%s\\\t-\t-" % path)
        else:
            fn = "%s.%d.bin" % (self.container, self.k)
            self.k += 1
            open(os.path.join(self.out, fn), "wb").write(data)
            self.lines.append("%s\t%d\t%s" % (path, len(data), fn))
        return path

    def write(self):
        open(os.path.join(self.out, self.container + ".expect"), "w").write("\n".join(self.lines) + "\n")


def gen_image(out, f):
    b = fat12.Builder("720k", label="VFSTEST")
    ex = Expect(out, "IMG.ST")
    b.add("README.TXT", f["readme"]); ex.add("", "README.TXT", f["readme"])
    b.add("EMPTY.DAT", f["empty"]); ex.add("", "EMPTY.DAT", f["empty"])
    b.mkdir("DOCS"); d = ex.add("", "DOCS")
    b.add("DOCS\\MANUAL.TXT", f["text"], attr=fat12.ATTR_RO); ex.add(d, "MANUAL.TXT", f["text"])
    b.mkdir("DOCS\\DEEP"); dd = ex.add(d, "DEEP")
    b.add("DOCS\\DEEP\\RANDOM.BIN", f["random"]); ex.add(dd, "RANDOM.BIN", f["random"])
    b.add("BIG.BIN", f["big"]); ex.add("", "BIG.BIN", f["big"])
    img = b.build()
    open(os.path.join(out, "IMG.ST"), "wb").write(img)
    ex.write()
    open(os.path.join(out, "IMG.MSA"), "wb").write(fat12.msa(img))
    # LOOP.ST : la chaine de BIG.BIN revient sur son premier cluster apres le
    # troisieme (fichier de 70 Ko, bien plus court que la disquette).
    im = fat12.Image(img)
    first = [e for p, e in im.walk() if p.endswith("BIG.BIN")][0]["cluster"]
    third = im.fat[im.fat[first]]
    loop = bytearray(img)
    for base in (im.fat_start * 512, (im.fat_start + im.spf) * 512):
        o = base + third * 3 // 2
        if third & 1:
            loop[o] = (loop[o] & 0x0F) | ((first << 4) & 0xF0)
            loop[o + 1] = (first >> 4) & 0xFF
        else:
            loop[o] = first & 0xFF
            loop[o + 1] = (loop[o + 1] & 0xF0) | ((first >> 8) & 0x0F)
    assert fat12.Image(bytes(loop)).fat[third] == first
    open(os.path.join(out, "LOOP.ST"), "wb").write(bytes(loop))
    ms = Expect(out, "IMG.MSA")
    ms.lines = [l.replace("IMG.ST.", "IMG.MSA.") for l in ex.lines]
    for l in ex.lines:
        parts = l.split("\t")
        if parts[2] != "-":
            os.link(os.path.join(out, parts[2]), os.path.join(out, parts[2].replace("IMG.ST.", "IMG.MSA.")))
    ms.write()


def lzh_member(path, data, method="-lh5-"):
    return lha.archive(path, data, method)[:-1]        # sans le 0 final


def gen_lzh(out, f):
    ex = Expect(out, "T.LZH")
    a = b""
    a += lzh_member("README.TXT", f["readme"]); ex.add("", "README.TXT", f["readme"])
    d = ex.add("", "src")
    a += lzh_member("src\\main.c", f["text"]); ex.add(d, "main.c", f["text"])
    a += lzh_member("src\\Data File.bin", f["random"], "-lh0-"); ex.add(d, "Data File.bin", f["random"])
    a += lzh_member("empty", f["empty"]); ex.add("", "empty", f["empty"])
    a += lzh_member("big.bin", f["big"]); ex.add("", "big.bin", f["big"])
    open(os.path.join(out, "T.LZH"), "wb").write(a + b"\0")
    ex.write()


def gen_zip(out, f):
    ex = Expect(out, "T.ZIP")
    path = os.path.join(out, "T.ZIP")
    with zipfile.ZipFile(path, "w") as z:
        z.writestr(zipfile.ZipInfo("docs/", (1995, 5, 5, 12, 0, 0)), b"")
        d = ex.add("", "docs")
        z.writestr(zipfile.ZipInfo("docs/A long name.text", (1995, 5, 5, 12, 0, 0)), f["text"], zipfile.ZIP_DEFLATED)
        ex.add(d, "A long name.text", f["text"])
        z.writestr(zipfile.ZipInfo("docs/A long name.textfile", (1995, 5, 5, 12, 0, 0)), f["readme"], zipfile.ZIP_STORED)
        ex.add(d, "A long name.textfile", f["readme"])
        z.writestr("random.bin", f["random"], zipfile.ZIP_DEFLATED); ex.add("", "random.bin", f["random"])
        z.writestr("empty.txt", f["empty"], zipfile.ZIP_DEFLATED); ex.add("", "empty.txt", f["empty"])
        z.writestr("big.bin", f["big"], zipfile.ZIP_DEFLATED); ex.add("", "big.bin", f["big"])
        z.writestr("stored.bin", f["big"][:5000], zipfile.ZIP_STORED); ex.add("", "stored.bin", f["big"][:5000])
    ex.write()


def gen_arc(out, f):
    ex = Expect(out, "T.ARC")
    members = [("README.TXT", f["readme"], 2), ("TEXT.TXT", f["text"], 3), ("SQUEEZE.TXT", f["text"], 4),
               ("CRUNCH.BIN", f["big"], 8), ("SQUASH.BIN", f["random"], 9), ("EMPTY.DAT", f["empty"], 9)]
    for n, data, m in members:
        ex.add("", n, data)
    open(os.path.join(out, "T.ARC"), "wb").write(arcpack.archive(members))
    ex.write()


def gen_zip_methods(out):
    """Methodes que TOSFC ne sait pas lire : bzip2, et un membre chiffre (bit 0
    des drapeaux). Listes, mais leur ouverture rend TE_METHOD."""
    import io
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr("ok.txt", b"readable\r\n", zipfile.ZIP_STORED)
        z.writestr("bz.txt", b"bzip2 " * 50, zipfile.ZIP_BZIP2)
        z.writestr("enc.txt", b"secret\r\n", zipfile.ZIP_STORED)
    d = bytearray(buf.getvalue())
    # Drapeau "chiffre" sur enc.txt, dans l'en-tete local et le repertoire central.
    for sig, name_off, flag_off in ((b"PK\x03\x04", 30, 6), (b"PK\x01\x02", 46, 8)):
        i = 0
        while True:
            i = d.find(sig, i)
            if i < 0:
                break
            if d[i + name_off:i + name_off + 7] == b"enc.txt":
                d[i + flag_off] |= 1
            i += 4
    open(os.path.join(out, "M.ZIP"), "wb").write(bytes(d))


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for fn in os.listdir(out):
        if fn.startswith(("IMG.", "T.")):
            os.remove(os.path.join(out, fn))
    f = files()
    gen_image(out, f)
    gen_lzh(out, f)
    gen_zip(out, f)
    gen_arc(out, f)
    gen_zip_methods(out)


if __name__ == "__main__":
    main()
