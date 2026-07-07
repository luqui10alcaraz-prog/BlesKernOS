# =============================================================================
# BleskernOS Makefile
# =============================================================================

# Herramientas
NASM    := nasm
CC      := gcc
LD      := ld
PYTHON  ?= python3
QEMU    ?= qemu-system-i386

# Flags
NASM_FLAGS := -f bin
CC_FLAGS   := -m32 -ffreestanding -fno-builtin -nostdlib -nostdinc -Os -Wall -MMD -MP -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables
LD_FLAGS   := -m elf_i386 -T kernel/linker.ld
INCLUDE_FLAGS := -Ikernel -Ilibs/tinygl -Ilibs/src

# Archivos
BOOT1_SRC  := boot/boot.asm
BOOT2_SRC  := boot/stage2.asm
KERNEL_ENTRY_SRC := kernel/entry.asm
KERNEL_LIBC_SOURCES := \
	kernel/libc/string.c \
	kernel/libc/ctype.c \
	kernel/libc/stdlib.c \
	kernel/libc/math.c \
	kernel/libc/stdio.c \
	kernel/libc/gcc_runtime.c
DOOM_SOURCES := \
	programs/doomapp.c \
	programs/doom/doomgeneric/dummy.c \
	programs/doom/doomgeneric/am_map.c \
	programs/doom/doomgeneric/doomdef.c \
	programs/doom/doomgeneric/doomstat.c \
	programs/doom/doomgeneric/dstrings.c \
	programs/doom/doomgeneric/d_event.c \
	programs/doom/doomgeneric/d_items.c \
	programs/doom/doomgeneric/d_iwad.c \
	programs/doom/doomgeneric/d_loop.c \
	programs/doom/doomgeneric/d_main.c \
	programs/doom/doomgeneric/d_mode.c \
	programs/doom/doomgeneric/d_net.c \
	programs/doom/doomgeneric/f_finale.c \
	programs/doom/doomgeneric/f_wipe.c \
	programs/doom/doomgeneric/g_game.c \
	programs/doom/doomgeneric/hu_lib.c \
	programs/doom/doomgeneric/hu_stuff.c \
	programs/doom/doomgeneric/info.c \
	programs/doom/doomgeneric/i_cdmus.c \
	programs/doom/doomgeneric/i_endoom.c \
	programs/doom/doomgeneric/i_joystick.c \
	programs/doom/doomgeneric/i_scale.c \
	programs/doom/doomgeneric/i_sound.c \
	programs/doom/doomgeneric/i_system.c \
	programs/doom/doomgeneric/i_timer.c \
	programs/doom/doomgeneric/memio.c \
	programs/doom/doomgeneric/m_argv.c \
	programs/doom/doomgeneric/m_bbox.c \
	programs/doom/doomgeneric/m_cheat.c \
	programs/doom/doomgeneric/m_config.c \
	programs/doom/doomgeneric/m_controls.c \
	programs/doom/doomgeneric/m_fixed.c \
	programs/doom/doomgeneric/m_menu.c \
	programs/doom/doomgeneric/m_misc.c \
	programs/doom/doomgeneric/m_random.c \
	programs/doom/doomgeneric/p_ceilng.c \
	programs/doom/doomgeneric/p_doors.c \
	programs/doom/doomgeneric/p_enemy.c \
	programs/doom/doomgeneric/p_floor.c \
	programs/doom/doomgeneric/p_inter.c \
	programs/doom/doomgeneric/p_lights.c \
	programs/doom/doomgeneric/p_map.c \
	programs/doom/doomgeneric/p_maputl.c \
	programs/doom/doomgeneric/p_mobj.c \
	programs/doom/doomgeneric/p_plats.c \
	programs/doom/doomgeneric/p_pspr.c \
	programs/doom/doomgeneric/p_saveg.c \
	programs/doom/doomgeneric/p_setup.c \
	programs/doom/doomgeneric/p_sight.c \
	programs/doom/doomgeneric/p_spec.c \
	programs/doom/doomgeneric/p_switch.c \
	programs/doom/doomgeneric/p_telept.c \
	programs/doom/doomgeneric/p_tick.c \
	programs/doom/doomgeneric/p_user.c \
	programs/doom/doomgeneric/r_bsp.c \
	programs/doom/doomgeneric/r_data.c \
	programs/doom/doomgeneric/r_draw.c \
	programs/doom/doomgeneric/r_main.c \
	programs/doom/doomgeneric/r_plane.c \
	programs/doom/doomgeneric/r_segs.c \
	programs/doom/doomgeneric/r_sky.c \
	programs/doom/doomgeneric/r_things.c \
	programs/doom/doomgeneric/sha1.c \
	programs/doom/doomgeneric/sounds.c \
	programs/doom/doomgeneric/statdump.c \
	programs/doom/doomgeneric/st_lib.c \
	programs/doom/doomgeneric/st_stuff.c \
	programs/doom/doomgeneric/s_sound.c \
	programs/doom/doomgeneric/tables.c \
	programs/doom/doomgeneric/v_video.c \
	programs/doom/doomgeneric/wi_stuff.c \
	programs/doom/doomgeneric/w_checksum.c \
	programs/doom/doomgeneric/w_file.c \
	programs/doom/doomgeneric/w_main.c \
	programs/doom/doomgeneric/w_wad.c \
	programs/doom/doomgeneric/z_zone.c \
	programs/doom/doomgeneric/w_file_stdc.c \
	programs/doom/doomgeneric/i_input.c \
	programs/doom/doomgeneric/i_video.c \
	programs/doom/doomgeneric/doomgeneric.c \
	programs/doom/doomgeneric/doomgeneric_bleskernos.c
