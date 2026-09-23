#!/usr/bin/env python3
"""memory.py -- la pile reellement utilisee, mesuree en faisant travailler
le programme.

crt0.S remet la BSS a zero, piles comprises : apres une session qui copie et
supprime une arborescence, ouvre des boites et declenche une erreur
critique, le mot non nul le plus bas de chaque pile donne son creux maximal.
On le compare a la pile reservee et a l'estimation statique de
tools/check_budget.py, qui doit rester un majorant.
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tosfc import TOSFC, Checks, DISK, ROOT, scratch, copy_disk  # noqa: E402


def used(st, lo, hi):
    raw = st.peek(st.sym(lo), st.syms[hi] - st.syms[lo])
    for i in range(0, len(raw), 2):
        if raw[i] or raw[i + 1]:
            return len(raw) - i
    return 0


def main():
    c = Checks("memory")
    st = TOSFC(disk=copy_disk(DISK, scratch()))
    try:
        st.boot()
        st.go(0, "A:\\DEMO\\")
        st.go(1, "A:\\")
        st.select(0, "NESTED")
        st.letter("c")
        st.hit("RETURN")
        st.go(0, "A:\\")
        st.select(0, "NESTED")
        st.letter("d")
        st.letter("d")
        st.letter("?")
        st.hit("ESC")
        st.focus(1)
        st.letter("l")
        st.select(1, "B:")
        st.press("RETURN")
        st.wait_dialog("Disk error")
        st.letter("c")
        st.idle()
        if st.dialog():
            st.hit("RETURN")
        main_used = used(st, "stack_bottom", "stack_top")
        critic_used = used(st, "critic_stack", "critic_stack_top")
        main_size = st.syms["stack_top"] - st.syms["stack_bottom"]
        critic_size = st.syms["critic_stack_top"] - st.syms["critic_stack"]
    finally:
        st.close()
    out = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "check_budget.py"),
                          os.path.join(ROOT, "build", "TOSFC.PRG"), os.path.join(ROOT, "build")],
                         capture_output=True, text=True).stdout
    est = int(re.search(r"main stack: worst case (\d+)", out).group(1))
    cest = int(re.search(r"critical-error stack: worst case (\d+)", out).group(1))
    print("  main stack used %d of %d (static estimate %d)" % (main_used, main_size, est))
    print("  critical-error stack used %d of %d (static estimate %d)" % (critic_used, critic_size, cest))
    c.check(main_used > 0 and critic_used > 0, "both stacks were exercised")
    c.check(main_used <= est, "static estimate bounds the measured main stack")
    c.check(critic_used <= cest, "static estimate bounds the measured critical-error stack")
    c.check(main_used < main_size * 3 // 4, "main stack keeps a quarter free")
    return c.done()


if __name__ == "__main__":
    sys.exit(main())
