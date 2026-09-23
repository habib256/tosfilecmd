#!/usr/bin/env python3
"""run_all.py -- tous les bancs NeoST, l'un apres l'autre, avec un resume.

    python3 bench/run_all.py            # tous
    python3 bench/run_all.py ops mouse  # certains

Chaque banc ecrit son journal dans $BENCH_OUT (defaut : un dossier
temporaire) ; le code de sortie est non nul si un seul controle echoue.
"""
import os
import re
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BENCHES = ["smoke", "ops", "data_safety", "mouse", "prefs", "viewers", "memory"]


def main():
    names = sys.argv[1:] or BENCHES
    out = os.environ.get("BENCH_OUT") or tempfile.mkdtemp(prefix="tosfc-bench-")
    os.makedirs(out, exist_ok=True)
    total_ok = total = 0
    failed = []
    for name in names:
        t0 = time.time()
        log = os.path.join(out, name + ".log")
        with open(log, "w") as f:
            r = subprocess.run([sys.executable, os.path.join(HERE, name + ".py")],
                               stdout=f, stderr=subprocess.STDOUT)
        text = open(log).read()
        m = re.search(r"(\d+)/(\d+) checks", text)
        ok, n = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
        total_ok += ok
        total += n
        status = "ok" if r.returncode == 0 and m and ok == n else "FAIL"
        if status != "ok":
            failed.append(name)
        print("%-12s %-4s %3d/%-3d checks  %5.0f s  %s" % (name, status, ok, n, time.time() - t0, log))
    print("benches: %d/%d checks%s" % (total_ok, total, "" if not failed else ", failed: " + " ".join(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