# DOOM se construye y empaqueta desde programs/doom/Makefile.
DOOM_SOURCES :=
TINYGL_SOURCES := \
	libs/src/bleskernos_compat.c \
	libs/src/api.c \
	libs/src/arrays.c \
	libs/src/clear.c \
	libs/src/clip.c \
	libs/src/get.c \
	libs/src/image_util.c \
	libs/src/init.c \
	libs/src/light.c \
	libs/src/list.c \
	libs/src/matrix.c \
	libs/src/memory.c \
	libs/src/misc.c \
	libs/src/msghandling.c \
	libs/src/select.c \
	libs/src/specbuf.c \
	libs/src/texture.c \
	libs/src/vertex.c \
	libs/src/zbuffer.c \
	libs/src/zline.c \
	libs/src/zmath.c \
	libs/src/zpostprocess.c \
	libs/src/zraster.c \
	libs/src/ztext.c \
	libs/src/ztriangle.c

KERNEL_SOURCES := \
	kernel/kernel.c \
	kernel/vga.c \
	kernel/memory.c \
	kernel/gdt.c \
	kernel/task.c \
	kernel/syscall.c \
	kernel/usermode.c \
	kernel/pic.c \
	kernel/idt.c \
	kernel/panic.c \
	kernel/elf_loader.c \
	gui/gfx.c \
	gui/font.c \
	gui/windows.c \
	gui/widget.c \
	gui/desktop.c \
	gui/compositor.c \
	gui/event.c \
	gui/gui.c \
	gui/image.c \
	programs/shell.c \
	programs/deskmanager.c \
	programs/deskbar.c \
	programs/runbox.c \
	programs/filebrowser.c \
	programs/shelllauncher.c \
	programs/texteditor.c \
	programs/calculator.c \
	programs/calendar.c \
	programs/screensaverd.c \
	programs/processmanager.c \
	programs/midamp.c \
	programs/imageviewer.c \
	programs/games.c \
	programs/gears.c \
	programs/paint.c \
	programs/settings.c \
	programs/about.c \
	programs/launcher.c \
	kernel/drivers/block.c \
	kernel/drivers/vfs.c \
	kernel/drivers/pci.c \
	kernel/drivers/gfx.c \
	kernel/drivers/vga.c \
	kernel/drivers/vesa.c \
	kernel/drivers/mouse.c \
	kernel/drivers/ata.c \
	kernel/drivers/floppy.c \
	kernel/drivers/pit.c \
	kernel/drivers/sound.c \
	kernel/drivers/rtc.c \
	kernel/drivers/keyboard.c \
	kernel/drivers/iso9660.c \
	kernel/drivers/fat.c \
	$(TINYGL_SOURCES) \
	$(KERNEL_LIBC_SOURCES) \
	$(DOOM_SOURCES)
KERNEL_ASM_SOURCES := kernel/isr_stubs.asm

BOOT1_BIN  := build/boot.bin
BOOT2_BIN  := build/stage2.bin
KERNEL_ENTRY_OBJ := build/entry.o
KERNEL_OBJS := $(patsubst %.c,build/%.o,$(KERNEL_SOURCES))
KERNEL_ASM_OBJS := build/isr_stubs.o
KERNEL_ELF := build/kernel.elf
KERNEL_BIN := build/kernel.bin

DISK_IMG   := build/bleskernos.img
ATA_IMG    := build/bleskernos-ata.img
FLOPPY_TOTAL_SECTORS := 2880
KERNEL_STAGE2_SECTORS := 1024
FAT_RESERVED_SECTORS := $(shell expr 9 + $(KERNEL_STAGE2_SECTORS))
QEMU_AUDIO_ARGS ?= -device sb16
DOOM_EXTRA_ISO ?= programs/doom/build/doom-extra.iso
QEMU_DOOM_ARGS := -drive file=$(DOOM_EXTRA_ISO),media=cdrom,if=ide,readonly=on

