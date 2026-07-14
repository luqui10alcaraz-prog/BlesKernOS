# =============================================================================
# BleskernOS Makefile
# =============================================================================

# Herramientas
NASM    := nasm
CC      := gcc
LD      := ld
AR      := ar
PYTHON  ?= python3
QEMU    ?= qemu-system-i386
WIN32_CC ?= i686-w64-mingw32-gcc
WIN32_WINDRES ?= i686-w64-mingw32-windres

# Flags
NASM_FLAGS := -f bin
CC_FLAGS   := -m32 -ffreestanding -fno-builtin -nostdlib -nostdinc -Os -Wall -MMD -MP -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables
LD_FLAGS   := -m elf_i386 -T kernel/linker.ld
INCLUDE_FLAGS := -I. -Ikernel -Iprograms -Ilibs/tinygl -Ilibs/src
WIN32_CFLAGS := -m32 -Os -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -ffunction-sections -fdata-sections
WIN32_LDFLAGS := -nostdlib -Wl,--entry=_entry -Wl,--subsystem=windows -Wl,--image-base,0x00400000 -Wl,--enable-stdcall-fixup -Wl,--gc-sections -s

# Archivos
BOOT1_SRC  := boot/boot.asm
BOOT1_FAT32_SRC := boot/boot_fat32.asm
MBR_SRC := boot/mbr.asm
BOOT2_SRC  := boot/stage2.asm
KERNEL_ENTRY_SRC := kernel/entry.asm
LIBC_SOURCES := \
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
	kernel/recovery_console.c \
	kernel/memory.c \
	kernel/gdt.c \
	kernel/task.c \
	kernel/syscall.c \
	kernel/api.c \
	kernel/public_api.c \
	kernel/file_dialog.c \
	kernel/usermode.c \
	kernel/pic.c \
	kernel/idt.c \
	kernel/panic.c \
	kernel/elf_loader.c \
	kernel/about_dialog.c \
	kernel/sound_file.c \
	kernel/startup_sound.c \
	kernel/driver_loader.c \
	kernel/pe_loader.c \
	kernel/win32/win32.c \
	kernel/win32/process.c \
	kernel/win32/thread.c \
	kernel/win32/sync.c \
	kernel/win32/resources.c \
	kernel/win32/profile.c \
	kernel/win32/exception.c \
	kernel/win32/ntdll.c \
	kernel/win32/kernel32.c \
	kernel/win32/msvcrt.c \
	kernel/win32/gdi32.c \
	kernel/win32/user32.c \
	kernel/win32/comctl32.c \
	kernel/win32/comdlg32.c \
	kernel/win32/advapi32.c \
	kernel/win32/shell32.c \
	kernel/win32/riched20.c \
	kernel/bootsplash.c \
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
	system/desktop/deskmanager.c \
	system/desktop/deskbar.c \
	system/services/screensaverd.c \
	programs/launcher.c \
	kernel/drivers/block.c \
	kernel/drivers/usb_storage.c \
	kernel/drivers/usb_uhci.c \
	kernel/drivers/vfs.c \
	kernel/drivers/pci.c \
	kernel/drivers/gfx.c \
	kernel/drivers/vga.c \
	kernel/drivers/vesa_core.c \
	kernel/drivers/mouse_core.c \
	kernel/drivers/ata.c \
	kernel/drivers/pit.c \
	kernel/drivers/sound_core.c \
	kernel/drivers/rtc_core.c \
	kernel/drivers/keyboard.c \
	kernel/drivers/iso9660_core.c \
	kernel/drivers/fat.c \
	$(DOOM_SOURCES)
KERNEL_ASM_SOURCES := kernel/isr_stubs.asm kernel/api_call.asm

BOOT1_BIN  := build/boot.bin
BOOT1_FAT32_BIN := build/boot_fat32.bin
MBR_BIN := build/mbr.bin
BOOT2_BIN  := build/stage2.bin
KERNEL_ENTRY_OBJ := build/entry.o
KERNEL_OBJS := $(patsubst %.c,build/%.o,$(KERNEL_SOURCES))
KERNEL_ASM_OBJS := build/isr_stubs.o build/api_call.o
KERNEL_ELF := build/kernel.elf
KERNEL_BIN := build/kernel.bin
LIBC_OBJS := $(patsubst %.c,build/%.o,$(LIBC_SOURCES))
TINYGL_OBJS := $(patsubst %.c,build/%.o,$(TINYGL_SOURCES))
LIBC_A := build/system/libs/libc/libc.a
TINYGL_A := build/system/libs/tinygl/tinygl.a
SDK_CFLAGS := -m32 -ffreestanding -fno-builtin -nostdlib -nostdinc -Os -Wall -MMD -MP -fno-pic -fno-pie -fno-stack-protector -Isdk/include
SDK_SYSCALL_OBJ := build/sdk/syscalls.o
SDK_A := build/sdk/libblesk.a

# Conservado sólo para que reglas antiguas puedan limpiarlo; `all` ya no lo
# construye ni inicializa el controlador de disquete.
DISK_IMG   := build/bleskernos.img
ATA_IMG    := build/bleskernos-ata.img
USB_IMG    := build/bleskernos-usb.img
# Debe coincidir con el limite que stage2 carga desde LBA 9 y con el espacio
# reservado al inicio de la imagen FAT32 para el kernel crudo.
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
# Estructura de la imagen principal FAT32:
#   /SYSTEM/PROGRAMS/*.O     ← aplicaciones nativas
#   /SYSTEM/CORE/*.O         ← componentes centrales del escritorio
#   /SYSTEM/WIN32/*.EXE      ← aplicaciones Win32

