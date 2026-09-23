# TOS File Cmd 0.1 — Manual

## 1. Starting

TOS File Cmd (TOSFC) runs on any Atari ST, STE or Mega ST with TOS or
EmuTOS, from 512 KB of memory, on a colour monitor (medium resolution) or a
monochrome monitor (high resolution). In low resolution it switches to
medium while it runs and restores low resolution and your colours when you
quit.

- **From the floppy**: boot `TOSFC-0.1.0.st` (or `-SS.st` for a single-sided
  drive). TOSFC starts from the `AUTO` folder.
- **From the desktop**: open `TOSFC.PRG`.

The left panel opens the folder TOSFC was started from; the right panel
lists the drives.

## 2. The screen

```
╔═════════════╤══ A:\DEMO\ ═╤════════╤═════╗
║    Name↓    │  Size   │  Date  │Time ║   column titles: the arrow marks the sort
║ ↑..         │     <UP>│        │     ║   the parent folder
║ MANY        │    <DIR>│23/09/26│12:00║
║✓BIG      BIN│  120,000│05/05/95│12:00║   ✓ = tagged
╟─────────────┴─────────┴────────┴─────╢
║BIG.BIN      Archive                  ║   the selected file, or the tag count
╚═════════ 467,968 bytes free ═════════╝
?Help CCopy VMove RRename DDelete ...        the key bar (clickable)
```

The active panel has its path highlighted and a selection bar. Operations
work on the tagged entries of the active panel, or on the selected entry if
nothing is tagged, and copy or move towards the folder of the other panel.

## 3. Keys

| Key | Action |
|---|---|
| `TAB` | Switch panels |
| `RETURN` | Open the selected folder or drive |
| `ESC`, `Backspace` | Go up; at the root of a drive, show the drive list |
| Up / Down | Select the previous / next entry (Shift: a page) |
| Left / Right | Previous / next page |
| `Clr Home` | First entry (Shift: last) |
| `SPACE`, `Insert` | Tag or untag, then move down |
| `*` · `+` · `-` | Invert tags · tag all files · untag all |
| `C`, `F5` | Copy to the other panel |
| `V`, `F6` | Move to the other panel |
| `R` | Rename |
| `K`, `F7` | Make a folder |
| `D`, `F8`, `Delete` | Delete |
| `A`, `F2` | Attributes |
| `S`, `F9` | Sort: name, extension, size, date, disk order |
| `L` | Drive list |
| `O` | Options |
| `Ctrl+R` | Re-read both panels (after changing a floppy) |
| `?`, `Help`, `F1` | Help |
| `Q`, `F10` | Quit |

In a dialog, the first letter of a button chooses it, Left/Right/TAB move
between buttons, RETURN takes the highlighted one and ESC or Undo cancels.

## 4. Mouse

- Click an entry to select it; click it again to open it.
- Right-click an entry to tag or untag it.
- Click a column title to sort by it (Name twice: by extension; Time: disk
  order).
- Click the path at the top of a panel to go up.
- Click a command in the key bar, or a button in a dialog.

## 5. Copying and moving

1. Open the destination folder in one panel and the source in the other.
2. Tag what you want (or just select one entry), then **C** or **V**.
3. Confirm. A progress box shows the file, the count and a bar; **ESC**
   stops after the current block and removes the unfinished file.

When a file already exists, TOSFC shows both versions and asks: **Yes**,
**No**, **All** (replace every following one), **None** (keep every
following one) or **Cancel**. When a folder already exists, it asks whether
to copy into it; existing files inside are asked about one by one.

What TOSFC guarantees (details in [DATA-SAFETY.md](DATA-SAFETY.md)):

- a copy is read back and compared with its source (Options → Verify, on by
  default); a move is always verified before its source is deleted;
- a replaced file is kept as `TOSFC.BAK` until the new one is checked, and
  put back if anything fails; if a `TOSFC.BAK` is already there, TOSFC will
  not replace files in that folder until you deal with it;
- a folder is deleted by a move only if everything in it was moved;
- read-only files are copied with their attribute but never deleted.

A folder cannot be copied into itself. Dates of files are kept; dates of
folders are not (GEMDOS cannot set them).

## 6. Deleting

**D** lists what will go and warns when folders are included. The default
button is **Cancel**: press **D** (or click Delete) to confirm. Read-only
files are not deleted; the folders that contain them stay.

## 7. Attributes

**A** shows Read-only, Hidden, System and Archive for the selected file
(or the first tagged file). Press the first letter of a flag to switch it,
then **O** (OK) to apply it to the selected or tagged files. Folders are
left as they are.

## 8. Disk errors

When a floppy is write-protected, missing or unreadable, TOSFC shows the
error and the drive with **Retry** and **Cancel**. Cancel lets the current
operation fail cleanly: files it created are removed and replaced files
are put back. On a single-drive machine, TOSFC asks you to insert the
disk for B: when needed.

## 9. Options

**O** opens the options:

- **Verify**: read back every copy (on by default). Moves are always verified.
- **Hidden**: show hidden and system files (on by default).
- **Save**: write `TOSFC.INF` in the folder TOSFC started from, with both
  panels, their sort orders and the options. Saved panels on a hard disk or
  on the program's own drive reopen next time; TOSFC never asks for another
  floppy at start-up.

## 10. Limits

- 1,024 entries per panel; beyond that the panel says so.
- Folders nested 16 deep at most, paths of 127 characters.
- Viewers, the editor, disk images, archives and program launching are
  planned for the next versions.
