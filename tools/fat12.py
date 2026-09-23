#!/usr/bin/env python3
"""fat12.py -- images de disquettes Atari ST (.st) : fabrication, lecture, fsck.

Utilise par tools/mkdisk.py pour fabriquer les disquettes publiees, et par
les bancs pour relire sur l'hote ce que TOSFC a ecrit : contenu octet par
octet, attributs, et coherence de la FAT (chaines croisees, clusters perdus,
tailles impossibles, copies de FAT divergentes).

    python3 tools/fat12.py ls  disk.st          # arborescence
    python3 tools/fat12.py cat disk.st A:\\DIR\\F.TXT
    python3 tools/fat12.py fsck disk.st
"""
import struct
import sys

ATTR_RO, ATTR_HIDDEN, ATTR_SYSTEM, ATTR_LABEL, ATTR_DIR, ATTR_ARCH = 1, 2, 4, 8, 16, 32

GEOMETRIES = {
    # nom : (secteurs, secteurs/piste, faces, media, secteurs/FAT, entrees racine)
    "720k": (1440, 9, 2, 0xF9, 5, 112),
    "360k": (720, 9, 1, 0xF8, 5, 112),
}


def dos_date(y, m, d):
    return ((y - 1980) << 9) | (m << 5) | d


def dos_time(h, mi, s=0):
    return (h << 11) | (mi << 5) | (s // 2)


def split83(name):
    base, _, ext = name.upper().partition(".")
    if not base or len(base) > 8 or len(ext) > 3:
        raise ValueError("not an 8.3 name: %r" % name)
    return base.ljust(8).encode("ascii"), ext.ljust(3).encode("ascii")


class Node:
    def __init__(self, name, is_dir, data=b"", attr=0, date=None, time=None):
        self.name = name.upper()
        self.is_dir = is_dir
        self.data = data
        self.attr = attr | (ATTR_DIR if is_dir else 0)
        self.date = date if date is not None else dos_date(2026, 9, 23)
        self.time = time if time is not None else dos_time(12, 0)
        self.children = []


class Builder:
    """Construit une image FAT12 au format des disquettes formatees par le TOS."""

    def __init__(self, geometry="720k", serial=0x544653, label=None):
        (self.nsects, self.spt, self.heads, self.media, self.spf,
         self.ndirs) = GEOMETRIES[geometry]
        self.spc = 2
        self.serial = serial
        self.label = label
        self.root = Node("", True)

    def _find_dir(self, path):
        node = self.root
        for part in [p for p in path.split("\\") if p]:
            for c in node.children:
                if c.name == part.upper() and c.is_dir:
                    node = c
                    break
            else:
                raise KeyError(path)
        return node

    def mkdir(self, path, **kw):
        parent, _, name = path.rstrip("\\").rpartition("\\")
        d = Node(name, True, **kw)
        self._find_dir(parent).children.append(d)
        return d

    def add(self, path, data, attr=0, **kw):
        parent, _, name = path.rpartition("\\")
        split83(name)
        self._find_dir(parent).children.append(Node(name, False, data, attr, **kw))

    def build(self):
        bps = 512
        img = bytearray(self.nsects * bps)
        boot = bytearray(bps)
        boot[0:2] = b"\x60\x38"
        boot[2:8] = b"TOSFC "
        boot[8:11] = struct.pack("<I", self.serial)[:3]
        struct.pack_into("<HBHBHHBHHHH", boot, 11, bps, self.spc, 1, 2,
                         self.ndirs, self.nsects, self.media, self.spf,
                         self.spt, self.heads, 0)
        # Pas executable : la somme des mots ne doit pas valoir $1234.
        if sum(struct.unpack(">256H", bytes(boot))) & 0xFFFF == 0x1234:
            boot[510] ^= 1
        img[0:bps] = boot

        fat_start = 1
        root_start = fat_start + 2 * self.spf
        data_start = root_start + self.ndirs * 32 // bps
        nclusters = (self.nsects - data_start) // self.spc
        fat = [0] * (nclusters + 2)
        fat[0] = 0xF00 | self.media
        fat[1] = 0xFFF
        cbytes = self.spc * bps
        next_free = [2]

        def alloc(nbytes):
            n = max(1, (nbytes + cbytes - 1) // cbytes) if nbytes else 0
            if n == 0:
                return 0, []
            first = next_free[0]
            chain = list(range(first, first + n))
            if chain[-1] >= nclusters + 2:
                raise ValueError("disk full")
            next_free[0] += n
            for a, b in zip(chain, chain[1:]):
                fat[a] = b
            fat[chain[-1]] = 0xFFF
            return first, chain

        def write_chain(chain, data):
            for i, c in enumerate(chain):
                off = (data_start + (c - 2) * self.spc) * bps
                piece = data[i * cbytes:(i + 1) * cbytes]
                img[off:off + len(piece)] = piece

        def entry(node, cluster, size):
            b, e = split83(node.name)
            return (b + e + bytes([node.attr]) + bytes(10)
                    + struct.pack("<HHHI", node.time, node.date, cluster, size))

        def build_dir(node, parent_cluster, self_cluster, chain):
            ents = []
            if node is not self.root:
                ents.append(b".       " + b"   " + bytes([ATTR_DIR]) + bytes(10)
                            + struct.pack("<HHHI", node.time, node.date, self_cluster, 0))
                ents.append(b"..      " + b"   " + bytes([ATTR_DIR]) + bytes(10)
                            + struct.pack("<HHHI", node.time, node.date, parent_cluster, 0))
            elif self.label:
                b, e = split83(self.label)
                ents.append(b + e + bytes([ATTR_LABEL]) + bytes(10)
                            + struct.pack("<HHHI", 0, node.date, 0, 0))
            for c in node.children:
                if c.is_dir:
                    need = (len(c.children) + 2) * 32
                    first, cchain = alloc(need)
                    c.cluster, c.chain = first, cchain
                    ents.append(entry(c, first, 0))
                else:
                    first, fchain = alloc(len(c.data))
                    write_chain(fchain, c.data)
                    ents.append(entry(c, first, len(c.data)))
            raw = b"".join(ents)
            if node is self.root:
                if len(ents) > self.ndirs:
                    raise ValueError("too many root entries")
                off = root_start * bps
                img[off:off + len(raw)] = raw
            else:
                write_chain(chain, raw)
            for c in node.children:
                if c.is_dir:
                    build_dir(c, 0 if node is self.root else self_cluster,
                              c.cluster, c.chain)

        build_dir(self.root, 0, 0, None)

        fatb = bytearray(self.spf * bps)
        for i in range(0, len(fat) - 1, 2):
            a, b = fat[i], fat[i + 1]
            fatb[i * 3 // 2:i * 3 // 2 + 3] = bytes(
                [a & 0xFF, ((a >> 8) & 0x0F) | ((b & 0x0F) << 4), b >> 4])
        if len(fat) % 2:
            i = len(fat) - 1
            fatb[i * 3 // 2] = fat[i] & 0xFF
            fatb[i * 3 // 2 + 1] = (fat[i] >> 8) & 0x0F
        for k in range(2):
            off = (fat_start + k * self.spf) * bps
            img[off:off + len(fatb)] = fatb
        return bytes(img)


class Image:
    """Lecture d'une image .st et verifications de coherence."""

    def __init__(self, data):
        self.data = data
        (self.bps, self.spc, self.res, self.nfats, self.ndirs, self.nsects,
         self.media, self.spf, self.spt, self.heads, _) = struct.unpack_from(
            "<HBHBHHBHHHH", data, 11)
        if self.bps != 512 or self.spc == 0 or self.nsects * 512 > len(data):
            raise ValueError("not an Atari FAT12 image")
        self.fat_start = self.res
        self.root_start = self.fat_start + self.nfats * self.spf
        self.data_start = self.root_start + self.ndirs * 32 // self.bps
        self.nclusters = (self.nsects - self.data_start) // self.spc
        self.fats = [self._read_fat(k) for k in range(self.nfats)]
        self.fat = self.fats[0]

    def _read_fat(self, k):
        off = (self.fat_start + k * self.spf) * self.bps
        raw = self.data[off:off + self.spf * self.bps]
        out = []
        for i in range(self.nclusters + 2):
            j = i * 3 // 2
            v = raw[j] | (raw[j + 1] << 8)
            out.append((v >> 4) if i & 1 else (v & 0xFFF))
        return out

    def chain(self, first):
        out, c, seen = [], first, set()
        while 2 <= c < 0xFF0:
            if c in seen or c >= self.nclusters + 2:
                raise ValueError("bad cluster chain from %d" % first)
            seen.add(c)
            out.append(c)
            c = self.fat[c]
        return out

    def cluster_bytes(self, chain):
        cb = self.spc * self.bps
        return b"".join(self.data[(self.data_start + (c - 2) * self.spc) * self.bps:
                                  (self.data_start + (c - 2) * self.spc) * self.bps + cb]
                        for c in chain)

    def entries(self, cluster):
        if cluster == 0:
            off = self.root_start * self.bps
            raw = self.data[off:off + self.ndirs * 32]
        else:
            raw = self.cluster_bytes(self.chain(cluster))
        out = []
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5:
                continue
            base = e[0:8].decode("latin-1").rstrip()
            ext = e[8:11].decode("latin-1").rstrip()
            time, date, first, size = struct.unpack_from("<HHHI", e, 22)
            out.append(dict(name=base + ("." + ext if ext else ""), attr=e[11],
                            time=time, date=date, cluster=first, size=size))
        return out

    def walk(self, cluster=0, prefix="A:\\"):
        for e in self.entries(cluster):
            if e["name"] in (".", "..") or e["attr"] & ATTR_LABEL:
                continue
            path = prefix + e["name"]
            yield path, e
            if e["attr"] & ATTR_DIR:
                yield from self.walk(e["cluster"], path + "\\")

    def find(self, path):
        parts = [p for p in path.upper().split("\\")[1:] if p]
        cluster, found = 0, None
        for i, part in enumerate(parts):
            for e in self.entries(cluster):
                if e["name"] == part:
                    found = e
                    cluster = e["cluster"]
                    break
            else:
                return None
        return found

    def read(self, path):
        e = self.find(path)
        if e is None or e["attr"] & ATTR_DIR:
            return None
        if e["size"] == 0:
            return b""
        return self.cluster_bytes(self.chain(e["cluster"]))[:e["size"]]

    def fsck(self):
        """Liste des problemes (vide si le disque est sain)."""
        problems = []
        if any(f != self.fats[0] for f in self.fats[1:]):
            problems.append("FAT copies differ")
        owner = {}
        cb = self.spc * self.bps

        def claim(chain, path):
            for c in chain:
                if c in owner:
                    problems.append("cross-linked cluster %d: %s and %s" % (c, owner[c], path))
                owner[c] = path

        def visit(cluster, prefix, depth):
            if depth > 32:
                problems.append("directory loop at " + prefix)
                return
            try:
                ents = self.entries(cluster)
            except ValueError as e:
                problems.append("%s: %s" % (prefix, e))
                return
            for e in ents:
                if e["name"] in (".", "..") or e["attr"] & ATTR_LABEL:
                    continue
                path = prefix + e["name"]
                try:
                    chain = self.chain(e["cluster"]) if e["cluster"] else []
                except ValueError as err:
                    problems.append("%s: %s" % (path, err))
                    continue
                claim(chain, path)
                if e["attr"] & ATTR_DIR:
                    if not chain:
                        problems.append(path + ": folder without clusters")
                    else:
                        visit(e["cluster"], path + "\\", depth + 1)
                else:
                    need = (e["size"] + cb - 1) // cb
                    if len(chain) != need:
                        problems.append("%s: size %d but %d cluster(s)"
                                        % (path, e["size"], len(chain)))

        visit(0, "A:\\", 0)
        for c in range(2, self.nclusters + 2):
            if self.fat[c] != 0 and c not in owner and self.fat[c] < 0xFF0:
                problems.append("lost cluster %d" % c)
            elif self.fat[c] >= 0xFF8 and c not in owner:
                problems.append("lost cluster %d" % c)
        return problems


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    img = Image(open(sys.argv[2], "rb").read())
    if sys.argv[1] == "ls":
        for path, e in img.walk():
            kind = "<DIR>" if e["attr"] & ATTR_DIR else str(e["size"])
            print("%-40s %8s  attr=%02x" % (path, kind, e["attr"]))
    elif sys.argv[1] == "cat":
        data = img.read(sys.argv[3])
        if data is None:
            sys.exit("not found")
        sys.stdout.buffer.write(data)
    elif sys.argv[1] == "fsck":
        p = img.fsck()
        print("\n".join(p) if p else "clean")
        sys.exit(1 if p else 0)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