.PHONY: all clean run run-ac97 run-usb run-uhci build doom-extra win32-tests sdk

all: $(ATA_IMG) $(USB_IMG)
	@echo ""
	@echo "============================================"
	@echo "  BleskernOS compilado exitosamente!"
	@echo "  ATA FAT32: $(ATA_IMG)"
	@echo "  USB HDD: $(USB_IMG)"
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

$(BOOT1_FAT32_BIN): $(BOOT1_FAT32_SRC) | build
	@echo "[NASM] Ensamblando Stage 1 FAT32 ATA..."
	$(NASM) $(NASM_FLAGS) $< -o $@
	@SIZE=$$(wc -c < $@); \
	if [ $$SIZE -ne 512 ]; then \
		echo "[ERROR] Stage 1 FAT32 debe ser 512 bytes, es $$SIZE bytes!"; \
		exit 1; \
	fi
	@echo "[OK]   Stage 1 FAT32: $$(wc -c < $@) bytes"

$(MBR_BIN): $(MBR_SRC) | build
	@echo "[NASM] Ensamblando MBR USB-HDD..."
	$(NASM) $(NASM_FLAGS) $< -o $@
	@SIZE=$$(wc -c < $@); \
	if [ $$SIZE -ne 512 ]; then \
		echo "[ERROR] MBR debe ser 512 bytes, es $$SIZE bytes!"; \
		exit 1; \
	fi
	@echo "[OK]   MBR: $$(wc -c < $@) bytes"

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
build/%.o: kernel/%.asm | build
	@echo "[NASM] Ensamblando ISR/IRQ stubs..."
	$(NASM) -f elf32 $< -o $@
	@echo "[OK]   Kernel stubs: $@"

# Kernel: compilar C a objeto
build/%.o: %.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando $<..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

# Kernel: linkear
$(LIBC_A): $(LIBC_OBJS) | build
	@mkdir -p $(dir $@)
	@echo "[AR]   Creando libc externa..."
	$(AR) rcs $@ $^

$(TINYGL_A): $(TINYGL_OBJS) | build
	@mkdir -p $(dir $@)
	@echo "[AR]   Creando TinyGL externa..."
	$(AR) rcs $@ $^

$(SDK_SYSCALL_OBJ): sdk/lib/syscalls.c sdk/include/bleskernos.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando ABI de user-space..."
	$(CC) $(SDK_CFLAGS) -c $< -o $@

$(SDK_A): $(SDK_SYSCALL_OBJ) | build
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

sdk: $(SDK_A)
	@echo "[OK]   SDK nativo: $(SDK_A)"

$(KERNEL_ELF): $(KERNEL_ENTRY_OBJ) $(KERNEL_ASM_OBJS) $(KERNEL_OBJS) $(LIBC_A) kernel/linker.ld | build
	@echo "[LD]   Linkeando kernel..."
	$(LD) $(LD_FLAGS) $(KERNEL_ENTRY_OBJ) $(KERNEL_ASM_OBJS) $(KERNEL_OBJS) $(LIBC_A) -o $@

# Kernel: extraer binario plano del ELF
$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "[OBJCOPY] Extrayendo binario..."
	objcopy -O binary $< $@
	@echo "[OK]   Kernel: $$(wc -c < $@) bytes"

# ── Programas externos de /SYSTEM/PROGRAMS ─────────────────────

# shell.o — compilado como objeto independiente para el disquete
SHELL_OBJ := build/programs/shelllauncher.o

$(SHELL_OBJ): programs/shelllauncher.c | build
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
ABOUT_OBJ := build/programs/about.o
RUNBOX_OBJ := build/programs/runbox.o
TEXTEDITOR_OBJ := build/programs/texteditor.o
CALCULATOR_OBJ := build/programs/calculator.o
MIDAMP_OBJ := build/programs/midamp.o
IMAGEVIEWER_OBJ := build/programs/imageviewer.o
GAMES_OBJ := build/programs/games.o
PAINT_OBJ := build/programs/paint.o
APITEST_OBJ := build/programs/apitest.o
SCANDISK_OBJ := build/programs/scandisk.o
WINE_OBJ := build/programs/wine.o
PHASE2_RAW_OBJ := build/tests/userland/phase2.raw.o
PHASE2_OBJ := build/programs/phase2.o
RING3PROXY_OBJ := build/programs/ring3proxy.o
COMMAND_NAMES := help about uname hostname uptime date time shutdown reboot sleep \
	fdisk format mount unmount label checkdisk fsinfo backup dir ls copy move delete mkdir \
	rmdir rename touch tree find attrib chmod type more cat diff ps kill tasklist \
	taskkill top nice pci usb lspci lsusb cpuinfo mem soundtest ipconfig ping \
	netstat ftp wget curl compile link objdump nm hexdump strings calc \
	hexedit compress extract checksum benchmark start
