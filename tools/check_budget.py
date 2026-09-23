#!/usr/bin/env python3
"""check_budget.py -- le programme tient-il dans un 520 ST ?

    check_budget.py build/TOSFC.PRG build/

Controle, a chaque construction :
  - la taille chargee (texte + donnees + BSS) par rapport a son plafond ;
  - la pile : a partir du graphe d'appels de GCC (-fcallgraph-info=su), le
    chemin le plus profond depuis main, les fonctions recursives comptees a
    leur profondeur maximale (MAX_DEPTH de fsops.c), doit tenir dans la pile
    reservee par crt0.S, avec une marge pour les appels systeme ;
  - la pile du gestionnaire d'erreurs critiques, depuis critic_c.

On ne desactive jamais ce controle pour faire passer une construction : un
debordement de pile ecrit dans les donnees du programme, et de la dans les
fichiers.
"""
import glob
import os
import re
import struct
import sys

# Texte + donnees + BSS. Un 520 ST sous TOS 1.0 laisse environ 400 Ko au
# programme ; TOSFC y prend en plus ses panneaux (48 Ko) et, le temps d'une
# operation, ses tampons (jusqu'a 80 Ko) ou le fichier ouvert. bench/smoke.py
# et bench/viewers.py tournent sur une machine de 512 Ko.
LOAD_BUDGET = 160 * 1024
MAIN_STACK = 16384            # crt0.S : stack_bottom .. stack_top
CRITIC_STACK = 3072           # crt0.S : critic_stack
TOS_MARGIN = 1024             # appels systeme (trap : pile utilisateur intacte,
                              # mais les gestionnaires y empilent parfois)
RECURSION = {"do_tree": 16, "del_tree": 16, "scan_tree": 16}


def die(msg):
    sys.exit("check_budget: " + msg)


def load_callgraph(build):
    frames, calls = {}, {}
    for ci in glob.glob(os.path.join(build, "*.ci")):
        text = open(ci).read()
        for m in re.finditer(r'node: \{ title: "([^"]+)" label: "([^"]*)"', text):
            title, label = m.group(1), m.group(2)
            name = title.split(":")[-1]
            su = re.search(r"(\d+) bytes \((static|dynamic[^)]*)\)", label.replace("\\n", " "))
            if su:
                if "dynamic" in su.group(2) and "bounded" not in su.group(2):
                    die("unbounded dynamic stack in %s" % name)
                frames[name] = max(frames.get(name, 0), int(su.group(1)))
            else:
                frames.setdefault(name, 0)
        for m in re.finditer(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"', text):
            s, t = m.group(1).split(":")[-1], m.group(2).split(":")[-1]
            calls.setdefault(s, set()).add(t)
    return frames, calls


def deepest(fn, frames, calls, stack):
    """Profondeur maximale en octets depuis fn (adresse de retour comprise)."""
    if fn in stack:
        return 0                       # recursion : comptee par RECURSION
    own = frames.get(fn, 0) + 4
    mult = RECURSION.get(fn, 1)
    best = 0
    for callee in calls.get(fn, ()):
        if callee == fn:
            continue
        best = max(best, deepest(callee, frames, calls, stack | {fn}))
    return own * mult + best


def main():
    prg, build = sys.argv[1], sys.argv[2]
    hdr = open(prg, "rb").read(28)
    magic, tlen, dlen, blen = struct.unpack(">HIII", hdr[:14])
    if magic != 0x601A:
        die("not a TOS executable")
    load = tlen + dlen + blen
    print("TOSFC.PRG: text %d + data %d + bss %d = %d bytes (budget %d)"
          % (tlen, dlen, blen, load, LOAD_BUDGET))
    if load > LOAD_BUDGET:
        die("program too large: %d > %d" % (load, LOAD_BUDGET))

    frames, calls = load_callgraph(build)
    if not frames:
        die("no call graph: build with -fcallgraph-info=su")
    for fn in RECURSION:
        if fn not in frames:
            die("recursive function %s not found in the call graph" % fn)
    need = deepest("main", frames, calls, frozenset()) + TOS_MARGIN
    print("main stack: worst case %d bytes (reserved %d)" % (need, MAIN_STACK))
    if need > MAIN_STACK:
        die("main stack overflow: %d > %d" % (need, MAIN_STACK))
    cneed = deepest("critic_c", frames, calls, frozenset()) + 256
    print("critical-error stack: worst case %d bytes (reserved %d)" % (cneed, CRITIC_STACK))
    if cneed > CRITIC_STACK:
        die("critical-error stack overflow: %d > %d" % (cneed, CRITIC_STACK))
    # Toute fonction appelee par pointeur (rappels OPS) n'apparait pas dans
    # le graphe : on ajoute le pire rappel au pire chemin de fsops.
    cb = max(deepest(f, frames, calls, frozenset())
             for f in ("cb_ask", "cb_error", "cb_progress", "cb_cancel"))
    ops = max(deepest(f, frames, calls, frozenset())
              for f in ("cmd_copy", "cmd_delete")) + 64
    total = ops + cb + TOS_MARGIN
    print("operations + callbacks: worst case %d bytes (reserved %d)" % (total, MAIN_STACK))
    if total > MAIN_STACK:
        die("stack overflow through a callback: %d > %d" % (total, MAIN_STACK))


if __name__ == "__main__":
    main()
