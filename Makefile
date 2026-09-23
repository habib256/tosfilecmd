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

VERSION = 0.1.0

CROSS   ?= m68k-elf-
CC      = $(CROSS)gcc
NM      = $(CROSS)nm
HOSTCC  ?= cc
PYTHON  ?= python3

BUILD = build
DIST  = dist

CFLAGS = -mcpu=68000 -Os -fomit-frame-pointer -ffreestanding \
         -fno-tree-loop-distribute-patterns -fno-common \
         -Wall -Wextra -Wno-unused-parameter -std=gnu99 -fcallgraph-info=su
LDFLAGS = -mcpu=68000 -nostdlib -T src/tosfc.ld -Wl,--emit-relocs \
          -Wl,-Map,$(BUILD)/tosfc.map -Wl,--no-warn-rwx-segments

OBJS = crt0.o main.o screen.o input.o ui.o panel.o fsops.o prefs.o sys_tos.o libc.o
OBJS := $(addprefix $(BUILD)/,$(OBJS))
HDRS = $(wildcard src/*.h)

all: $(BUILD)/TOSFC.PRG

$(BUILD):
	mkdir -p $(BUILD) $(BUILD)/host

$(BUILD)/%.o: src/%.c $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/crt0.o: src/crt0.S | $(BUILD)
	$(CC) -mcpu=68000 -c $< -o $@

$(BUILD)/tosfc.elf: $(OBJS) src/tosfc.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

$(BUILD)/TOSFC.PRG: $(BUILD)/tosfc.elf tools/elf2prg.py tools/check_budget.py
	$(PYTHON) tools/elf2prg.py $< $@
	$(NM) -n $< > $(BUILD)/tosfc.sym
	$(PYTHON) tools/check_budget.py $@ $(BUILD)

disk: $(BUILD)/TOSFC.PRG tools/mkdisk.py tools/fat12.py
	$(PYTHON) tools/mkdisk.py $(VERSION) $(BUILD)/TOSFC.PRG $(DIST)

HOST_CFLAGS = -std=c99 -Wall -Wextra -g -DTOSFC_HOST -fsanitize=address,undefined

$(BUILD)/host/test_fsops: tests/test_fsops.c tests/fakedos.c tests/fakedos.h \
                          src/fsops.c src/libc.c $(HDRS) | $(BUILD)
	$(HOSTCC) $(HOST_CFLAGS) -o $@ tests/test_fsops.c tests/fakedos.c src/fsops.c src/libc.c

$(BUILD)/host/test_prefs: tests/test_prefs.c tests/fakedos.c tests/fakedos.h \
                          src/prefs.c src/fsops.c src/libc.c $(HDRS) | $(BUILD)
	$(HOSTCC) $(HOST_CFLAGS) -o $@ tests/test_prefs.c tests/fakedos.c src/prefs.c \
	    src/fsops.c src/libc.c

test: $(BUILD)/host/test_fsops $(BUILD)/host/test_prefs $(BUILD)/TOSFC.PRG
	$(BUILD)/host/test_fsops
	$(BUILD)/host/test_prefs
	$(PYTHON) tests/test_fat12.py
	$(PYTHON) tests/test_elf2prg.py $(BUILD)

bench: disk
	$(PYTHON) bench/run_all.py

clean:
	rm -f $(BUILD)/*.o $(BUILD)/*.su $(BUILD)/tosfc.* $(BUILD)/TOSFC.PRG $(BUILD)/host/*

.PHONY: all disk test bench clean
