# TOS File Cmd 0.2 — Manual

## 1. Starting

TOS File Cmd (TOSFC) runs on any Atari ST, STE or Mega ST with TOS or
EmuTOS, from 512 KB of memory, on a colour monitor (medium resolution) or a
monochrome monitor (high resolution). In low resolution it switches to
medium while it runs and restores low resolution and your colours when you
quit.

- **From the floppy**: boot `TOSFC-0.2.0.st` (or `-SS.st` for a single-sided
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
?Help TText IImage CCopy VMove ...           the key bar (clickable)
```

The active panel has its path highlighted and a selection bar. Operations
work on the tagged entries of the active panel, or on the selected entry if
nothing is tagged, and copy or move towards the folder of the other panel.

## 3. Keys

| Key | Action |
|---|---|
| `TAB` | Switch panels |
| `RETURN`, `F3` | Open the selected folder or drive; view a file (pictures full screen, anything else as text) |
| `T` · `I` · `H` | Read the file as text · show it as a picture · show it in hex |
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

## 5. Viewing files

Viewers only read: nothing is ever written to the disk.

**Text** (`RETURN` on a file, or `T`): the file is shown 80 columns wide,
long lines wrapped between words. CR, LF and CRLF line ends are all
understood, tabs every 8 columns, and control codes appear as dots.
1st Word and 1st Word Plus documents (`.DOC`) are shown without their
format lines and style codes.

| Key | Action |
|---|---|
| Up / Down | One line |
| Left / Right, SPACE | One page (one line of overlap) |
| `Clr Home` · Shift+`Clr Home` | Beginning · end |
| `F` · `N` | Find text (any case) · find the next one |
| `H` | Switch between text and hex at the same place |
| `ESC`, `Q`, right click | Back to the panels |

A click in the upper half of the screen goes back a page, in the lower half
forward. The top line shows the name, the size and where you are. A file
too large for the free memory is shown from its beginning, and the top
line says so; a read error shows what could be read, and says so.

**Pictures** (`RETURN` on a picture, or `I`): Degas `.PI1` `.PI2` `.PI3`,
Degas Elite `.PC1` `.PC2` `.PC3` and NEOchrome `.NEO` are shown full
screen in their own resolution and palette.

- Left / Right (or a click, or SPACE) show the previous / next picture of
  the folder; files that are not pictures, or are damaged, are skipped.
  When you come back, the panel's selection is on the last picture shown.
- On a monochrome monitor, a colour picture is dithered to black and white.
  On a colour monitor, a monochrome picture is shown in white, grey and
  black at medium resolution.
- Any other key, or a right click, returns to the panels.

## 6. Copying and moving

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

## 7. Deleting

**D** lists what will go and warns when folders are included. The default
button is **Cancel**: press **D** (or click Delete) to confirm. Read-only
files are not deleted; the folders that contain them stay.

## 8. Attributes

**A** shows Read-only, Hidden, System and Archive for the selected file
(or the first tagged file). Press the first letter of a flag to switch it,
then **O** (OK) to apply it to the selected or tagged files. Folders are
left as they are.

## 9. Disk errors

When a floppy is write-protected, missing or unreadable, TOSFC shows the
error and the drive with **Retry** and **Cancel**. Cancel lets the current
operation fail cleanly: files it created are removed and replaced files
are put back. On a single-drive machine, TOSFC asks you to insert the
disk for B: when needed.

## 10. Options

**O** opens the options:

- **Verify**: read back every copy (on by default). Moves are always verified.
- **Hidden**: show hidden and system files (on by default).
- **Save**: write `TOSFC.INF` in the folder TOSFC started from, with both
  panels, their sort orders and the options. Saved panels on a hard disk or
  on the program's own drive reopen next time; TOSFC never asks for another
  floppy at start-up.

## 11. Limits

- 1,024 entries per panel; beyond that the panel says so.
- Folders nested 16 deep at most, paths of 127 characters.
- Spectrum 512 pictures, the text editor, music, disk images, archives and
  program launching are planned for the next versions.
