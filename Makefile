# TOS File Cmd -- two-panel file manager, Atari ST.
#
#   make          build/TOSFC.PRG (and build/tosfc.sym, build/tosfc.map)
#   make disk     dist/TOSFC-<v>.st (720 KB) and dist/TOSFC-<v>-SS.st (360 KB)
#   make test     host tests: file operations with injected faults, TOSFC.INF,
#                 the FAT12 tool, and the memory budget of the program
#   make bench    NeoST benches (needs neost-headless, see bench/README.md)
#   make clean
#
# Toolchain: m68k-elf-gcc (Homebrew: brew install m68k-elf-gcc) and Python 3.
# No C library: src/libc.c and the TOS calls of src/tos.h are all there is.

VERSION = 0.4.0

CROSS   ?= m68k-elf-
CC      = $(CROSS)gcc
NM      = $(CROSS)nm
HOSTCC  ?= cc
PYTHON  ?= python3

BUILD = build
DIST  = dist

CFLAGS = -mcpu=68000 -Os -fomit-frame-pointer -ffreestanding \
         -fno-tree-loop-distribute-patterns -fno-common \
         -Wall -Wextra -Wno-unused-parameter -std=gnu99 -fcallgraph-info=su \
         --param=min-pagesize=0
LDFLAGS = -mcpu=68000 -nostdlib -T src/tosfc.ld -Wl,--emit-relocs \
          -Wl,-Map,$(BUILD)/tosfc.map -Wl,--no-warn-rwx-segments

OBJS = crt0.o main.o screen.o input.o ui.o panel.o fsops.o prefs.o view.o picture.o \
       textview.o edit.o edbuf.o spec512.o music.o musicisr.o lzh.o ice.o ym.o sndh.o \
       vfs.o inflate.o arc.o disk.o flop.o sys_tos.o libc.o
