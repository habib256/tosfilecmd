#!/usr/bin/env python3
"""test_elf2prg.py -- l'executable TOS produit est-il bien forme ?

On relit build/TOSFC.PRG comme le ferait le chargeur du GEMDOS : en-tete,
table de relocation (deltas pairs, dans le texte ou les donnees), et on
verifie que reloger a deux adresses differentes ne change que les longs
listes -- de la valeur de l'ecart exactement.
"""
import os
import struct
import sys


def relocate(prg, base):
    magic, tlen, dlen, blen, slen = struct.unpack(">HIIII", prg[:18])
    body = bytearray(prg[28:28 + tlen + dlen])
    pos = 28 + tlen + dlen + slen
    first = struct.unpack(">I", prg[pos:pos + 4])[0]
    pos += 4
    offs = []
    if first:
        off = first
        while True:
            offs.append(off)
            v = struct.unpack(">I", body[off:off + 4])[0]
            struct.pack_into(">I", body, off, (v + base) & 0xFFFFFFFF)
            while True:
                d = prg[pos]
                pos += 1
                if d == 0:
                    return body, offs
                if d == 1:
                    off += 254
                    continue
                if d & 1:
                    raise ValueError("odd relocation delta")
                off += d
                break
    return body, offs


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else "build"
    prg = open(os.path.join(build, "TOSFC.PRG"), "rb").read()
    fails = []
    magic, tlen, dlen, blen, slen = struct.unpack(">HIIII", prg[:18])
    if magic != 0x601A:
        fails.append("bad magic")
    if slen != 0:
        fails.append("unexpected symbol table")
    a, offs = relocate(prg, 0x10000)
    b, _ = relocate(prg, 0x20000)
    if not offs:
        fails.append("no relocations at all")
    if any(o >= tlen + dlen - 3 for o in offs):
        fails.append("relocation outside text+data")
    diff = [i for i in range(len(a)) if a[i] != b[i]]
    touched = set()
    for o in offs:
        touched.update(range(o, o + 4))
    if not set(diff) <= touched:
        fails.append("bytes changed outside relocated longs")
    for o in offs:
        va = struct.unpack(">I", a[o:o + 4])[0]
        vb = struct.unpack(">I", b[o:o + 4])[0]
        if vb - va != 0x10000:
            fails.append("relocated long at %x moved by %x" % (o, vb - va))
            break
        if not (0x10000 <= va < 0x10000 + tlen + dlen + blen):
            fails.append("relocated pointer at %x points outside the program" % o)
            break
    for f in fails:
        print("FAIL " + f)
    print("test_elf2prg: %d relocations, %d failed" % (len(offs), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