ifdef CDROM_IMG
QEMU_CDROM_ARGS := -drive file=$(CDROM_IMG),media=cdrom,if=ide,readonly=on
else
QEMU_CDROM_ARGS :=
endif

# Layout del floppy FAT12:
# Sector 0       = Stage 1 + BPB FAT12
# Sectores 1-4   = Stage 2
# Sectores 9..(8 + KERNEL_STAGE2_SECTORS) = Kernel crudo reservado para stage2
# Sectores siguientes                    = FAT12 + root dir + data
#
# Estructura de carpetas en el disquete (gestionada por build_fat_floppy.py):
#   /PROGRAMS/SHELL.O        ← shell ejecutable
#   /PROGRAMS/FILEBROWSER.O  ← navegador de archivos ejecutable

.PHONY: all clean run build doom-extra

all: $(DISK_IMG)
	@echo ""
	@echo "============================================"
	@echo "  BleskernOS compilado exitosamente!"
	@echo "  Imagen: $(DISK_IMG)"
	@echo "  Para emular: make run"
	@echo "============================================"

build:
	@mkdir -p build

doom-extra:
	$(MAKE) -C programs/doom

# Stage 1: Bootloader MBR (512 bytes exactos)
$(BOOT1_BIN): $(BOOT1_SRC) | build
	@echo "[NASM] Ensamblando Stage 1..."
	$(NASM) $(NASM_FLAGS) -DRESERVED_SECTORS=$(FAT_RESERVED_SECTORS) $< -o $@
	@SIZE=$$(wc -c < $@); \
	if [ $$SIZE -ne 512 ]; then \
		echo "[ERROR] Stage 1 debe ser 512 bytes, es $$SIZE bytes!"; \
		exit 1; \
	fi
	@echo "[OK]   Stage 1: $$(wc -c < $@) bytes"

# Stage 2: Second stage bootloader
$(BOOT2_BIN): $(BOOT2_SRC) | build
	@echo "[NASM] Ensamblando Stage 2..."
	$(NASM) $(NASM_FLAGS) -DKERNEL_SECTORS=$(KERNEL_STAGE2_SECTORS) $< -o $@
	@echo "[OK]   Stage 2: $$(wc -c < $@) bytes"

# Kernel entry stub: ensamblar
$(KERNEL_ENTRY_OBJ): $(KERNEL_ENTRY_SRC) | build
	@echo "[NASM] Ensamblando entry stub..."
	$(NASM) -f elf32 $< -o $@
	@echo "[OK]   Kernel entry: $@"

# Kernel assembly stubs
$(KERNEL_ASM_OBJS): $(KERNEL_ASM_SOURCES) | build
	@echo "[NASM] Ensamblando ISR/IRQ stubs..."
	$(NASM) -f elf32 $< -o $@
	@echo "[OK]   Kernel stubs: $@"

# Kernel: compilar C a objeto
build/%.o: %.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando $<..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

# Kernel: linkear
$(KERNEL_ELF): $(KERNEL_ENTRY_OBJ) $(KERNEL_ASM_OBJS) $(KERNEL_OBJS) kernel/linker.ld | build
	@echo "[LD]   Linkeando kernel..."
	$(LD) $(LD_FLAGS) $(KERNEL_ENTRY_OBJ) $(KERNEL_ASM_OBJS) $(KERNEL_OBJS) -o $@

# Kernel: extraer binario plano del ELF
$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "[OBJCOPY] Extrayendo binario..."
	objcopy -O binary $< $@
	@echo "[OK]   Kernel: $$(wc -c < $@) bytes"

# ── Programas que van en el disquete (carpeta /PROGRAMS) ─────────────────────

# shell.o — compilado como objeto independiente para el disquete
SHELL_OBJ := build/programs/shell.o

$(SHELL_OBJ): programs/shell.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando shell como programa separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@
	@echo "[OK]   Shell object: $@"

# filebrowser.o — compilado como objeto independiente para el disquete
FILEBROWSER_OBJ := build/programs/filebrowser.o

$(FILEBROWSER_OBJ): programs/filebrowser.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando filebrowser como programa separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@
	@echo "[OK]   Filebrowser object: $@"

# processmanager.o - compilado como objeto independiente para el disquete
PROCESSMANAGER_OBJ := build/programs/processmanager.o

$(PROCESSMANAGER_OBJ): programs/processmanager.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando processmanager como programa separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@
	@echo "[OK]   Processmanager object: $@"

