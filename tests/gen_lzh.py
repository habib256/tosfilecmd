#!/usr/bin/env python3
"""gen_lzh.py -- vecteurs de test pour src/lzh.c : archives fabriquees par
tools/lha.py (lui-meme verifie contre lhasa par tests/test_lha.py), et
leur contenu attendu. Ecrit tests/data/lzh_*.lzh et .bin."""
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))
import lha  # noqa: E402

def cases():
    random.seed(7)
    text = open(os.path.join(os.path.dirname(HERE), "docs", "MANUAL.md"), "rb").read()
    return {
        "empty": b"",
        "one": b"Z",
        "run": b"\x55" * 40000,
        "text": text,
        "random": bytes(random.randrange(256) for _ in range(30000)),
        "mixed": b"YM2149 " * 900 + bytes(random.randrange(8) for _ in range(20000)),
        "big": bytes((i * 13 ^ (i >> 6)) & 0xFF for i in range(200000)),
    }

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "data")
    os.makedirs(out, exist_ok=True)
    for name, data in cases().items():
        open(os.path.join(out, "lzh_%s.bin" % name), "wb").write(data)
        open(os.path.join(out, "lzh_%s.lzh" % name), "wb").write(lha.archive(name.upper() + ".BIN", data))

if __name__ == "__main__":
    main()
