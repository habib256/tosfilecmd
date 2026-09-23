#!/usr/bin/env python3
"""tosfc.py -- jouer de TOS File Cmd comme un utilisateur, et lire son ecran.

Au-dessus de neost.py : on lit les panneaux a l'ecran (chemin, entrees,
selection, panneau actif) et les boites de dialogue, puis on navigue au
clavier comme on le ferait devant la machine. Apres chaque touche on attend
que l'image video soit stable (empreinte `screen=` de NeoST) : les bancs ne
dependent pas de la vitesse du programme.
"""
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from neost import NeoST, K, BenchError, ROOT, Checks  # noqa: E402,F401

sys.path.insert(0, os.path.join(ROOT, "tools"))
import fat12  # noqa: E402

A_CURSOR, A_CURTAG, A_DIALOG, A_DLGSEL, A_BARTXT = 3, 4, 5, 6, 8
VERSION = open(os.path.join(ROOT, "Makefile")).read().split("VERSION = ")[1].split()[0]
DISK = os.path.join(ROOT, "dist", "TOSFC-%s.st" % VERSION)
DISK_SS = os.path.join(ROOT, "dist", "TOSFC-%s-SS.st" % VERSION)
PRG = os.path.join(ROOT, "build", "TOSFC.PRG")


def scratch(prefix="tosfc-bench-"):
    return tempfile.mkdtemp(prefix=prefix)


def copy_disk(src, dst_dir, name="A.ST"):
    """Les bancs ecrivent : ils travaillent toujours sur une copie jetable.
    La disquette doit porter le TOSFC.PRG construit : sinon les symboles lus
    dans build/tosfc.sym ne designeraient pas les bonnes variables."""
    img = fat12.Image(open(src, "rb").read())
    if src in (DISK, DISK_SS) and img.read("A:\\AUTO\\TOSFC.PRG") != open(PRG, "rb").read():
        raise BenchError("%s is older than build/TOSFC.PRG: run make disk" % src)
    dst = os.path.join(dst_dir, name)
    shutil.copyfile(src, dst)
    return dst


def hd_with_program(root):
    """Un disque GEMDOS hote qui demarre TOSFC depuis son dossier AUTO."""
    os.makedirs(os.path.join(root, "AUTO"), exist_ok=True)
    shutil.copyfile(PRG, os.path.join(root, "AUTO", "TOSFC.PRG"))
    return root


def entry_name(cells):
    """'NAME     EXT' (12 colonnes) -> 'NAME.EXT'."""
    base, ext = cells[:8].strip(), cells[9:12].strip()
    if base.startswith("↑"):
        return ".."
    return base + ("." + ext if ext else "")