# calendar.o - compilado como objeto independiente para el disquete
CALENDAR_OBJ := build/programs/calendar.o
GEARS_OBJ := build/programs/gears.o
SCREENSAVERD_OBJ := build/programs/screensaverd.o
SSLOGO_OBJ := build/programs/ss_logo.o
SSPIPES_OBJ := build/programs/ss_pipes.o

$(CALENDAR_OBJ): programs/calendar.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando calendar como programa separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@
	@echo "[OK]   Calendar object: $@"

$(GEARS_OBJ): programs/gears.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando gears como programa separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@
	@echo "[OK]   Gears object: $@"
$(SCREENSAVERD_OBJ): programs/screensaverd.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando screensaverd como programa separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@
	@echo "[OK]   ScreenSaver daemon object: $@"

$(SSLOGO_OBJ): programs/ss_logo.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando ss_logo como programa separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@
	@echo "[OK]   SSLogo object: $@"

$(SSPIPES_OBJ): programs/ss_pipes.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando ss_pipes como programa separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -DSS_PIPES_EXTERNAL_ENTRY -c $< -o $@
	@echo "[OK]   SSPipes object: $@"


# ── Imagen de disco ───────────────────────────────────────────────────────────
$(DISK_IMG): $(BOOT1_BIN) $(BOOT2_BIN) $(KERNEL_BIN) $(SHELL_OBJ) $(FILEBROWSER_OBJ) $(PROCESSMANAGER_OBJ) $(CALENDAR_OBJ) $(SCREENSAVERD_OBJ) $(SSLOGO_OBJ) $(SSPIPES_OBJ) build/programs/texteditor.o build/programs/calculator.o build/programs/midamp.o Desktop.INI Associations.INI tools/build_fat_floppy.py $(wildcard assets/icons/*.BMP) assets/gif/abount.gif
	@echo "[IMG]  Creando imagen de disco..."
	@KERNEL_BIN_SECTORS=$$(( ($$(wc -c < $(KERNEL_BIN)) + 511) / 512 )); \
	if [ $$KERNEL_BIN_SECTORS -gt $(KERNEL_STAGE2_SECTORS) ]; then \
		echo "[ERROR] Kernel ocupa $$KERNEL_BIN_SECTORS sectores, pero stage2 carga $(KERNEL_STAGE2_SECTORS)."; \
		exit 1; \
	fi; \
	dd if=/dev/zero of=$@ bs=512 count=$(FLOPPY_TOTAL_SECTORS) 2>/dev/null
	dd if=$(BOOT1_BIN) of=$@ bs=512 seek=0 conv=notrunc 2>/dev/null
	dd if=$(BOOT2_BIN) of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	dd if=$(KERNEL_BIN) of=$@ bs=512 seek=9 conv=notrunc 2>/dev/null
	$(PYTHON) tools/build_fat_floppy.py $@ $(SHELL_OBJ) $(FILEBROWSER_OBJ) build/programs/texteditor.o build/programs/calculator.o build/programs/midamp.o $(PROCESSMANAGER_OBJ) $(CALENDAR_OBJ) Desktop.INI assets/icons assets/gif/abount.gif Associations.INI $(FAT_RESERVED_SECTORS) $(SCREENSAVERD_OBJ) $(SSLOGO_OBJ) $(SSPIPES_OBJ)
	@echo "[OK]   Imagen: $$(wc -c < $@) bytes"

$(ATA_IMG): $(DISK_IMG)
	cp $(DISK_IMG) $(ATA_IMG)

# Ejecutar en QEMU (requiere qemu-system-i386)
run: $(DISK_IMG) $(ATA_IMG) doom-extra
	@echo "[QEMU] Iniciando BleskernOS..."
	$(QEMU) \
		-drive file=$(DISK_IMG),format=raw,if=floppy \
		-drive file=$(ATA_IMG),format=raw,if=ide \
		$(QEMU_DOOM_ARGS) \
		$(QEMU_CDROM_ARGS) \
		$(QEMU_AUDIO_ARGS) \
		-boot a \
		-m 128M \
		-no-reboot \
		-no-shutdown

# Ejecutar con salida serial (útil para debugging)
run-debug: $(DISK_IMG) $(ATA_IMG) doom-extra
	$(QEMU) \
		-drive file=$(DISK_IMG),format=raw,if=floppy \
		-drive file=$(ATA_IMG),format=raw,if=ide \
		$(QEMU_DOOM_ARGS) \
		$(QEMU_CDROM_ARGS) \
		$(QEMU_AUDIO_ARGS) \
		-boot a \
		-m 128M \
		-serial stdio \
		-no-reboot \

clean:
	@echo "[CLEAN] Limpiando build..."
	rm -rf build/
	@echo "[OK]"

# Rebuild C objects when one of their kernel/program headers changes.
-include $(KERNEL_OBJS:.o=.d)