COMMAND_COMMON_OBJ := build/system/commands/common.o
COMMAND_RAW_OBJS := $(addprefix build/system/commands/raw/,$(addsuffix .o,$(COMMAND_NAMES)))
COMMAND_OBJS := $(addprefix build/system/commands/,$(addsuffix .o,$(COMMAND_NAMES)))
WIN32_HELLO_EXE := build/win32/HELLO.EXE
WIN32_NOTEPAD_EXE := build/win32/NOTEPAD.EXE
WIN32_MSGBOX_EXE := build/win32/MSGBOX.EXE
WIN32_DYNLOAD_EXE := build/win32/DYNLOAD.EXE
WIN32_DLLTEST_EXE := build/win32/DLLTEST.EXE
WIN32_TEST_DLL := build/win32/TESTDLL.DLL
WIN32_TLSTEST_EXE := build/win32/TLSTEST.EXE
WIN32_THREADTEST_EXE := build/win32/THREADTEST.EXE
WIN32_SYNCTEST_EXE := build/win32/SYNCTEST.EXE
WIN32_RESOURCETEST_EXE := build/win32/RESOURCETEST.EXE
WIN32_MENUTEST_EXE := build/win32/MENUTEST.EXE
WIN32_DIALOGTEST_EXE := build/win32/DIALOGTEST.EXE
WIN32_SEHTEST_EXE := build/win32/SEHTEST.EXE
WIN32_WINECALC_COMPAT_EXE := build/win32/WINECALC_COMPAT.EXE
WIN32_EDITTEST_EXE := build/win32/EDITTEST.EXE
WIN32_WINECALC_COMPAT_RES := build/win32/winecalc_compat.res.o
WIN32_RESOURCE_ASSETS := build/win32/resource-assets.stamp
WIN32_RESOURCETEST_RES := build/win32/resourcetest.res.o
WIN32_MENUTEST_RES := build/win32/menutest.res.o
WIN32_DIALOGTEST_RES := build/win32/dialogtest.res.o
CONTROL_PANEL_OBJ := build/system/control/control_panel.o
APPEAR_CPL_RAW_OBJ := build/system/control/appearance.raw.o
APPEAR_CPL_OBJ := build/system/control/appearance.o
DISPLAY_CPL_RAW_OBJ := build/system/control/display.raw.o
DISPLAY_CPL_OBJ := build/system/control/display.o
SOUND_CPL_OBJ := build/system/control/sound.o
DATETIME_CPL_OBJ := build/system/control/datetime.o
MOUSE_CPL_OBJ := build/system/control/mouse.o
KEYBOARD_CPL_OBJ := build/system/control/keyboard.o
SYSTEM_CPL_OBJ := build/system/control/system.o
DEVMGR_CPL_OBJ := build/system/control/device_manager.o
CONTROL_OBJS := \
	$(CONTROL_PANEL_OBJ) \
	$(APPEAR_CPL_OBJ) \
	$(DISPLAY_CPL_OBJ) \
	$(SOUND_CPL_OBJ) \
	$(DATETIME_CPL_OBJ) \
	$(MOUSE_CPL_OBJ) \
	$(KEYBOARD_CPL_OBJ) \
	$(SYSTEM_CPL_OBJ) \
	$(DEVMGR_CPL_OBJ)
GEARS_OBJ := build/programs/gears.o
GEARS_RAW_OBJ := build/programs/gears.raw.o
SCREENSAVERD_OBJ := build/system/services/screensaverd.o
SB16_DVR := build/system/drivers/SB16.DVR
AC97_DVR := build/system/drivers/AC97.DVR
MAESTRO3_DVR := build/system/drivers/MAESTRO3.DVR
RTC_DVR := build/system/drivers/CMOSRTC.DVR
ISO9660_DVR := build/system/drivers/ISO9660.DVR
PS2MOUSE_DVR := build/system/drivers/PS2MOUSE.DVR
VESA_DVR := build/system/drivers/VESA.DVR
DRIVER_OBJS := $(AC97_DVR) $(MAESTRO3_DVR) $(SB16_DVR) $(RTC_DVR) $(ISO9660_DVR) \
	$(PS2MOUSE_DVR) $(VESA_DVR)
SSLOGO_OBJ := build/system/screensavers/ss_logo.o
SSPIPES_OBJ := build/system/screensavers/ss_pipes.o
SSPIPES_RAW_OBJ := build/system/screensavers/ss_pipes.raw.o
SSBALLS_OBJ := build/system/screensavers/ss_balls.o
SSBALLS_RAW_OBJ := build/system/screensavers/ss_balls.raw.o

EXTERNAL_APP_OBJS := \
	$(SHELL_OBJ) \
	$(FILEBROWSER_OBJ) \
	$(TEXTEDITOR_OBJ) \
	$(CALCULATOR_OBJ) \
	$(MIDAMP_OBJ) \
	$(PROCESSMANAGER_OBJ) \
	$(CALENDAR_OBJ) \
	$(ABOUT_OBJ) \
	$(RUNBOX_OBJ) \
	$(IMAGEVIEWER_OBJ) \
	$(GAMES_OBJ) \
	$(GEARS_OBJ) \
	$(PAINT_OBJ) \
	$(APITEST_OBJ) \
	$(SCANDISK_OBJ) \
	$(WINE_OBJ) \
	$(PHASE2_OBJ) \
	$(RING3PROXY_OBJ) \
	$(COMMAND_OBJS) \
	$(CONTROL_OBJS) \
	$(SCREENSAVERD_OBJ) \
	$(SSLOGO_OBJ) \
	$(SSPIPES_OBJ) \
	$(SSBALLS_OBJ)

