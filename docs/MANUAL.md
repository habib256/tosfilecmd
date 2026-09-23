# TOS File Cmd 0.4 — Manual

## 1. Starting

TOS File Cmd (TOSFC) runs on any Atari ST, STE or Mega ST with TOS or
EmuTOS, from 512 KB of memory, on a colour monitor (medium resolution) or a
monochrome monitor (high resolution). In low resolution it switches to
medium while it runs and restores low resolution and your colours when you
quit.

- **From the floppy**: boot `TOSFC-0.4.0.st` (or `-SS.st` for a single-sided
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
║✓BIG      BIN│   90,000│05/05/95│12:00║   ✓ = tagged
╟─────────────┴─────────┴────────┴─────╢
║BIG.BIN      Archive                  ║   the selected file, or the tag count
╚═════════ 467,968 bytes free ═════════╝
?Help TView IPic EEdit CCopy ...             the key bar (clickable)
```

The active panel has its path highlighted and a selection bar. Operations
work on the tagged entries of the active panel, or on the selected entry if
nothing is tagged, and copy or move towards the folder of the other panel.

## 3. Keys

| Key | Action |
|---|---|
| `TAB` | Switch panels |
| `RETURN`, `F3` | Open the selected folder, drive, disk image or archive; view a file (pictures full screen, anything else as text) |
| `T` · `I` · `H` | Read the file as text · show it as a picture · show it in hex |
| `E`, `F4` | Edit the file; on a folder or `..`, create a new text file |
| `M` · `P` | Music box · pause or resume the tune |
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
| `F` | Floppy tools: read to an image, write an image, copy, format |
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
Degas Elite `.PC1` `.PC2` `.PC3`, NEOchrome `.NEO` and Spectrum 512 `.SPU`
`.SPC` are shown full screen in their own resolution and palette.

Spectrum 512 pictures use 48 colours on every line: TOSFC rewrites the
palette 48 times per line, at exactly the moments the format expects, and
switches the screen to 50 Hz while it does. The mouse is switched off
during the display (it comes back afterwards) and background music may
stutter: the routine keeps the processor for almost the whole frame.

- Left / Right (or a click, or SPACE) show the previous / next picture of
  the folder; files that are not pictures, or are damaged, are skipped.
  When you come back, the panel's selection is on the last picture shown.
- On a monochrome monitor, a colour picture is dithered to black and white.
  On a colour monitor, a monochrome picture is shown in white, grey and
  black at medium resolution.
- Any other key, or a right click, returns to the panels.

## 6. Editing text

`E` (or `F4`) opens the selected file in the editor. On a folder or on
`..`, it asks for the name of a new file to create in the panel's folder.

| Key | Action |
|---|---|
| Arrows | Move |
| Shift+Left / Shift+Right | Start / end of the line |
| Shift+Up / Shift+Down | One page |
| `Clr Home` · Shift+`Clr Home` | Beginning · end of the text |
| `Backspace` · `Delete` | Delete before · after the cursor |
| `RETURN` · `TAB` | New line · tab |
| `Ctrl+Y` | Cut the line |
| `F10`, `Ctrl+S` | Save |
| `ESC` | Close (asks Save / Discard / Cancel if the text changed) |

Long lines scroll sideways; they are never wrapped into the file. The top
line shows the line, the column, the size and the line-end style.

Saving writes the text to `TOSFC.$ED`, closes it and reads it back; only
then does the original become `TOSFC.BAK`, the new copy take its name, and
`TOSFC.BAK` go away. If anything fails, the file on disk is left as it was
and the text stays open, so you can save it elsewhere. Line ends are kept
in the file's own style (CRLF, LF or CR).

Some files are not opened, because editing them would lose something:
files larger than the free memory, files with a read error, read-only
files (clear the attribute with `A`), files containing zero bytes (not
text), files mixing several line-end styles, and 1st Word documents (their
formatting codes would be damaged; read them with `T`).

## 7. Music

`RETURN` on a `.YM`, `.SND` or `.SNDH` file plays it in the background: you
keep working in the panels while it plays. A note at the end of the key
bar shows that a tune is playing.

- **YM** files (YM2, YM3, YM3b, YM4, YM5, YM6), compressed with LHA as
  they usually are, or not. The 14 sound-chip registers are played; the
  Atari special effects of YM4-YM6 files (digidrums, SID voices) are not.
- **SNDH** tunes, packed with ICE! or not. An SNDH file is a 68000 music
  program: TOSFC runs its own code, at the rate its header asks for. A
  faulty tune can crash the machine like any program; avoid playing an
  unknown tune during a long copy.
- `P` pauses and resumes. `M` opens the Music box: title, author, format,
  tune number and time, with Pause, Stop and, for SNDH files with several
  tunes, Prev and Next.
- Quitting TOSFC stops the music.

## 8. Copying and moving

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

## 9. Deleting

**D** lists what will go and warns when folders are included. The default
button is **Cancel**: press **D** (or click Delete) to confirm. Read-only
files are not deleted; the folders that contain them stay.

## 10. Attributes

**A** shows Read-only, Hidden, System and Archive for the selected file
(or the first tagged file). Press the first letter of a flag to switch it,
then **O** (OK) to apply it to the selected or tagged files. Folders are
left as they are.

## 11. Disk images and archives

RETURN on one of these files opens it like a folder:

| Kind | Files | What TOSFC reads |
|---|---|---|
| Floppy images | `.ST`, `.MSA` | FAT12 floppies of any size up to 11 sectors and 86 tracks; MSA tracks compressed or not |
| LHA archives | `.LZH`, `.LHA` | Methods `-lh0-` (stored) and `-lh5-`, headers of level 0, 1 and 2, folders |
| ZIP archives | `.ZIP` | Stored and deflated files, folders |
| ARC archives | `.ARC` | Methods 1 to 4 (stored, packed, squeezed) and 8, 9 (crunched, squashed) |

The path at the top shows where you are (`A:\DEMO\ARCHIVES\TEXTS.LZH\TEXTS\`);
on `..` at the top the info line describes the file, and the bottom line
says **Read-only**. ESC at the top closes it and selects it again.

- **Copy out** with **C** (or F5): files and whole folders, to the folder in
  the other panel. It is the ordinary copy — created only where nothing
  exists, read back and compared — and every file from an archive is checked
  against the checksum stored with it (CRC-16 or CRC-32) before a byte is
  written.
- **View and play** from inside: texts, pictures and music open with RETURN
  as they do on a disk.
- **Read-only**: move, delete, rename, make folder, attributes, edit, and
  copying *into* an image or archive are refused with a message.
- **Names**: long or odd names are shown in 8.3, in capitals, with characters
  GEMDOS refuses replaced by `_`; two names that would clash get `~1`, `~2`.
- **Not handled**: encrypted ZIP entries, other compression methods (listed,
  but copying them says *Compression method not supported*), an archive
  inside an archive, and more than 1,024 entries. A damaged file says
  *Image or archive damaged* and nothing is written. If the image or archive
  changes on disk while it is open (replaced from the other panel, another
  floppy inserted), TOSFC refuses to read it until you open it again.
- **Memory**: a file inside an archive is unpacked in memory, so it must fit
  in free memory (about 150 KB on a 520 ST). Floppy images are read sector
  by sector and have no such limit.

## 12. Floppy tools

**F** opens the floppy tools:

- **Read** — makes an image of a floppy: choose the drive, check the size
  shown (from the floppy's boot sector) and give the image a name
  (`DISK.ST`). The image is written to the folder of the other panel, with
  the same care as a copy: never over an existing file without asking, read
  back and compared with the floppy (a floppy that reads differently twice is
  reported). The other panel must not be on the floppy being read.
- **Write** — writes the `.ST` or `.MSA` image selected in the active panel
  to a floppy. Each track is formatted to the image's layout, written, read
  back and compared. The image must not be on the floppy it is written to.
- **Dup** — copies a whole floppy. With two drives, choose A: to B: or B: to
  A:. With one drive, TOSFC reads as much of the source as memory holds
  (a few passes on a 520 ST, two with 1 MB) and asks you to swap the
  source and target floppies until it is done. It checks each floppy you insert: if the source
  is still in the drive when it wants the target, it asks again.
- **Format** — 720 KB (9 sectors, 2 sides), 800 KB (10 sectors), 880 KB
  (11 sectors; not every drive manages it) or 360 KB (single-sided). The new
  floppy gets a fresh serial number, so GEMDOS never mistakes it for another.

Protection:

- The floppy TOSFC was started from is never written to, formatted or copied
  over, whatever drive it is in.
- Every destructive command names the drive and asks first; the default
  button is Cancel.
- Track 0 (boot sector and FAT) is written last. If you stop with ESC, or an
  error happens, TOSFC says the floppy is incomplete: it will not look like a
  good copy.
- After writing, GEMDOS is made to read the floppy afresh, so the panels show
  what is really on it.
- A disk copy with one drive cannot tell the source from an earlier copy of
  it (their boot sectors are identical): format such a target first.

## 13. Disk errors

When a floppy is write-protected, missing or unreadable, TOSFC shows the
error and the drive with **Retry** and **Cancel**. Cancel lets the current
operation fail cleanly: files it created are removed and replaced files
are put back. On a single-drive machine, TOSFC asks you to insert the
disk for B: when needed.

## 14. Options

**O** opens the options:

- **Verify**: read back every copy (on by default). Moves are always verified.
- **Hidden**: show hidden and system files (on by default).
- **Save**: write `TOSFC.INF` in the folder TOSFC started from, with both
  panels, their sort orders and the options. Saved panels on a hard disk or
  on the program's own drive reopen next time; TOSFC never asks for another
  floppy at start-up.

## 15. Limits

- 1,024 entries per panel; beyond that the panel says so.
- Folders nested 16 deep at most, paths of 127 characters.
- The editor holds files up to the free memory (about 150 KB on a 520 ST).
- Inside an archive, a file must fit in free memory to be viewed or copied.
- Creating archives and writing into images are not planned; program
  launching and tools are for the next versions.
