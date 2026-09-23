#!/usr/bin/env python3
"""music.py -- la musique en tache de fond.

YM (LHA -lh5-) : 50 appels par seconde quelle que soit la frequence de
l'ecran, et les registres ecrits sont ceux de la partition (recalculee par
tools/chiptune.py). SNDH compresse ICE! et en clair : le lecteur 68000 du
morceau tourne (son compteur de pas avance), sous-morceau suivant. Pause,
arret : le vecteur etv_timer revient a sa valeur d'origine, y compris en
quittant TOSFC pendant la lecture. Un morceau abime est refuse. Enfin, un
vrai son sort : la sortie audio de NeoST est enregistree et mesuree.
"""
import os
import struct
import subprocess
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tosfc import TOSFC, Checks, DISK, ROOT, scratch, copy_disk, image, fat12  # noqa: E402
from neost import NEOST, ROM, load_symbols  # noqa: E402

sys.path.insert(0, os.path.join(ROOT, "tools"))
import chiptune  # noqa: E402

MASK = [0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0x3F, 0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F]


def expected_regs(frame):
    regs = chiptune.frames()[frame]
    out = []
    for r in range(14):
        v = regs[r] & MASK[r]
        if r == 7:
            v |= 0xC0
        out.append(v)
    return out


def sndh_vars(st):
    base = st.long(st.sym("sndh_base"))
    off = load_symbols(os.path.join(ROOT, "build", "demo_sndh.sym"))["vars"]
    raw = st.peek(base + off, 10)
    tune, frame, step, steps = struct.unpack(">HHHI", raw)
    return tune, steps


def main():
    c = Checks("music")
    work = scratch()
    st = TOSFC(disk=copy_disk(DISK, work))
    try:
        st.boot()
        etv0 = st.long(0x400)
        st.go(0, "A:\\DEMO\\MUSIC\\")

        # ---- YM ----
        st.select(0, "WELCOME.YM")
        st.hit("RETURN")
        c.check(st.dialog() is None and st.long(0x400) == st.sym("music_etv"),
                "RETURN on a YM starts it, hooked on etv_timer")
        t0, f0 = st.var("music_ticks", 4, False), st.frame
        st.run(300)
        ticks = st.var("music_ticks", 4, False) - t0
        c.check(abs(ticks - 250) <= 2, "50 calls per second on a 60 Hz screen (%d in 300 frames)" % ticks)
        frame = st.var("ym_frame", 4)
        shadow = list(st.peek(st.sym("ym_shadow"), 14))
        # L'image de NeoST peut tomber au milieu de la routine du lecteur :
        # registres deja ceux de la trame suivante, compteur pas encore
        # avance. Les deux trames voisines sont donc justes.
        near = [expected_regs(f)[:13] for f in (frame - 1, frame)]
        c.check(shadow[:13] in near, "YM registers are the score's, frame %d (%s)" % (frame - 1, shadow[:13]))
        c.check(st.screen()[24][79] == "♪", "a note at the end of the key bar")
        st.letter("p")
        t1 = st.var("music_ticks", 4, False)
        st.run(100)
        c.check(st.var("music_ticks", 4, False) == t1, "P pauses")
        st.letter("p")
        st.run(50)
        c.check(st.var("music_ticks", 4, False) > t1, "P again resumes")
        st.letter("m")
        d = st.dialog()
        c.check(d and d[0] == "Music" and "Welcome to TOS File Cmd" in d[1] and any("YM5!" in l for l in d[1]),
                "M shows title and format (%s)" % (d[1] if d else None))
        st.letter("s")
        c.check(st.long(0x400) == etv0, "Stop restores etv_timer")

        # ---- SNDH compresse ICE! ----
        st.select(0, "TOSFC.SND")
        st.hit("RETURN")
        st.run(120)
        tune, steps = sndh_vars(st)
        c.check(tune == 1 and steps >= 15, "ICE-packed SNDH: its player runs (%d steps)" % steps)
        st.letter("m")
        st.letter("n")                              # sous-morceau suivant
        st.hit("ESC")
        st.run(30)
        tune, steps = sndh_vars(st)
        c.check(tune == 2, "Next switches to tune 2 of 2")

        # ---- SNDH en clair, puis quitter en jouant ----
        st.select(0, "PLAIN.SND")
        st.hit("RETURN")
        st.run(60)
        tune, steps = sndh_vars(st)
        c.check(tune == 1 and steps >= 5, "plain SNDH plays too")
        lo = st.text_base()
        hi = lo + st.syms["stack_top"]
        st.letter("q")
        st.press("RETURN")   # TOSFC s'en va : pas d'attente
        st.run(200)
        # Apres la sortie, le bureau installe son propre gestionnaire : le
        # vecteur ne doit simplement plus pointer dans la memoire de TOSFC.
        v = st.long(0x400)
        c.check(not st.running() and not (lo <= v < hi) and st.long(0x380) != 0x12345678,
                "quitting while playing unhooks (etv_timer $%x outside TOSFC) and leaves cleanly" % v)
    finally:
        st.close()

    # ---- morceau abime ----
    good = image(DISK).read("A:\\DEMO\\MUSIC\\WELCOME.YM")
    b = fat12.Builder("360k")
    b.add("BAD.YM", good[:300])
    b.add("NOISE.SND", bytes(range(256)) * 4)
    bad = os.path.join(work, "BAD.ST")
    open(bad, "wb").write(b.build())
    st = TOSFC(disk=copy_disk(DISK, work, "A2.ST"), diskb=bad)
    try:
        st.boot()
        st.go(0, "B:\\")
        for name in ("BAD.YM", "NOISE.SND"):
            st.select(0, name)
            st.hit("RETURN")
            d = st.dialog()
            c.check(d and "Not a YM or SNDH tune, or damaged" in d[1], "%s is refused" % name)
            st.hit("RETURN")
        c.check(st.var("music_kind", 2) == 0, "and nothing plays")
    finally:
        st.close()

    # ---- un vrai son ----
    wav = os.path.join(work, "sound.wav")
    disk = copy_disk(DISK, work, "A3.ST")
    keys = []
    frame = 400
    # Racine : AUTO DEMO READ_ME TOSFC -> DEMO ; DEMO : .. ARCHIVES MANY MUSIC ... -> MUSIC ;
    # MUSIC : .. PLAIN.SND TOSFC.SND WELCOME.YM -> WELCOME.YM.
    for sc in ["50", "1c", "50", "50", "50", "1c", "50", "50", "50", "1c"]:
        keys += ["--scancode-at", str(frame), sc]
        frame += 40
    subprocess.run([NEOST, ROM, "--machine", "st", "--mem", "1m", "--disk", disk, "--fastfdc",
                    "--frames", str(frame + 300), "--sound-dump", wav] + keys,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    w = wave.open(wav)
    rate = w.getframerate()
    data = w.readframes(w.getnframes())
    samples = struct.unpack("<%dh" % (len(data) // 2), data)

    def level(t0, t1):
        a, b = int(t0 * rate) * 2, int(t1 * rate) * 2
        seg = samples[a:b]
        mean = sum(seg) / max(1, len(seg))
        return (sum((s - mean) ** 2 for s in seg) / max(1, len(seg))) ** 0.5

    before = level(300 / 60, 380 / 60)
    after = level((frame + 60) / 60, (frame + 280) / 60)
    c.check(after > 300 and after > 20 * (before + 1),
            "the YM2149 is heard: level %.0f while playing, %.0f before" % (after, before))
    return c.done()


if __name__ == "__main__":
    sys.exit(main())