$(PHASE2_RAW_OBJ): tests/userland/phase2.c sdk/include/bleskernos.h | build
	@mkdir -p $(dir $@)
	$(CC) $(SDK_CFLAGS) -c $< -o $@

$(PHASE2_OBJ): $(PHASE2_RAW_OBJ) $(SDK_A)
	@mkdir -p $(dir $@)
	$(LD) -m elf_i386 -r $< $(SDK_A) -o $@
	@echo "[OK]   Prueba Ring 3/API: $@"

$(RING3PROXY_OBJ): tests/userland/ring3proxy.c | build
	@mkdir -p $(dir $@)
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@
	@echo "[OK]   Prueba proxy API Ring 3: $@"

$(COMMAND_COMMON_OBJ): system/commands/common.c system/commands/common.h \
		sdk/include/bleskernos_api.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando biblioteca comun de comandos Ring 3..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(COMMAND_RAW_OBJS): build/system/commands/raw/%.o: system/commands/%.c \
		system/commands/common.h sdk/include/bleskernos_api.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando comando $*..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(COMMAND_OBJS): build/system/commands/%.o: build/system/commands/raw/%.o $(COMMAND_COMMON_OBJ)
	@mkdir -p $(dir $@)
	$(LD) -m elf_i386 -r $< $(COMMAND_COMMON_OBJ) -o $@
	@echo "[OK]   Comando independiente: $@"

$(SB16_DVR): kernel/drivers/sound.c kernel/include/driver.h kernel/include/sound.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando driver de sonido separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(AC97_DVR): kernel/drivers/ac97.c kernel/include/driver.h kernel/include/sound.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando driver AC97 ICH-compatible separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(MAESTRO3_DVR): kernel/drivers/maestro3.c kernel/drivers/maestro3_regs.h \
		kernel/drivers/maestro3_firmware.h kernel/include/driver.h \
		kernel/include/pci.h kernel/include/sound.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando driver externo ESS Maestro3..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(RTC_DVR): kernel/drivers/rtc.c kernel/include/driver.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando driver CMOS/RTC separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(ISO9660_DVR): kernel/drivers/iso9660.c kernel/include/driver.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando driver ISO9660 separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(PS2MOUSE_DVR): kernel/drivers/mouse.c kernel/include/driver.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando driver de mouse PS/2 separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(VESA_DVR): kernel/drivers/vesa.c kernel/include/driver.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando driver VESA separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(CALENDAR_OBJ): programs/calendar.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando calendar como programa separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@
	@echo "[OK]   Calendar object: $@"

$(APITEST_OBJ): programs/apitest.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando apitest como programa separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@
	@echo "[OK]   APITest object: $@"

$(SCANDISK_OBJ): programs/scandisk.c sdk/include/bleskernos_api.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando ScanDisk como programa Ring 3..."
	$(CC) $(SDK_CFLAGS) -c $< -o $@
	@echo "[OK]   ScanDisk object: $@"

$(WINE_OBJ): programs/wine.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando lanzador PE/Win32..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@
	@echo "[OK]   Wine/PE launcher object: $@"

$(WIN32_HELLO_EXE): tools/build_win32_hello.py | build
	@mkdir -p $(dir $@)
	@echo "[PEGEN] Construyendo HELLO.EXE de prueba..."
	$(PYTHON) $< $@

$(WIN32_NOTEPAD_EXE): tests/win32/notepad.c | build
	@mkdir -p $(dir $@)
	@command -v $(WIN32_CC) >/dev/null 2>&1 || { \
		echo "[ERROR] Falta $(WIN32_CC). Instalalo con: sudo apt install gcc-mingw-w64-i686"; \
		exit 1; \
	}
	@echo "[MINGW] Construyendo NOTEPAD.EXE..."
	$(WIN32_CC) $(WIN32_CFLAGS) $< $(WIN32_LDFLAGS) -luser32 -lkernel32 -lmsvcrt -lgcc -o $@
	@echo "[OK]   Win32 Notepad: $@"

$(WIN32_TLSTEST_EXE): tests/win32/tlstest.c | build
	@mkdir -p $(dir $@)
	@command -v $(WIN32_CC) >/dev/null 2>&1 || { \
		echo "[ERROR] Falta $(WIN32_CC). Instalalo con: sudo apt install gcc-mingw-w64-i686"; \
		exit 1; \
	}
	@echo "[MINGW] Construyendo TLSTEST.EXE..."
	$(WIN32_CC) $(WIN32_CFLAGS) $< $(WIN32_LDFLAGS) -Wl,--undefined=__tls_used -lkernel32 -lgcc -o $@
	@echo "[OK]   Win32 TLS test: $@"

$(WIN32_THREADTEST_EXE): tests/win32/threadtest.c | build
	@mkdir -p $(dir $@)
	@command -v $(WIN32_CC) >/dev/null 2>&1 || { \
		echo "[ERROR] Falta $(WIN32_CC). Instalalo con: sudo apt install gcc-mingw-w64-i686"; \
		exit 1; \
	}
	@echo "[MINGW] Construyendo THREADTEST.EXE..."
	$(WIN32_CC) $(WIN32_CFLAGS) $< $(WIN32_LDFLAGS) -Wl,--undefined=__tls_used -lkernel32 -lgcc -o $@
	@echo "[OK]   Win32 thread test: $@"

