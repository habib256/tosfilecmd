#!/usr/bin/env python3
"""arcpack.py -- archives ARC (SEA ARC 5.x) pour les tests et la demo.

Methodes : 2 stocke, 3 RLE ($90), 4 squeeze (Huffman + RLE), 8 crunch (LZW
12 bits + RLE, comme compress 4.0), 9 squash (LZW 13 bits). Verifie a la
main contre un decompresseur independant (unar -D, 30 archives de 2026-09-23 :
unar ne reconnait une archive que si son premier membre est stocke) ; dans
le depot, src/arc.c le relit a chaque `make test` (test_vfs).

    python3 tools/arcpack.py out.arc METHOD FILE...
"""
import heapq
import os
import struct
import sys


def rle90(data):
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        c = data[i]
        run = 1
        while i + run < n and data[i + run] == c and run < 255:
            run += 1
        if c == 0x90:
            out += b"\x90\x00"
            if run > 1:
                out += bytes([0x90, run])     # la repetition compte celui deja ecrit
        else:
            out.append(c)
            if run > 2:
                out += bytes([0x90, run])
            elif run == 2:
                out.append(c)
        i += run
    return bytes(out)


def squeeze(data):
    """Huffman (257 symboles, 256 = fin) ; noeuds (gauche, droite), feuille
    = -(valeur + 1), racine = noeud 0 ; bits de poids faible d'abord."""
    freq = [0] * 257
    for b in data:
        freq[b] += 1
    freq[256] = 1
    heap = [(f, i, ("leaf", s)) for i, (s, f) in enumerate((s, f) for s, f in enumerate(freq) if f)]
    heapq.heapify(heap)
    uid = 1000
    if len(heap) == 1:
        heap.append((0, uid, ("leaf", 256 if heap[0][2][1] != 256 else 0)))
        uid += 1
    while len(heap) > 1:
        a = heapq.heappop(heap)
        b = heapq.heappop(heap)
        heapq.heappush(heap, (a[0] + b[0], uid, ("node", a[2], b[2])))
        uid += 1
    root = heap[0][2]
    nodes, codes = [], {}

    def number(t):
        idx = len(nodes)
        nodes.append(None)
        kids = []
        for sub in t[1:]:
            kids.append(-(sub[1] + 1) if sub[0] == "leaf" else number(sub))
        nodes[idx] = kids
        return idx

    number(root)

    def walk(i, prefix):
        for bit, child in enumerate(nodes[i]):
            if child < 0:
                codes[-(child + 1)] = prefix + [bit]
            else:
                walk(child, prefix + [bit])

    walk(0, [])
    out = bytearray(struct.pack("<H", len(nodes)))
    for l, r in nodes:
        out += struct.pack("<hh", l, r)
    bits = []
    for b in data:
        bits += codes[b]
    bits += codes[256]
    acc = 0
    for i, bit in enumerate(bits):
        acc |= bit << (i % 8)
        if i % 8 == 7:
            out.append(acc)
            acc = 0
    if len(bits) % 8:
        out.append(acc)
    return bytes(out)


def lzw(data, maxbits, clear_when_full=True):
    """compress 4.0 : codes groupes par n_bits octets, groupe complete quand
    la largeur change."""
    out = bytearray()
    st = dict(n_bits=9, maxcode=511, free_ent=257, clear=0, buf=bytearray(16), offset=0)
    maxmax = 1 << maxbits

    def output(code):
        n = st["n_bits"]
        if code >= 0:
            off = st["offset"]
            for k in range(n):
                if code >> k & 1:
                    st["buf"][(off + k) >> 3] |= 1 << ((off + k) & 7)
            st["offset"] += n
            if st["offset"] == n * 8:
                out.extend(st["buf"][:n])
                st["buf"] = bytearray(16)
                st["offset"] = 0
            if st["free_ent"] > st["maxcode"] or st["clear"]:
                if st["offset"] > 0:
                    out.extend(st["buf"][:n])
                    st["buf"] = bytearray(16)
                st["offset"] = 0
                if st["clear"]:
                    st["n_bits"] = 9
                    st["maxcode"] = 511
                    st["clear"] = 0
                else:
                    st["n_bits"] += 1
                    st["maxcode"] = maxmax if st["n_bits"] == maxbits else (1 << st["n_bits"]) - 1
        else:
            if st["offset"] > 0:
                out.extend(st["buf"][:(st["offset"] + 7) // 8])

    if not data:
        return bytes(out)
    table = {}
    ent = data[0]
    for c in data[1:]:
        key = (ent, c)
        if key in table:
            ent = table[key]
            continue
        output(ent)
        ent = c
        if st["free_ent"] < maxmax:
            table[key] = st["free_ent"]
            st["free_ent"] += 1
        elif clear_when_full:
            table = {}
            st["free_ent"] = 257
            st["clear"] = 1
            output(256)
    output(ent)
    output(-1)
    return bytes(out)


def crc16(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def pack(method, data):
    if method == 2:
        return data
    if method == 3:
        return rle90(data)
    if method == 4:
        return squeeze(rle90(data))
    if method == 8:
        return bytes([12]) + lzw(rle90(data), 12)
    if method == 9:
        return lzw(data, 13)
    raise ValueError(method)


def member(name, data, method, date=0x5A77, time=0x6000):
    body = pack(method, data)
    nm = name.upper().encode("ascii")[:12].ljust(13, b"\0")
    return (bytes([0x1A, method]) + nm + struct.pack("<IHHHI", len(body), date, time, crc16(data), len(data))
            + body)


def archive(members):
    """members : [(nom, donnees, methode)]"""
    return b"".join(member(n, d, m) for n, d, m in members) + b"\x1a\x00"


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    method = int(sys.argv[2])
    members = [(os.path.basename(p), open(p, "rb").read(), method) for p in sys.argv[3:]]
    open(sys.argv[1], "wb").write(archive(members))


if __name__ == "__main__":
    main()
