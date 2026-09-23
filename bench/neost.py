#!/usr/bin/env python3
"""neost.py -- le pilote commun des bancs : un Atari ST emule par NeoST.

NeoST tourne sans fenetre en mode serveur (`neost-headless --server`) : on lui
envoie des trames, des touches, des mouvements de souris, et on lit la
memoire sans effet de bord. Rien n'est ajoute au binaire livre : l'adresse du
programme vient de la basepage courante du GEMDOS (act_pd, en-tete systeme +
$28), celles de ses variables de la table de symboles du lien
(build/tosfc.sym). L'ecran se lit dans scr_chr, le texte exact que TOSFC
dessine.

Variables d'environnement :
  NEOST        l'executable neost-headless (defaut : ../neost/build/)
  NEOST_ROM    l'image TOS (defaut : EmuTOS 192 Ko US de NeoST)
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_NEOST = os.path.join(os.path.dirname(ROOT), "neost", "build", "neost-headless")
NEOST = os.environ.get("NEOST", DEFAULT_NEOST)
NEOST_DIR = os.path.dirname(os.path.dirname(os.path.abspath(NEOST)))
ROM = os.environ.get("NEOST_ROM", os.path.join(NEOST_DIR, "roms", "etos192us.img"))

# Glyphes propres a TOSFC -> caracteres lisibles dans les journaux.
GLYPHS = {0x0F: "┴", 0x10: "═", 0x11: "║", 0x12: "╔", 0x13: "╗", 0x14: "╚",
          0x15: "╝", 0x16: "╤", 0x17: "╧", 0x18: "╟", 0x19: "╢", 0x1A: "│",
          0x1B: "─", 0x1C: "✓", 0x1D: "░", 0x1E: "█", 0x1F: "↑", 0x02: "↓", 0x0E: "♪"}

# Scancodes US : caractere -> (scancode, shift)
_ROWS = [
    ("1234567890-=", 0x02), ("qwertyuiop[]", 0x10), ("asdfghjkl;'", 0x1E),
    ("zxcvbnm,./", 0x2C),
]
_SHIFTED = {"!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6",
            "&": "7", "*": "8", "(": "9", ")": "0", "_": "-", "+": "=",
            "{": "[", "}": "]", ":": ";", '"': "'", "<": ",", ">": ".",
            "?": "/", "~": "`"}
SCAN = {}
for chars, first in _ROWS:
    for i, c in enumerate(chars):
        SCAN[c] = (first + i, False)
        if c.isalpha():
            SCAN[c.upper()] = (first + i, True)
for s, base in _SHIFTED.items():
    if base in SCAN:
        SCAN[s] = (SCAN[base][0], True)
SCAN[" "] = (0x39, False)
SCAN["`"] = (0x29, False)
SCAN["~"] = (0x29, True)

K = dict(ESC=0x01, BACKSPACE=0x0E, TAB=0x0F, RETURN=0x1C, SPACE=0x39,
         F1=0x3B, F2=0x3C, F5=0x3F, F6=0x40, F7=0x41, F8=0x42, F9=0x43, F10=0x44,
         HOME=0x47, UP=0x48, LEFT=0x4B, RIGHT=0x4D, DOWN=0x50, INSERT=0x52,
         DELETE=0x53, UNDO=0x61, HELP=0x62, LSHIFT=0x2A, CTRL=0x1D)


class BenchError(Exception):
    pass


def load_symbols(path):
    syms = {}
    for line in open(path):
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    return syms


class NeoST:
    def __init__(self, disk=None, diskb=None, gemdos=None, machine="st",
                 mono=False, mem="1m", symbols=None, extra=(), disk_ro=False):
        if not os.path.exists(NEOST):
            raise BenchError("neost-headless not found: %s (set NEOST=...)" % NEOST)
        args = [NEOST, ROM, "--machine", machine, "--mem", mem, "--fastfdc"]
        if disk:
            args += ["--disk", disk]
        if diskb:
            args += ["--diskb", diskb]
        if gemdos:
            args += ["--gemdos", gemdos]
        if mono:
            args += ["--mono"]
        if disk_ro:
            args += ["--disk-ro"]
        args += list(extra) + ["--server"]
        self.log = open(os.environ.get("BENCH_LOG", os.devnull), "a")
        self.p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=self.log, text=True, bufsize=1)
        self.syms = load_symbols(symbols or os.path.join(ROOT, "build", "tosfc.sym"))
        self.frame = 0

    # ---- protocole ----
    def cmd(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()
        r = self.p.stdout.readline().strip()
        if not r.startswith("ok"):
            raise BenchError("%s -> %s" % (line, r or "(no answer: emulator died)"))
        return r[3:]

    def run(self, frames):
        r = self.cmd("run %d" % frames)
        for tok in r.split():
            if tok.startswith("frame="):
                self.frame = int(tok[6:])
        return r

    def peek(self, addr, n):
        out = b""
        while n > 0:
            k = min(n, 4096)
            out += bytes.fromhex(self.cmd("peek %x %d" % (addr, k)))
            addr += k
            n -= k
        return out

    def long(self, addr):
        return int.from_bytes(self.peek(addr, 4), "big")

    def word(self, addr):
        return int.from_bytes(self.peek(addr, 2), "big")

    def shot(self, path):
        self.cmd("shot " + path)

    def close(self):
        try:
            self.cmd("quit")
        except Exception:
            pass
        self.p.wait(timeout=10)

    # ---- entrees ----
    def key(self, scan, shift=False, ctrl=False, hold=3, after=4):
        if shift:
            self.cmd("key make %x" % K["LSHIFT"])
        if ctrl:
            self.cmd("key make %x" % K["CTRL"])
        self.cmd("key make %x" % scan)
        self.run(hold)
        self.cmd("key break %x" % scan)
        if ctrl:
            self.cmd("key break %x" % K["CTRL"])
        if shift:
            self.cmd("key break %x" % K["LSHIFT"])
        self.run(after)

    def press(self, name, **kw):
        self.key(K[name], **kw)

    def type(self, text):
        for c in text:
            sc, sh = SCAN[c]
            self.key(sc, shift=sh)

    def mouse(self, dx, dy, buttons=0, frames=2):
        # Le paquet IKBD relatif porte au plus +-127 par axe et par envoi.
        while dx or dy:
            sx = max(-100, min(100, dx))
            sy = max(-100, min(100, dy))
            self.cmd("mouse %d %d %d" % (sx, sy, buttons))
            self.run(1)
            dx -= sx
            dy -= sy
        self.cmd("mouse 0 0 %d" % buttons)
        self.run(frames)

    # ---- le programme ----
    def basepage(self):
        os_header = self.long(0x4F2)
        p_run = self.long(os_header + 0x28)
        return self.long(p_run)

    def text_base(self):
        return self.long(self.basepage() + 8)

    def sym(self, name):
        return self.text_base() + self.syms[name]

    def running(self):
        """TOSFC est-il le programme courant ?"""
        try:
            bp = self.basepage()
            tb = self.long(bp + 8)
            return self.peek(tb + self.syms["_start"], 4) == bytes.fromhex("2a6f0004")
        except Exception:
            return False

    def screen(self):
        raw = self.peek(self.sym("scr_chr"), 25 * 80)
        rows = []
        for y in range(25):
            line = raw[y * 80:(y + 1) * 80]
            rows.append("".join(GLYPHS.get(b, chr(b) if 32 <= b < 127 else "·") for b in line))
        return rows

    def attrs(self):
        return self.peek(self.sym("scr_att"), 25 * 80)

    def text(self):
        return "\n".join(self.screen())

    def wait_for(self, needle, frames=3000, step=10):
        waited = 0
        while waited <= frames:
            if self.running() and needle in self.text():
                return True
            self.run(step)
            waited += step
        return False

    def wait_gone(self, needle, frames=3000, step=10):
        waited = 0
        while waited <= frames:
            if self.running() and needle not in self.text():
                return True
            self.run(step)
            waited += step
        return False

    def var(self, name, size=2, signed=True):
        v = int.from_bytes(self.peek(self.sym(name), size), "big", signed=signed)
        return v


class Checks:
    """Compte les controles d'un banc et resume."""

    def __init__(self, name):
        self.name = name
        self.ok = 0
        self.failed = []

    def check(self, cond, what):
        if cond:
            self.ok += 1
            print("  ok   " + what)
        else:
            self.failed.append(what)
            print("  FAIL " + what)
        return cond

    def done(self):
        total = self.ok + len(self.failed)
        print("%s: %d/%d checks" % (self.name, self.ok, total))
        return 0 if not self.failed else 1


if __name__ == "__main__":
    st = NeoST(disk=sys.argv[1] if len(sys.argv) > 1 else None)
    st.wait_for("bytes free", 3000)
    print(st.text())
    st.close()
