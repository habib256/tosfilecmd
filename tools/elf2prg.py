#!/usr/bin/env python3
"""elf2prg.py -- ELF m68k (lie a 0, --emit-relocs) vers executable TOS $601A.

Le TOS charge un programme n'importe ou en memoire puis ajoute l'adresse du
texte a chaque long liste dans la table de relocation. On la construit a
partir des relocations R_68K_32 que l'editeur de liens a conservees. Toute
autre relocation absolue (16 ou 8 bits) ne peut pas etre relogee par le TOS :
on refuse le binaire plutot que de livrer un programme qui plantera ailleurs
qu'a l'adresse 0.

    elf2prg.py build/tosfc.elf build/TOSFC.PRG
"""
import struct
import sys

R_68K_NONE, R_68K_32, R_68K_16, R_68K_8 = 0, 1, 2, 3
R_68K_PC32, R_68K_PC16, R_68K_PC8 = 4, 5, 6
SHN_ABS = 0xFFF1


def die(msg):
    sys.exit("elf2prg: " + msg)


def read_elf(data):
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 2:
        die("not a big-endian ELF32 file")
    (e_shoff,) = struct.unpack_from(">I", data, 0x20)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from(">HHH", data, 0x2E)
    secs = []
    for i in range(e_shnum):
        f = struct.unpack_from(">IIIIIIIIII", data, e_shoff + i * e_shentsize)
        secs.append(dict(name_off=f[0], type=f[1], flags=f[2], addr=f[3],
                         offset=f[4], size=f[5], link=f[6], info=f[7],
                         entsize=f[9]))
    strtab = secs[e_shstrndx]
    for s in secs:
        start = strtab["offset"] + s["name_off"]
        s["name"] = data[start:data.index(b"\0", start)].decode()
    return secs


def main():
    if len(sys.argv) != 3:
        die("usage: elf2prg.py in.elf out.prg")
    data = open(sys.argv[1], "rb").read()
    secs = read_elf(data)
    by = {s["name"]: s for s in secs}
    for need in (".text", ".data", ".bss"):
        if need not in by:
            die("missing section " + need)
    text, dsec, bss = by[".text"], by[".data"], by[".bss"]
    if text["addr"] != 0:
        die(".text must start at 0")
    if dsec["addr"] != text["addr"] + text["size"]:
        die(".data does not follow .text")
    if bss["addr"] != dsec["addr"] + dsec["size"]:
        die(".bss does not follow .data")
    image = data[text["offset"]:text["offset"] + text["size"]]
    image += data[dsec["offset"]:dsec["offset"] + dsec["size"]]
    loaded = {text["addr"]: text, dsec["addr"]: dsec}

    symtab = by.get(".symtab")
    if symtab is None:
        die("no symbol table")

    fixups = []
    for s in secs:
        if s["type"] != 4:            # SHT_RELA
            continue
        target = secs[s["info"]]
        if target["name"] not in (".text", ".data"):
            continue
        for off in range(s["offset"], s["offset"] + s["size"], 12):
            r_offset, r_info, _addend = struct.unpack_from(">IIi", data, off)
            rtype, rsym = r_info & 0xFF, r_info >> 8
            sym_off = symtab["offset"] + rsym * 16
            st_shndx = struct.unpack_from(">H", data, sym_off + 14)[0]
            if rtype in (R_68K_NONE, R_68K_PC32, R_68K_PC16, R_68K_PC8):
                continue
            if st_shndx == SHN_ABS:
                continue
            if rtype != R_68K_32:
                die("unsupported absolute relocation type %d at $%x"
                    % (rtype, r_offset))
            if r_offset & 1:
                die("odd relocation offset $%x" % r_offset)
            fixups.append(r_offset)

    fixups = sorted(set(fixups))
    reloc = bytearray()
    if fixups:
        reloc += struct.pack(">I", fixups[0])
        prev = fixups[0]
        for f in fixups[1:]:
            d = f - prev
            while d > 254:
                reloc.append(1)
                d -= 254
            reloc.append(d)
            prev = f
        reloc.append(0)
    else:
        reloc += struct.pack(">I", 0)

    hdr = struct.pack(">HIIIIIIH", 0x601A, text["size"], dsec["size"],
                      bss["size"], 0, 0, 0, 0)
    with open(sys.argv[2], "wb") as f:
        f.write(hdr + image + reloc)
    print("%s: text %d, data %d, bss %d, %d relocations"
          % (sys.argv[2], text["size"], dsec["size"], bss["size"], len(fixups)))


if __name__ == "__main__":
    main()