OBJS := $(addprefix $(BUILD)/,$(OBJS))
HDRS = $(wildcard src/*.h)

all: $(BUILD)/TOSFC.PRG

$(BUILD):
	mkdir -p $(BUILD) $(BUILD)/host

$(BUILD)/%.o: src/%.c $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.S | $(BUILD)
	$(CC) -mcpu=68000 $(ASFLAGS) -c $< -o $@

$(BUILD)/tosfc.elf: $(OBJS) src/tosfc.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

$(BUILD)/TOSFC.PRG: $(BUILD)/tosfc.elf tools/elf2prg.py tools/check_budget.py
	$(PYTHON) tools/elf2prg.py $< $@
	$(NM) -n $< > $(BUILD)/tosfc.sym
	$(PYTHON) tools/check_budget.py $@ $(BUILD)

# La musique de demonstration (tools/chiptune.py) : un YM5 en LHA -lh5-, et
# un SNDH assemble ici, en clair et compresse ICE! par unice68.
$(BUILD)/WELCOME.YM: tools/chiptune.py tools/lha.py | $(BUILD)
	$(PYTHON) tools/chiptune.py ym $@
$(BUILD)/PLAIN.SND: tools/chiptune.py | $(BUILD)
	$(PYTHON) tools/chiptune.py asm $(BUILD)/demo_sndh.S
	$(CC) -mcpu=68000 -c $(BUILD)/demo_sndh.S -o $(BUILD)/demo_sndh.o
	$(CROSS)objcopy -O binary -j .text $(BUILD)/demo_sndh.o $@
	$(NM) $(BUILD)/demo_sndh.o > $(BUILD)/demo_sndh.sym
$(BUILD)/TOSFC.SND: $(BUILD)/PLAIN.SND $(BUILD)/host/icetool
	$(BUILD)/host/icetool p $< $@

MUSIC_DEMO = $(BUILD)/WELCOME.YM $(BUILD)/PLAIN.SND $(BUILD)/TOSFC.SND

disk: $(BUILD)/TOSFC.PRG tools/mkdisk.py tools/fat12.py $(MUSIC_DEMO)
	$(PYTHON) tools/mkdisk.py $(VERSION) $(BUILD)/TOSFC.PRG $(DIST) $(BUILD)

HOST_CFLAGS = -std=c99 -Wall -Wextra -g -DTOSFC_HOST -fsanitize=address,undefined

$(BUILD)/host/test_fsops: tests/test_fsops.c tests/fakedos.c tests/fakedos.h \
                          src/fsops.c src/libc.c $(HDRS) | $(BUILD)
	$(HOSTCC) $(HOST_CFLAGS) -o $@ tests/test_fsops.c tests/fakedos.c src/fsops.c src/libc.c

$(BUILD)/host/test_prefs: tests/test_prefs.c tests/fakedos.c tests/fakedos.h \
                          src/prefs.c src/fsops.c src/libc.c $(HDRS) | $(BUILD)
	$(HOSTCC) $(HOST_CFLAGS) -o $@ tests/test_prefs.c tests/fakedos.c src/prefs.c \
	    src/fsops.c src/libc.c

$(BUILD)/host/test_view: tests/test_view.c src/picture.c src/textview.c src/libc.c \
                         $(HDRS) | $(BUILD)
	$(HOSTCC) $(HOST_CFLAGS) -o $@ tests/test_view.c src/picture.c src/textview.c src/libc.c

$(BUILD)/host/test_edit: tests/test_edit.c tests/fakedos.c src/edbuf.c src/fsops.c src/libc.c \
                         $(HDRS) | $(BUILD)
	$(HOSTCC) $(HOST_CFLAGS) -o $@ tests/test_edit.c tests/fakedos.c src/edbuf.c src/fsops.c src/libc.c

# unice68 (sc68, GPL v3+) : compresseur et decompresseur ICE! de reference,
# pour les vecteurs de test et la disquette de demonstration seulement.
ICE_SRC = tests/ext/unice68
$(BUILD)/host/icetool: $(ICE_SRC)/icetool.c $(ICE_SRC)/unice68_pack.c \
                       $(ICE_SRC)/unice68_unpack.c $(ICE_SRC)/unice68_version.c | $(BUILD)
	$(HOSTCC) -O2 -w -DNDEBUG -include stdint.h -include assert.h \
	    -DPACKAGE_NAME='"unice68"' -DPACKAGE_STRING='"unice68 2.0.0"' \
	    -DPACKAGE_VERSION='"2.0.0"' -DPACKAGE_URL='"sc68"' -DPACKAGE_BUGREPORT='"sc68"' \
	    -o $@ $(filter %.c,$^)

MUSIC_SRC = src/lzh.c src/ice.c src/ym.c src/sndh.c src/libc.c
$(BUILD)/host/test_music: tests/test_music.c $(MUSIC_SRC) $(HDRS) | $(BUILD)
	$(HOSTCC) $(HOST_CFLAGS) -o $@ tests/test_music.c $(MUSIC_SRC)

VFS_SRC = src/vfs.c src/lzh.c src/inflate.c src/arc.c src/fsops.c src/libc.c tests/fakedos.c
$(BUILD)/host/test_vfs: tests/test_vfs.c $(VFS_SRC) $(HDRS) | $(BUILD)
	$(HOSTCC) $(HOST_CFLAGS) -o $@ tests/test_vfs.c $(VFS_SRC)

DISK_SRC = src/disk.c $(VFS_SRC)
$(BUILD)/host/test_disk: tests/test_disk.c $(DISK_SRC) $(HDRS) | $(BUILD)
	$(HOSTCC) $(HOST_CFLAGS) -o $@ tests/test_disk.c $(DISK_SRC)

$(BUILD)/host/data/.stamp: tests/gen_lzh.py tests/gen_vfs.py tools/lha.py tools/arcpack.py \
                           tools/fat12.py $(BUILD)/host/icetool
	$(PYTHON) tests/gen_lzh.py $(BUILD)/host/data
	$(PYTHON) tests/gen_vfs.py $(BUILD)/host/data
	for f in $(BUILD)/host/data/lzh_*.bin; do \
	    b=$$(basename $$f .bin); b=$${b#lzh_}; \
	    [ -s $$f ] && $(BUILD)/host/icetool p $$f $(BUILD)/host/data/ice_$$b.ice; \
	done; touch $@

test: $(BUILD)/host/test_fsops $(BUILD)/host/test_prefs $(BUILD)/host/test_view \
      $(BUILD)/host/test_edit $(BUILD)/host/test_music $(BUILD)/host/test_vfs $(BUILD)/host/test_disk \
      $(BUILD)/host/data/.stamp $(BUILD)/TOSFC.PRG
	$(BUILD)/host/test_fsops
	$(BUILD)/host/test_prefs
	$(BUILD)/host/test_view
	$(BUILD)/host/test_edit
	$(BUILD)/host/test_music $(BUILD)/host/data
	$(BUILD)/host/test_vfs $(BUILD)/host/data
	$(BUILD)/host/test_disk $(BUILD)/host/data
	$(PYTHON) tests/test_lha.py
	$(PYTHON) tests/test_fat12.py
	$(PYTHON) tests/test_elf2prg.py $(BUILD)

bench: disk
	$(PYTHON) bench/run_all.py

clean:
	rm -f $(BUILD)/*.o $(BUILD)/*.su $(BUILD)/tosfc.* $(BUILD)/TOSFC.PRG $(BUILD)/host/*

.PHONY: all disk test bench clean
