CC      ?= gcc
LD      ?= ld
XORRISO ?= xorriso

ROOT       := ../..
BUILD      := build
PROGRAM    := $(BUILD)/DOOM.O
ISO        := $(BUILD)/doom-extra.iso
ISO_ROOT   := $(BUILD)/iso-root
WAD        ?= DOOM1.WAD
CC_FLAGS   := -m32 -ffreestanding -fno-builtin -nostdlib -nostdinc -Os -Wall \
	-fno-pie -fno-pic -fno-asynchronous-unwind-tables
INCLUDES   := -I$(ROOT)/kernel -I$(ROOT)/programs -Idoomgeneric

FOREIGN_PORT_SOURCES := \
	doomgeneric/doomgeneric_allegro.c \
	doomgeneric/doomgeneric_emscripten.c \
	doomgeneric/doomgeneric_linuxvt.c \
	doomgeneric/doomgeneric_sdl.c \
	doomgeneric/doomgeneric_soso.c \
	doomgeneric/doomgeneric_sosox.c \
	doomgeneric/doomgeneric_win.c \
	doomgeneric/doomgeneric_xlib.c \
	doomgeneric/i_allegromusic.c \
	doomgeneric/i_allegrosound.c \
	doomgeneric/i_sdlmusic.c \
	doomgeneric/i_sdlsound.c \
	doomgeneric/gusconf.c \
	doomgeneric/icon.c \
	doomgeneric/mus2mid.c

ENGINE_SOURCES := $(filter-out $(FOREIGN_PORT_SOURCES),$(wildcard doomgeneric/*.c))
SOURCES        := doomapp.c $(ENGINE_SOURCES)
OBJECTS        := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES))

.PHONY: all clean check-sources

all: check-sources $(ISO)
	@echo "[OK] Programa: $(PROGRAM)"
	@echo "[OK] ISO extra: $(ISO)"

check-sources:
	@test -n "$(ENGINE_SOURCES)" || { \
		echo "[ERROR] Falta programs/doom/doomgeneric/."; \
		echo "Restaura las fuentes de DoomGeneric para construir DOOM.O."; \
		exit 1; \
	}
	@test -f "$(WAD)" || { echo "[ERROR] No existe $(WAD)."; exit 1; }

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CC_FLAGS) $(INCLUDES) -c $< -o $@

# Doom feature flags live in doomfeatures.h; without this dependency,
# stale objects can survive when toggling sound support.
$(BUILD)/doomgeneric/%.o: Makefile doomgeneric/%.c doomgeneric/doomfeatures.h

$(PROGRAM): $(OBJECTS)
	$(LD) -m elf_i386 -r $(OBJECTS) -o $@

$(ISO): $(PROGRAM) $(WAD)
	@rm -rf $(ISO_ROOT)
	@mkdir -p $(ISO_ROOT)/DOOM
	@cp $(PROGRAM) $(ISO_ROOT)/DOOM/DOOM.O
	@cp $(WAD) $(ISO_ROOT)/DOOM/$(notdir $(WAD))
	$(XORRISO) -as mkisofs -quiet -V "BLESKERN DOOM" \
		-iso-level 3 -o $@ $(ISO_ROOT)

clean:
	rm -rf $(BUILD)