class TOSFC(NeoST):
    def __init__(self, **kw):
        super().__init__(**kw)
        self.booted = False

    # ---- attente ----
    def screen_hash(self):
        r = self.cmd("observe")
        return [t for t in r.split() if t.startswith("screen=")][0]

    def settle(self, stable=6, limit=3000):
        last, same, waited = self.screen_hash(), 0, 0
        while same < stable and waited < limit:
            self.run(1)
            waited += 1
            h = self.screen_hash()
            if h == last:
                same += 1
            else:
                same, last = 0, h
        return waited

    def boot(self, frames=4000):
        if not self.wait_for("╚", frames):
            raise BenchError("TOSFC did not show its panels")
        self.idle()
        self.booted = True

    def idle(self, frames=30000):
        """Attend que TOSFC attende l'utilisateur : son compteur de sondages
        sans evenement (in_idle_polls) doit repartir, sur plusieurs trames."""
        waited = 0
        self.run(4)                      # que la touche envoyee soit lue
        last = self.var("in_idle_polls", 4, signed=False)
        while waited < frames:
            self.run(3)
            waited += 3
            now = self.var("in_idle_polls", 4, signed=False)
            if now >= last + 2:
                self.run(3)              # la derniere image est dessinee
                return
            last = now
        raise BenchError("TOSFC still busy after %d frames; screen:\n%s" % (frames, self.text()))

    def hit(self, name, **kw):
        self.press(name, **kw)
        self.idle()

    def letter(self, c):
        self.type(c)
        self.idle()

    # ---- lecture de l'ecran ----
    def panel(self, side):
        rows, att = self.screen(), self.attrs()
        x0 = 40 * side
        title = rows[0][x0 + 1:x0 + 39].strip("═╤ ")
        entries, cursor = [], None
        for y in range(2, 21):
            cells = rows[y][x0 + 2:x0 + 14]
            name = entry_name(cells)
            if not name:
                break
            entries.append(name)
            if att[y * 80 + x0 + 3] in (A_CURSOR, A_CURTAG):
                cursor = len(entries) - 1
        tagged = [entry_name(rows[y][x0 + 2:x0 + 14]) for y in range(2, 21)
                  if rows[y][x0 + 1] == "✓"]
        return dict(path=title, entries=entries, cursor=cursor, tagged=tagged,
                    info=rows[22][x0 + 1:x0 + 39].rstrip(),
                    free=rows[23][x0 + 1:x0 + 39].strip("═ "),
                    active=any(att[x] == A_BARTXT for x in range(x0, x0 + 40)))

    def active(self):
        return 0 if self.panel(0)["active"] else 1

    def dialogs(self):
        """Toutes les boites visibles : [(titre, lignes)]."""
        rows, att = self.screen(), self.attrs()
        out = []
        for y in range(25):
            for x in range(80):
                if rows[y][x] != "╔" or att[y * 80 + x] != A_DIALOG:
                    continue
                # Le coin droit, sans croiser le coin d'une autre boite.
                x1 = x + 1
                while x1 < 80 and rows[y][x1] not in "╗╔":
                    x1 += 1
                if x1 < 80 and rows[y][x1] == "╗":
                    title = rows[y][x + 1:x1].strip("═ ")
                    lines = []
                    for yy in range(y + 1, 25):
                        if rows[yy][x] == "╚":
                            break
                        lines.append(rows[yy][x + 1:x1].strip("║ "))
                    out.append((title, [ln for ln in lines if ln]))
        return out

    def dialog(self):
        """La boite au premier plan : une question ou une erreur l'emporte
        sur la barre de progression qu'elle recouvre."""
        boxes = self.dialogs()
        for b in boxes:
            if b[0] not in ("Copying", "Moving", "Deleting"):
                return b
        return boxes[0] if boxes else None

    def dialog_text(self):
        d = self.dialog()
        return "" if d is None else d[0] + "\n" + "\n".join(d[1])

    # ---- navigation ----
    def focus(self, side):
        if self.active() != side:
            self.hit("TAB")

    def select(self, side, name, limit=1100):
        self.focus(side)
        name = name.upper()
        self.hit("HOME")
        seen = 0
        while seen < limit:
            p = self.panel(side)
            if p["cursor"] is not None and p["entries"][p["cursor"]] == name:
                return True
            before = (p["entries"], p["cursor"])
            self.hit("DOWN")
            p2 = self.panel(side)
            if (p2["entries"], p2["cursor"]) == before:
                break
            seen += 1
        raise BenchError("%s not found in panel %d: %s" % (name, side, self.panel(side)))

    def open(self, side, name):
        """Ouvre un dossier ou un lecteur : l'ecran ne bouge pas pendant
        l'acces disque, on attend donc le nouveau chemin (ou une boite)."""
        self.select(side, name)
        self.hit("RETURN")

    def until(self, cond, frames=6000, step=5):
        waited = 0
        while waited < frames:
            if cond():
                return True
            self.run(step)
            waited += step
        raise BenchError("condition not met after %d frames; screen:\n%s" % (frames, self.text()))

    def go(self, side, path):
        """Ouvre path ("C:\\DIR\\SUB\\") dans le panneau side."""
        self.focus(side)
        if self.panel(side)["path"] != "Drives":
            self.letter("l")
        self.open(side, path[:2])
        for part in [p for p in path[3:].split("\\") if p]:
            self.open(side, part)
        got = self.panel(side)["path"]
        if got != path.upper():
            raise BenchError("wanted %s, panel shows %s" % (path, got))

    def tag(self, side, *names):
        for n in names:
            self.select(side, n)
            self.hit("SPACE")

    def wait_dialog(self, title, frames=6000):
        waited = 0
        while waited < frames:
            d = self.dialog()
            if d and d[0] == title:
                return d
            self.run(10)
            waited += 10
        raise BenchError("dialog %r never appeared; screen:\n%s" % (title, self.text()))

    def wait_idle(self, frames=30000):
        """Attend la fin d'une operation (ou la boite qui l'interrompt)."""
        self.idle(frames)


def host_find(root, path):
    """Chemin hote d'un chemin GEMDOS C:\\A\\B (insensible a la casse)."""
    cur = root
    for part in [p for p in path[3:].split("\\") if p]:
        for n in os.listdir(cur):
            if n.upper() == part.upper():
                cur = os.path.join(cur, n)
                break
        else:
            return None
    return cur


def host_read(root, path):
    p = host_find(root, path)
    if p is None or os.path.isdir(p):
        return None
    return open(p, "rb").read()


def image(path):
    return fat12.Image(open(path, "rb").read())