$(WIN32_SYNCTEST_EXE): tests/win32/synctest.c | build
	@mkdir -p $(dir $@)
	@command -v $(WIN32_CC) >/dev/null 2>&1 || { \
		echo "[ERROR] Falta $(WIN32_CC). Instalalo con: sudo apt install gcc-mingw-w64-i686"; \
		exit 1; \
	}
	@echo "[MINGW] Construyendo SYNCTEST.EXE..."
	$(WIN32_CC) $(WIN32_CFLAGS) $< $(WIN32_LDFLAGS) -lkernel32 -lgcc -o $@
	@echo "[OK]   Win32 synchronization test: $@"



$(WIN32_SEHTEST_EXE): tests/win32/sehtest.c | build
	@mkdir -p $(dir $@)
	@command -v $(WIN32_CC) >/dev/null 2>&1 || { \
		echo "[ERROR] Falta $(WIN32_CC). Instalalo con: sudo apt install gcc-mingw-w64-i686"; \
		exit 1; \
	}
	@echo "[MINGW] Construyendo SEHTEST.EXE..."
	$(WIN32_CC) $(WIN32_CFLAGS) $< $(WIN32_LDFLAGS) -lkernel32 -lgcc -o $@
	@echo "[OK]   Win32 SEH test: $@"

$(WIN32_RESOURCE_ASSETS): tools/build_win32_resource_assets.py | build
	@mkdir -p build/win32
	@echo "[PEGEN] Generando BMP/ICO para recursos Win32..."
	$(PYTHON) $< build/win32

$(WIN32_RESOURCETEST_RES): tests/win32/resourcetest.rc tests/win32/resource_ids.h $(WIN32_RESOURCE_ASSETS) | build
	@mkdir -p $(dir $@)
	@command -v $(WIN32_WINDRES) >/dev/null 2>&1 || { \
		echo "[ERROR] Falta $(WIN32_WINDRES). Instalalo con: sudo apt install binutils-mingw-w64-i686"; \
		exit 1; \
	}
	$(WIN32_WINDRES) -I tests/win32 -i $< -O coff -o $@

$(WIN32_MENUTEST_RES): tests/win32/menutest.rc tests/win32/resource_ids.h | build
	@mkdir -p $(dir $@)
	@command -v $(WIN32_WINDRES) >/dev/null 2>&1 || { \
		echo "[ERROR] Falta $(WIN32_WINDRES). Instalalo con: sudo apt install binutils-mingw-w64-i686"; \
		exit 1; \
	}
	$(WIN32_WINDRES) -I tests/win32 -i $< -O coff -o $@

$(WIN32_DIALOGTEST_RES): tests/win32/dialogtest.rc tests/win32/resource_ids.h | build
	@mkdir -p $(dir $@)
	@command -v $(WIN32_WINDRES) >/dev/null 2>&1 || { \
		echo "[ERROR] Falta $(WIN32_WINDRES). Instalalo con: sudo apt install binutils-mingw-w64-i686"; \
		exit 1; \
	}
	$(WIN32_WINDRES) -I tests/win32 -i $< -O coff -o $@

$(WIN32_RESOURCETEST_EXE): tests/win32/resourcetest.c tests/win32/resource_ids.h $(WIN32_RESOURCETEST_RES) | build
	@mkdir -p $(dir $@)
	@command -v $(WIN32_CC) >/dev/null 2>&1 || { \
		echo "[ERROR] Falta $(WIN32_CC). Instalalo con: sudo apt install gcc-mingw-w64-i686"; \
		exit 1; \
	}
	@echo "[MINGW] Construyendo RESOURCETEST.EXE..."
	$(WIN32_CC) $(WIN32_CFLAGS) tests/win32/resourcetest.c $(WIN32_RESOURCETEST_RES) $(WIN32_LDFLAGS) -luser32 -lkernel32 -lgcc -o $@

$(WIN32_MENUTEST_EXE): tests/win32/menutest.c tests/win32/resource_ids.h $(WIN32_MENUTEST_RES) | build
	@mkdir -p $(dir $@)
	@command -v $(WIN32_CC) >/dev/null 2>&1 || { \
		echo "[ERROR] Falta $(WIN32_CC). Instalalo con: sudo apt install gcc-mingw-w64-i686"; \
		exit 1; \
	}
	@echo "[MINGW] Construyendo MENUTEST.EXE..."
	$(WIN32_CC) $(WIN32_CFLAGS) tests/win32/menutest.c $(WIN32_MENUTEST_RES) $(WIN32_LDFLAGS) -luser32 -lkernel32 -lgcc -o $@

$(WIN32_DIALOGTEST_EXE): tests/win32/dialogtest.c tests/win32/resource_ids.h $(WIN32_DIALOGTEST_RES) | build
	@mkdir -p $(dir $@)
	@command -v $(WIN32_CC) >/dev/null 2>&1 || { \
		echo "[ERROR] Falta $(WIN32_CC). Instalalo con: sudo apt install gcc-mingw-w64-i686"; \
		exit 1; \
	}
	@echo "[MINGW] Construyendo DIALOGTEST.EXE..."
	$(WIN32_CC) $(WIN32_CFLAGS) tests/win32/dialogtest.c $(WIN32_DIALOGTEST_RES) $(WIN32_LDFLAGS) -luser32 -lkernel32 -lgcc -o $@


