#!/usr/bin/env python3
"""test_lha.py -- le compresseur de tools/lha.py contre un decompresseur
independant (lhasa : `lha p`). Saute si lhasa n'est pas installe."""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_lzh  # noqa: E402

def main():
    lha_bin = shutil.which("lha")
    if not lha_bin:
        print("test_lha: lhasa not installed, skipped")
        return 0
    fails = 0
    tmp = tempfile.mkdtemp()
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))
    import lha
    for name, data in gen_lzh.cases().items():
        path = os.path.join(tmp, name + ".lzh")
        open(path, "wb").write(lha.archive(name.upper() + ".BIN", data))
        r = subprocess.run([lha_bin, "pq", path], capture_output=True)
        if r.stdout != data:
            fails += 1
            print("FAIL %s: lhasa reads %d bytes, expected %d" % (name, len(r.stdout), len(data)))
    print("test_lha: %d cases, %d failed" % (len(gen_lzh.cases()), fails))
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())
