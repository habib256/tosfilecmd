#!/usr/bin/env python3
"""lha.py -- archives LHA (en-tete de niveau 0), methodes -lh0- et -lh5-.

Les fichiers YM sont des archives LHA -lh5- ; les bancs et la disquette de
demonstration en fabriquent ici. Le compresseur est verifie contre un
decompresseur independant (lhasa, `lha p`) par tests/test_lha.py ; le
decompresseur de TOSFC (src/lh5.c) est verifie contre lui.

    python3 tools/lha.py pack out.lzh NAME.YM data-file
"""
import struct
import sys

DICBIT = 13
DICSIZ = 1 << DICBIT
MAXMATCH = 256
THRESHOLD = 3
NC = 256 + MAXMATCH - THRESHOLD + 1      # 510
NP = DICBIT + 1                          # 14
NT = 19
CBIT, PBIT, TBIT = 9, 4, 5


class BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, nbits, value):
        for i in range(nbits - 1, -1, -1):
            self.acc = (self.acc << 1) | ((value >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                self.acc = 0
                self.n = 0

    def flush(self):
        if self.n:
            self.out.append(self.acc << (8 - self.n))
            self.acc = 0
            self.n = 0
        return bytes(self.out)


def limited_lengths(freq, limit=16):
    """Longueurs de Huffman bornees (package-merge). freq : liste d'entiers."""
    syms = [i for i, f in enumerate(freq) if f]
    lengths = [0] * len(freq)
    if not syms:
        return lengths
    if len(syms) == 1:
        lengths[syms[0]] = 1
        return lengths
    leaves = sorted((freq[s], [s]) for s in syms)
    level = list(leaves)
    for _ in range(limit - 1):
        merged = []
        for i in range(0, len(level) - 1, 2):
            merged.append((level[i][0] + level[i + 1][0], level[i][1] + level[i + 1][1]))
        level = sorted(leaves + merged, key=lambda t: t[0])
    for w, items in level[:2 * len(syms) - 2]:
        for s in items:
            lengths[s] += 1
    return lengths


def canonical_codes(lengths):
    """Codes comme le make_table de LHA : par longueur croissante, puis par
    symbole croissant."""
    codes = [0] * len(lengths)
    code = 0
    for ln in range(1, 17):
        for s, l in enumerate(lengths):
            if l == ln:
                codes[s] = code
                code += 1
        code <<= 1
    return codes


def find_matches(data):
    """LZ77 glouton, fenetre de 8 Ko : liste de symboles (litteral ou
    (longueur, distance))."""
    out = []
    head = {}
    i = 0
    n = len(data)
    while i < n:
        best_len, best_dist = 0, 0
        if i + 2 < n:
            key = data[i:i + 3]
            cands = head.get(key, [])
            for j in reversed(cands[-64:]):
                if i - j > DICSIZ:
                    break
                ln = 0
                while ln < MAXMATCH and i + ln < n and data[j + ln] == data[i + ln]:
                    ln += 1
                if ln > best_len:
                    best_len, best_dist = ln, i - j
                    if ln == MAXMATCH:
                        break
        if best_len >= THRESHOLD:
            out.append((best_len, best_dist))
            step = best_len
        else:
            out.append(data[i])
            step = 1
        for k in range(i, min(i + step, n - 2)):
            head.setdefault(data[k:k + 3], []).append(k)
        i += step
    return out


def p_code(dist):
    d = dist - 1
    if d == 0:
        return 0, 0, 0
    nb = d.bit_length()
    return nb, nb - 1, d - (1 << (nb - 1))


def write_pt_len(bw, lengths, nbit, special):
    n = len(lengths)
    while n > 0 and lengths[n - 1] == 0:
        n -= 1
    bw.put(nbit, n)
    i = 0
    while i < n:
        k = lengths[i]
        i += 1
        if k <= 6:
            bw.put(3, k)
        else:
            bw.put(k - 3, (1 << (k - 3)) - 2)
        if i == special:
            while i < 6 and lengths[i] == 0:
                i += 1
            bw.put(2, i - 3)


def encode_block(bw, syms):
    cfreq = [0] * NC
    pfreq = [0] * NP
    for s in syms:
        if isinstance(s, int):
            cfreq[s] += 1
        else:
            cfreq[s[0] - THRESHOLD + 256] += 1
            pfreq[p_code(s[1])[0]] += 1
    clen = limited_lengths(cfreq)
    plen = limited_lengths(pfreq)
    ccode, pcode = canonical_codes(clen), canonical_codes(plen)

    bw.put(16, len(syms))
    # Longueurs de c codees par la table t.
    n = NC
    while n > 0 and clen[n - 1] == 0:
        n -= 1
    tsyms = []
    i = 0
    while i < n:
        k = clen[i]
        i += 1
        if k == 0:
            count = 1
            while i < n and clen[i] == 0:
                i += 1
                count += 1
            if count <= 2:
                tsyms += [(0, 0, 0)] * count
            elif count <= 18:
                tsyms.append((1, 4, count - 3))
            elif count == 19:
                tsyms += [(0, 0, 0), (1, 4, 15)]
            else:
                tsyms.append((2, CBIT, count - 20))
        else:
            tsyms.append((k + 2, 0, 0))
    tfreq = [0] * NT
    for t, _, _ in tsyms:
        tfreq[t] += 1
    used_c = sum(1 for x in clen if x)
    if used_c <= 1:
        # Un seul symbole : table constante, zero bit par code.
        only = cfreq.index(max(cfreq))
        bw.put(TBIT, 0)
        bw.put(TBIT, 0)
        bw.put(CBIT, 0)
        bw.put(CBIT, only)
        ccode = [0] * NC
        clen = [0] * NC
    else:
        tlen = limited_lengths(tfreq, 16)
        if sum(1 for x in tlen if x) == 1:
            only = tlen.index(1)
            bw.put(TBIT, 0)
            bw.put(TBIT, only)
            tlen = [0] * NT
            tcode = [0] * NT
        else:
            write_pt_len(bw, tlen, TBIT, 3)
            tcode = canonical_codes(tlen)
        bw.put(CBIT, n)
        for t, eb, ev in tsyms:
            bw.put(tlen[t], tcode[t])
            if eb:
                bw.put(eb, ev)
    if sum(1 for x in plen if x) <= 1:
        only = pfreq.index(max(pfreq)) if any(pfreq) else 0
        bw.put(PBIT, 0)
        bw.put(PBIT, only)
        plen = [0] * NP
        pcode = [0] * NP
    else:
        write_pt_len(bw, plen, PBIT, -1)
    for s in syms:
        if isinstance(s, int):
            bw.put(clen[s], ccode[s])
        else:
            c = s[0] - THRESHOLD + 256
            bw.put(clen[c], ccode[c])
            code, eb, ev = p_code(s[1])
            bw.put(plen[code], pcode[code])
            if eb:
                bw.put(eb, ev)


def lh5_compress(data):
    syms = find_matches(data)
    bw = BitWriter()
    for i in range(0, len(syms), 0xFFFF):
        encode_block(bw, syms[i:i + 0xFFFF])
    return bw.flush()


def crc16(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def archive(name, data, method="-lh5-"):
    """Archive LHA a un fichier, en-tete de niveau 0 (comme les YM)."""
    body = lh5_compress(data) if method == "-lh5-" else data
    nm = name.encode("ascii")
    hdr = (method.encode("ascii") + struct.pack("<II", len(body), len(data))
           + struct.pack("<HH", 0, 0x5000) + bytes([0x20, 0, len(nm)]) + nm
           + struct.pack("<H", crc16(data)))
    return bytes([len(hdr), sum(hdr) & 0xFF]) + hdr + body + b"\0"


def main():
    if len(sys.argv) == 5 and sys.argv[1] == "pack":
        data = open(sys.argv[4], "rb").read()
        open(sys.argv[2], "wb").write(archive(sys.argv[3], data))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
