#!/usr/bin/env python3
"""screenshots.py -- les captures du README et du manuel, prises dans NeoST.

    python3 tools/screenshots.py docs/screenshots

Rejoue une courte session sur une copie de la disquette publiee et range des
PNG aux proportions de l'ecran (la moyenne resolution est doublee en
hauteur).
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "bench"))
from tosfc import TOSFC, DISK, scratch, copy_disk  # noqa: E402

PPM2PNG = os.path.join(ROOT, "tools", "ppm2png.py")


def shot(st, out, name, mono=False):
    ppm = os.path.join(scratch(), name + ".ppm")
    st.shot(ppm)
    args = [sys.executable, PPM2PNG, ppm, os.path.join(out, name + ".png")]
    if not mono:
        args += ["--yscale", "2"]
    subprocess.run(args, check=True)
    print(os.path.join(out, name + ".png"))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "docs", "screenshots")
    os.makedirs(out, exist_ok=True)
    st = TOSFC(disk=copy_disk(DISK, scratch()))
    try:
        st.boot()
        st.go(0, "A:\\DEMO\\MANY\\")
        st.go(1, "A:\\DEMO\\")
        st.tag(0, "FILE03.DAT", "FILE04.DAT", "FILE07.DAT")
        st.select(0, "FILE09.DAT")
        shot(st, out, "01-panels")
        # Une premiere copie de LOREM.TXT dans DEMO, puis une seconde : la
        # question d'ecrasement.
        st.go(0, "A:\\DEMO\\TEXTS\\")
        st.go(1, "A:\\DEMO\\")
        for _ in range(2):
            st.select(0, "LOREM.TXT")
            st.letter("c")
            st.hit("RETURN")
        st.wait_dialog("File exists")
        shot(st, out, "02-overwrite")
        st.letter("n")
        st.letter("?")
        shot(st, out, "03-help")
        st.hit("ESC")
        st.select(0, "LOREM.TXT")
        st.letter("a")
        shot(st, out, "04-attributes")
        st.hit("ESC")
    finally:
        st.close()
    st = TOSFC(disk=copy_disk(DISK, scratch()), mono=True)
    try:
        st.boot()
        st.go(0, "A:\\DEMO\\")
        st.go(1, "A:\\DEMO\\NESTED\\")
        st.tag(0, "BIG.BIN")
        st.select(0, "TEXTS")
        shot(st, out, "05-mono", mono=True)
    finally:
        st.close()


if __name__ == "__main__":
    main()
