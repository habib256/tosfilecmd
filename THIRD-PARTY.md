# Third-party code

TOS File Cmd itself contains no third-party code. The host tests and the
demo-disk build use one external tool:

| Component | Where | Licence | Used for |
|---|---|---|---|
| unice68 2.x, from sc68, by Benjamin Gerard | `tests/ext/unice68/` | GPL v3 or later (`COPYING`) | Packing test vectors and the demo SNDH with ICE! 2.4, and cross-checking TOSFC's own depacker (`src/ice.c`). Compiled for the host only (`build/host/icetool`), never linked into TOSFC.PRG. |

`tests/ext/unice68/icetool.c` is TOSFC's small command-line wrapper around it.
The copy comes from the DeaDBeeF repository (`plugins/sc68/libsc68/unice68`);
a later copy with explicit buffer sizes was not used, because its range check
on back-references looks at the packed buffer instead of the output.