$(WIN32_WINECALC_COMPAT_RES): tests/win32/winecalc_compat.rc | build
	@mkdir -p $(dir $@)
	@command -v $(WIN32_WINDRES) >/dev/null 2>&1 || { echo "[ERROR] Falta $(WIN32_WINDRES)."; exit 1; }
	$(WIN32_WINDRES) -I tests/win32 -i $< -O coff -o $@

$(WIN32_WINECALC_COMPAT_EXE): tests/win32/winecalc_compat.c $(WIN32_WINECALC_COMPAT_RES) | build
	@mkdir -p $(dir $@)
	@command -v $(WIN32_CC) >/dev/null 2>&1 || { echo "[ERROR] Falta $(WIN32_CC)."; exit 1; }
	@echo "[MINGW] Construyendo WINECALC_COMPAT.EXE..."
	$(WIN32_CC) $(WIN32_CFLAGS) tests/win32/winecalc_compat.c $(WIN32_WINECALC_COMPAT_RES) $(WIN32_LDFLAGS) -luser32 -lgdi32 -lkernel32 -lgcc -o $@

$(WIN32_EDITTEST_EXE): tests/win32/edittest.c | build
	@mkdir -p $(dir $@)
	@command -v $(WIN32_CC) >/dev/null 2>&1 || { echo "[ERROR] Falta $(WIN32_CC)."; exit 1; }
	@echo "[MINGW] Construyendo EDITTEST.EXE..."
	$(WIN32_CC) $(WIN32_CFLAGS) tests/win32/edittest.c $(WIN32_LDFLAGS) -luser32 -lkernel32 -lgcc -o $@
	@echo "[OK]   Win32 EDIT test: $@"

win32-tests: $(WIN32_HELLO_EXE) $(WIN32_NOTEPAD_EXE) $(WIN32_MSGBOX_EXE) $(WIN32_DYNLOAD_EXE) $(WIN32_DLLTEST_EXE) $(WIN32_TEST_DLL) $(WIN32_TLSTEST_EXE) $(WIN32_THREADTEST_EXE) $(WIN32_SYNCTEST_EXE) $(WIN32_RESOURCETEST_EXE) $(WIN32_MENUTEST_EXE) $(WIN32_DIALOGTEST_EXE) $(WIN32_SEHTEST_EXE) $(WIN32_WINECALC_COMPAT_EXE) $(WIN32_EDITTEST_EXE)
	@echo "[OK]   Pruebas Win32 construidas."

$(WIN32_MSGBOX_EXE): tools/build_win32_msgbox.py | build
	@mkdir -p $(dir $@)
	@echo "[PEGEN] Construyendo MSGBOX.EXE de prueba..."
	$(PYTHON) $< $@

$(WIN32_DYNLOAD_EXE): tools/build_win32_dynload.py tools/build_win32_msgbox.py | build
	@mkdir -p $(dir $@)
	@echo "[PEGEN] Construyendo DYNLOAD.EXE de prueba..."
	$(PYTHON) $< $@

$(WIN32_TEST_DLL): tools/build_win32_testdll.py tools/build_win32_msgbox.py | build
	@mkdir -p $(dir $@)
	@echo "[PEGEN] Construyendo TESTDLL.DLL..."
	$(PYTHON) $< $@

$(WIN32_DLLTEST_EXE): tools/build_win32_dlltest.py tools/build_win32_dynload.py | build
	@mkdir -p $(dir $@)
	@echo "[PEGEN] Construyendo DLLTEST.EXE..."
	$(PYTHON) $< $@

$(APPEAR_CPL_RAW_OBJ): system/control/appearance.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando appearance..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(APPEAR_CPL_OBJ): $(APPEAR_CPL_RAW_OBJ) $(TINYGL_A)
	@echo "[LD]   Asociando appearance con TinyGL..."
	$(LD) -m elf_i386 -r $< $(TINYGL_A) -o $@
	@echo "[OK]   Appearance CPL object: $@"

$(DISPLAY_CPL_RAW_OBJ): system/control/display.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando display..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(DISPLAY_CPL_OBJ): $(DISPLAY_CPL_RAW_OBJ) $(TINYGL_A)
	@echo "[LD]   Asociando display con TinyGL..."
	$(LD) -m elf_i386 -r $< $(TINYGL_A) -o $@
	@echo "[OK]   Display CPL object: $@"

$(GEARS_RAW_OBJ): programs/gears.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando gears..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(GEARS_OBJ): $(GEARS_RAW_OBJ) $(TINYGL_A)
	@echo "[LD]   Asociando gears con TinyGL..."
	$(LD) -m elf_i386 -r $< $(TINYGL_A) -o $@
	@echo "[OK]   Gears object: $@"
$(SCREENSAVERD_OBJ): system/services/screensaverd.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando screensaverd como programa separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@
	@echo "[OK]   ScreenSaver daemon object: $@"

$(SSLOGO_OBJ): system/screensavers/ss_logo.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando ss_logo como programa separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@
	@echo "[OK]   SSLogo object: $@"

$(SSPIPES_RAW_OBJ): system/screensavers/ss_pipes.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando ss_pipes..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -DSS_PIPES_EXTERNAL_ENTRY -c $< -o $@

