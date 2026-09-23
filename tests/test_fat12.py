#!/usr/bin/env python3
"""test_fat12.py -- l'outil qui fabrique et relit les disquettes.

Les bancs jugent TOSFC sur ce que tools/fat12.py relit : un fsck qui ne
verrait pas une chaine croisee ou un cluster perdu laisserait passer une
corruption. On fabrique donc des images saines, puis on les abime a la main.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))
import fat12  # noqa: E402

fails = []


def check(cond, what):
    if not cond:
        fails.append(what)
        print("FAIL " + what)


def sample():
    b = fat12.Builder("720k", label="TEST")
    b.mkdir("DIR")
    b.mkdir("DIR\\SUB")
    b.add("A.TXT", b"hello\r\n")
    b.add("DIR\\BIG.BIN", bytes(range(256)) * 20, attr=fat12.ATTR_RO)
    b.add("DIR\\SUB\\EMPTY", b"")
    return bytearray(b.build())


def set_fat(img, cluster, value):
    im = fat12.Image(bytes(img))
    for k in range(im.nfats):
        off = (im.fat_start + k * im.spf) * im.bps + cluster * 3 // 2
        v = img[off] | (img[off + 1] << 8)
        if cluster & 1:
            v = (v & 0x000F) | (value << 4)
        else:
            v = (v & 0xF000) | value
        img[off] = v & 0xFF
        img[off + 1] = v >> 8


def main():
    img = sample()
    im = fat12.Image(bytes(img))
    check(im.fsck() == [], "fresh image is clean: %s" % im.fsck())
    check(im.read("A:\\A.TXT") == b"hello\r\n", "read a root file")
    check(im.read("A:\\DIR\\BIG.BIN") == bytes(range(256)) * 20, "read a multi-cluster file")
    check(im.read("A:\\DIR\\SUB\\EMPTY") == b"", "read an empty file")
    check(im.find("A:\\DIR\\BIG.BIN")["attr"] & fat12.ATTR_RO, "attributes kept")
    names = [p for p, _ in im.walk()]
    # Ordre des entrees sur le disque (ordre de creation), en profondeur.
    check(names == ["A:\\DIR", "A:\\DIR\\SUB", "A:\\DIR\\SUB\\EMPTY",
                    "A:\\DIR\\BIG.BIN", "A:\\A.TXT"], "walk order: %s" % names)
    check(sum(struct.unpack(">256H", bytes(img[:512]))) & 0xFFFF != 0x1234,
          "boot sector is not executable")

    big = im.find("A:\\DIR\\BIG.BIN")["cluster"]
    a = im.find("A:\\A.TXT")["cluster"]

    bad = bytearray(img)
    set_fat(bad, a, big)            # A.TXT continue dans BIG.BIN
    p = fat12.Image(bytes(bad)).fsck()
    check(any("cross-linked" in x for x in p), "cross-link detected: %s" % p)

    bad = bytearray(img)
    set_fat(bad, 700, 0xFFF)        # cluster alloue, a personne
    p = fat12.Image(bytes(bad)).fsck()
    check(any("lost cluster 700" in x for x in p), "lost cluster detected: %s" % p)

    bad = bytearray(img)
    root = fat12.Image(bytes(bad)).root_start * 512
    # Taille de A.TXT portee a 5000 octets pour un seul cluster
    for i in range(0, 112 * 32, 32):
        if bad[root + i:root + i + 11] == b"A       TXT":
            struct.pack_into("<I", bad, root + i + 28, 5000)
    p = fat12.Image(bytes(bad)).fsck()
    check(any("A.TXT: size 5000" in x for x in p), "size/chain mismatch detected: %s" % p)

    bad = bytearray(img)
    im2 = fat12.Image(bytes(bad))
    bad[(im2.fat_start + im2.spf) * 512 + 10] ^= 0xFF   # seconde FAT seulement
    p = fat12.Image(bytes(bad)).fsck()
    check("FAT copies differ" in p, "diverging FAT copies detected: %s" % p)

    ss = fat12.Image(fat12.Builder("360k").build())
    check(ss.nsects == 720 and ss.heads == 1 and ss.fsck() == [], "360 KB single-sided geometry")

    print("test_fat12: %d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