$(SSPIPES_OBJ): $(SSPIPES_RAW_OBJ) $(TINYGL_A)
	@echo "[LD]   Asociando ss_pipes con TinyGL..."
	$(LD) -m elf_i386 -r $< $(TINYGL_A) -o $@
	@echo "[OK]   SSPipes object: $@"

$(SSBALLS_RAW_OBJ): system/screensavers/ss_balls.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando ss_balls..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -DSS_BALLS_EXTERNAL_ENTRY -c $< -o $@

$(SSBALLS_OBJ): $(SSBALLS_RAW_OBJ) $(TINYGL_A)
	@echo "[LD]   Asociando ss_balls con TinyGL..."
	$(LD) -m elf_i386 -r $< $(TINYGL_A) -o $@
	@echo "[OK]   SSBalls object: $@"


# ── Imagen de disco ───────────────────────────────────────────────────────────
$(DISK_IMG): $(BOOT1_BIN) $(BOOT2_BIN) $(KERNEL_BIN) $(EXTERNAL_APP_OBJS) $(WIN32_HELLO_EXE) $(WIN32_NOTEPAD_EXE) $(WIN32_MSGBOX_EXE) $(WIN32_DYNLOAD_EXE) $(WIN32_DLLTEST_EXE) $(WIN32_TEST_DLL) $(WIN32_TLSTEST_EXE) $(WIN32_THREADTEST_EXE) $(WIN32_SYNCTEST_EXE) $(WIN32_RESOURCETEST_EXE) $(WIN32_MENUTEST_EXE) $(WIN32_DIALOGTEST_EXE) $(WIN32_SEHTEST_EXE) $(WIN32_WINECALC_COMPAT_EXE) $(WIN32_EDITTEST_EXE) $(TINYGL_A) $(LIBC_A) Desktop.INI Associations.INI tools/build_fat_floppy.py $(wildcard assets/icons/*.BMP) $(wildcard assets/wallpapers/*) assets/gif/abount.gif
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
	$(PYTHON) tools/build_fat_floppy.py $@ $(SHELL_OBJ) $(FILEBROWSER_OBJ) $(TEXTEDITOR_OBJ) $(CALCULATOR_OBJ) $(MIDAMP_OBJ) $(PROCESSMANAGER_OBJ) $(CALENDAR_OBJ) Desktop.INI assets/icons assets/gif/abount.gif Associations.INI $(FAT_RESERVED_SECTORS) $(SCREENSAVERD_OBJ) $(SSLOGO_OBJ) $(SSPIPES_OBJ) "" $(TINYGL_A) $(LIBC_A) $(CONTROL_OBJS) $(SSBALLS_OBJ)
	@echo "[OK]   Imagen: $$(wc -c < $@) bytes"

$(ATA_IMG): $(BOOT1_FAT32_BIN) $(BOOT2_BIN) $(KERNEL_BIN) $(EXTERNAL_APP_OBJS) $(DRIVER_OBJS) $(WIN32_HELLO_EXE) $(WIN32_NOTEPAD_EXE) $(WIN32_MSGBOX_EXE) $(WIN32_DYNLOAD_EXE) $(WIN32_DLLTEST_EXE) $(WIN32_TEST_DLL) $(WIN32_TLSTEST_EXE) $(WIN32_THREADTEST_EXE) $(WIN32_SYNCTEST_EXE) $(WIN32_RESOURCETEST_EXE) $(WIN32_MENUTEST_EXE) $(WIN32_DIALOGTEST_EXE) $(WIN32_SEHTEST_EXE) $(WIN32_WINECALC_COMPAT_EXE) $(WIN32_EDITTEST_EXE) $(TINYGL_A) $(LIBC_A) Desktop.INI Associations.INI tools/build_fat32_ata.py tools/build_fat_floppy.py tools/build_icons_pak.py $(wildcard assets/icons/*.BMP) $(wildcard assets/wallpapers/*) assets/gif/abount.gif assets/sounds/startup.wav
	@echo "[ATA]  Creando imagen FAT32 ATA..."
	$(PYTHON) tools/build_fat32_ata.py $@ $(SHELL_OBJ) $(FILEBROWSER_OBJ) $(TEXTEDITOR_OBJ) $(CALCULATOR_OBJ) $(MIDAMP_OBJ) $(PROCESSMANAGER_OBJ) $(CALENDAR_OBJ) Desktop.INI assets/icons assets/gif/abount.gif Associations.INI $(FAT_RESERVED_SECTORS) $(SCREENSAVERD_OBJ) $(SSLOGO_OBJ) $(SSPIPES_OBJ) "" $(BOOT1_FAT32_BIN) $(BOOT2_BIN) $(KERNEL_BIN) $(KERNEL_STAGE2_SECTORS) $(TINYGL_A) $(LIBC_A) $(CONTROL_OBJS) $(SSBALLS_OBJ)
	@echo "[OK]   ATA: $$(wc -c < $@) bytes"

$(USB_IMG): $(ATA_IMG) $(MBR_BIN) tools/build_usb_image.py
	@echo "[USB]  Creando imagen USB-HDD con MBR..."
	$(PYTHON) tools/build_usb_image.py $@ $(ATA_IMG) $(MBR_BIN)
	@echo "[OK]   USB: $$(wc -c < $@) bytes"

# Ejecutar en QEMU (requiere qemu-system-i386)
run: $(ATA_IMG) doom-extra
	@echo "[QEMU] Iniciando BleskernOS..."
	$(QEMU) \
		-drive file=$(ATA_IMG),format=raw,if=ide \
		$(QEMU_DOOM_ARGS) \
		$(QEMU_CDROM_ARGS) \
		$(QEMU_AUDIO_ARGS) \
		-boot c \
		-m 128M \
		-no-reboot \
		-no-shutdown

run-ac97: $(ATA_IMG)
	@echo "[QEMU] Iniciando prueba AC97 ICH-compatible..."
	$(QEMU) \
		-drive file=$(ATA_IMG),format=raw,if=ide \
		-device AC97 \
		-boot c \
		-m 128M \
		-serial stdio \
		-no-reboot \
		-no-shutdown

# Ejecutar con salida serial (útil para debugging)
run-usb: $(USB_IMG) doom-extra
	@echo "[QEMU] Iniciando BleskernOS desde imagen USB-HDD..."
	$(QEMU) \
		-device usb-ehci,id=ehci \
		-drive id=usbdisk,file=$(USB_IMG),format=raw,if=none \
		-device usb-storage,drive=usbdisk,bootindex=1 \
		$(QEMU_DOOM_ARGS) \
		$(QEMU_CDROM_ARGS) \
		$(QEMU_AUDIO_ARGS) \
		-boot c \
		-m 128M \
		-no-reboot \
		-no-shutdown

# Ejecutar el HCD UHCI contra un PIIX4 (8086:7112) emulado. El sistema
# arranca por ATA y la imagen USB queda como dispositivo secundario.
run-uhci: $(ATA_IMG) $(USB_IMG)
	@echo "[QEMU] Iniciando prueba PIIX4 UHCI..."
	$(QEMU) \
		-M pc,usb=off \
		-device piix4-usb-uhci,id=uhci \
		-drive file=$(ATA_IMG),format=raw,if=ide \
		-drive id=usbdisk,file=$(USB_IMG),format=raw,if=none \
		-device usb-storage,bus=uhci.0,drive=usbdisk \
		-boot c \
		-m 128M \
		-snapshot \
		-no-reboot \
		-no-shutdown

run-debug: $(ATA_IMG) doom-extra
	$(QEMU) \
		-drive file=$(ATA_IMG),format=raw,if=ide \
		$(QEMU_DOOM_ARGS) \
		$(QEMU_CDROM_ARGS) \
		$(QEMU_AUDIO_ARGS) \
		-boot c \
		-m 128M \
		-serial stdio \
		-no-reboot \

clean:
	@echo "[CLEAN] Limpiando build..."
	rm -rf build/
	@echo "[OK]"

# Rebuild C objects when one of their kernel/program headers changes.
ALL_C_DEPS := $(KERNEL_OBJS:.o=.d) $(LIBC_OBJS:.o=.d) $(TINYGL_OBJS:.o=.d) $(EXTERNAL_APP_OBJS:.o=.d) $(COMMAND_RAW_OBJS:.o=.d) $(COMMAND_COMMON_OBJ:.o=.d) $(APPEAR_CPL_RAW_OBJ:.o=.d) $(DISPLAY_CPL_RAW_OBJ:.o=.d) $(SDK_SYSCALL_OBJ:.o=.d) $(PHASE2_RAW_OBJ:.o=.d)
-include $(ALL_C_DEPS)


# --- BlesKernOS bootable ISO target ---

ISO_IMAGE := build/bleskernos.iso
ISO_ROOT  := build/iso_root
ISO_BOOT  := $(ISO_ROOT)/boot.img

.PHONY: iso run-iso clean-iso

# Target legado explícito: no forma parte de `all` porque aún usa FAT12.
iso: $(ISO_IMAGE)

$(ISO_IMAGE): build/bleskernos.img
	@echo "[ISO] Creando ISO booteable: $(ISO_IMAGE)"
	@rm -rf $(ISO_ROOT)
	@mkdir -p $(ISO_ROOT)
	@cp build/bleskernos.img $(ISO_BOOT)
	@if command -v xorriso >/dev/null 2>&1; then \
		xorriso -as mkisofs -quiet -V BLESKERNOS -o $(ISO_IMAGE) -b boot.img -c boot.catalog $(ISO_ROOT); \
	elif command -v genisoimage >/dev/null 2>&1; then \
		genisoimage -quiet -V BLESKERNOS -o $(ISO_IMAGE) -b boot.img -c boot.catalog $(ISO_ROOT); \
	elif command -v mkisofs >/dev/null 2>&1; then \
		mkisofs -quiet -V BLESKERNOS -o $(ISO_IMAGE) -b boot.img -c boot.catalog $(ISO_ROOT); \
	else \
		echo "ERROR: necesitás xorriso, genisoimage o mkisofs."; \
		echo "En Ubuntu/Debian: sudo apt install xorriso"; \
		exit 1; \
	fi
	@echo "[ISO] Lista: $(ISO_IMAGE)"

run-iso: $(ISO_IMAGE) build/bleskernos-ata.img
	qemu-system-i386 \
	  -drive file=$(ISO_IMAGE),media=cdrom,if=ide,readonly=on \
	  -drive file=build/bleskernos-ata.img,format=raw,if=ide \
	  -device sb16 \
	  -boot d \
	  -m 128M \
	  -serial stdio \
	  -no-reboot \
	  -no-shutdown

clean-iso:
	@rm -rf $(ISO_ROOT) $(ISO_IMAGE)
	@echo "[ISO] Limpio"
