# =============================================================================
# BleskernOS Makefile
# =============================================================================

EDITION ?= user
VALID_EDITIONS := user developer
ifeq ($(filter $(EDITION),$(VALID_EDITIONS)),)
$(error EDITION debe ser user o developer)
endif

# Herramientas
NASM    := nasm
CC      := gcc
LD      := ld
AR      := ar
PYTHON  ?= python3
QEMU    ?= qemu-system-i386
QEMU_IMG ?= qemu-img
WIN32_CC ?= i686-w64-mingw32-gcc
WIN32_WINDRES ?= i686-w64-mingw32-windres

# Flags
NASM_FLAGS := -f bin
CC_FLAGS   := -march=i486 -mtune=pentium -m32 -mfpmath=387 -mno-sse -mno-sse2 -ffreestanding -fno-builtin -nostdlib -nostdinc -Os -Wall -MMD -MP -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
LD_FLAGS   := -m elf_i386 -T kernel/linker.ld
INCLUDE_FLAGS := -I. -Ikernel -Iprograms -Ilibs/tinygl -Ilibs/src
WIN32_CFLAGS := -m32 -Os -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -ffunction-sections -fdata-sections
WIN32_LDFLAGS := -nostdlib -Wl,--entry=_entry -Wl,--subsystem=windows -Wl,--image-base,0x00400000 -Wl,--enable-stdcall-fixup -Wl,--gc-sections -s

# Archivos
BOOT1_SRC  := boot/boot.asm
BOOT1_CD_SRC := boot/boot.asm
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
	kernel/libc/gcc_runtime.c \
	kernel/libc/inflate.c
TINYGL_SOURCES := \
	libs/src/bleskernos_compat.c \
	libs/src/bleskernos_gpu.c \
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

# Mesa 3.5 freestanding con backend híbrido OSMesa/GFX3D.  El core y TNL
# conservan el fallback SWRAST; las primitivas compatibles se envían al driver
# 3D activo. X11/GLX, SVGAlib, Glide y threads del host quedan desactivados.
MESA35_ROOT := libs/Mesa-3.5
MESA35_SOURCES := \
	$(filter-out $(MESA35_ROOT)/src/config.c,$(wildcard $(MESA35_ROOT)/src/*.c)) \
	$(filter-out $(MESA35_ROOT)/src/math/m_debug_%.c,$(wildcard $(MESA35_ROOT)/src/math/*.c)) \
	$(wildcard $(MESA35_ROOT)/src/swrast/*.c) \
	$(wildcard $(MESA35_ROOT)/src/swrast_setup/*.c) \
	$(wildcard $(MESA35_ROOT)/src/tnl/*.c) \
	$(wildcard $(MESA35_ROOT)/src/array_cache/*.c) \
	$(wildcard $(MESA35_ROOT)/src/BlesKernOS/*.c) \
	$(MESA35_ROOT)/src/OSmesa/osmesa.c \
	$(MESA35_ROOT)/bleskernos/config_stub.c \
	$(MESA35_ROOT)/bleskernos/runtime.c

KERNEL_SOURCES := \
	kernel/kernel.c \
	kernel/installer.c \
	kernel/floppy_installer.c \
	kernel/bkl_setup.c \
	kernel/setup_boot.c \
	kernel/vga.c \
	kernel/recovery_console.c \
	kernel/paging.c \
	kernel/usercopy.c \
	kernel/memory.c \
	kernel/compat_mode.c \
	kernel/gdt.c \
	kernel/task.c \
	kernel/scheduler_smp.c \
	kernel/smp.c \
	kernel/smp_watchdog.c \
	kernel/klock.c \
	kernel/kernel_domains.c \
	kernel/perfmon.c \
	kernel/syscall.c \
	kernel/api.c \
	kernel/graphics_resources.c \
	kernel/language.c \
	kernel/public_api.c \
	kernel/clipboard.c \
	kernel/file_dialog.c \
	kernel/usermode.c \
	kernel/pic.c \
	kernel/idt.c \
	kernel/panic.c \
	kernel/elf_loader.c \
	kernel/bex_loader.c \
	kernel/about_dialog.c \
	kernel/sound_file.c \
	kernel/startup_sound.c \
	kernel/driver_loader.c \
	kernel/network.c \
	kernel/bootsplash.c \
	gui/gfx.c \
	gui/font.c \
	gui/windows.c \
	gui/widget.c \
	gui/desktop.c \
	gui/compositor.c \
	gui/gpu_compositor.c \
	gui/event.c \
	gui/gui.c \
	gui/image.c \
	gui/svg.c \
	gui/jpeg.c \
	programs/shell.c \
	system/desktop/deskmanager.c \
	system/desktop/deskbar.c \
	system/services/screensaverd.c \
	programs/launcher.c \
	kernel/drivers/core/block.c \
	kernel/drivers/platform/lpt.c \
	kernel/drivers/storage/floppy.c \
	kernel/drivers/storage/usb_storage.c \
	kernel/drivers/usb/usb_uhci.c \
	kernel/drivers/core/vfs.c \
	kernel/drivers/core/pci.c \
	kernel/drivers/core/gfx.c \
	kernel/drivers/core/gfx3d.c \
	kernel/drivers/core/rage128_engine.c \
	kernel/drivers/graphics/svga_transport.c \
	kernel/drivers/graphics/vga.c \
	kernel/drivers/graphics/vesa_core.c \
	kernel/drivers/input/mouse_core.c \
	kernel/drivers/storage/ata.c \
	kernel/drivers/core/pit.c \
	kernel/drivers/audio/sound_core.c \
	kernel/drivers/platform/rtc_core.c \
	kernel/drivers/input/keyboard.c \
	kernel/drivers/storage/iso9660_core.c \
	kernel/drivers/storage/iso9660.c \
	kernel/drivers/storage/fat.c \
	kernel/drivers/storage/bfs.c
WINE_SOURCES := kernel/ne_loader.c kernel/pe_loader.c \
	$(sort $(wildcard kernel/win32/*.c))
WINE_OBJS := $(patsubst %.c,build/%.o,$(WINE_SOURCES))
WINE_A := build/system/libs/wine/WINE.A

KERNEL_ASM_SOURCES := kernel/isr_stubs.asm kernel/api_call.asm

BOOT1_BIN  := build/boot.bin
BOOT1_CD_BIN := build/boot_cd.bin
BOOT1_FAT32_BIN := build/boot_fat32.bin
MBR_BIN := build/mbr.bin
BOOT2_BIN  := build/stage2.bin
KERNEL_ENTRY_OBJ := build/entry.o
KERNEL_OBJS := $(patsubst %.c,build/%.o,$(KERNEL_SOURCES))
KERNEL_ASM_OBJS := build/isr_stubs.o build/api_call.o
KERNEL_GAS_OBJS := build/kernel/ap_trampoline.o
KERNEL_ELF := build/kernel.elf
KERNEL_BIN := build/kernel.bin
LIBC_OBJS := $(patsubst %.c,build/%.o,$(LIBC_SOURCES))
TINYGL_OBJS := $(patsubst %.c,build/%.o,$(TINYGL_SOURCES))
MESA35_OBJS := $(patsubst %.c,build/%.o,$(MESA35_SOURCES))
LIBC_A := build/system/libs/libc/libc.a
TINYGL_A := build/system/libs/tinygl/tinygl.a
MESA35_A := build/system/libs/mesa35/mesa35.a
SDK_CFLAGS := -m32 -mfpmath=387 -mno-sse -mno-sse2 -ffreestanding -fno-builtin -nostdlib -nostdinc -Os -Wall -MMD -MP -fno-pic -fno-pie -fno-stack-protector -Isdk/include
MESA35_CFLAGS := $(CC_FLAGS) \
	-I$(MESA35_ROOT)/bleskernos/include \
	-I$(MESA35_ROOT) -I$(MESA35_ROOT)/include -I$(MESA35_ROOT)/src \
	-I$(MESA35_ROOT)/src/math -I$(MESA35_ROOT)/src/swrast \
	-I$(MESA35_ROOT)/src/swrast_setup -I$(MESA35_ROOT)/src/tnl \
	-I$(MESA35_ROOT)/src/array_cache -I$(MESA35_ROOT)/src/BlesKernOS \
	-DHAVE_CONFIG_H -DMESA_BLESKERNOS=1 -U__linux__ -Ulinux
SDK_SYSCALL_OBJ := build/sdk/syscalls.o
SDK_THREAD_OBJ := build/sdk/thread.o
SDK_PRINT_OBJ := build/sdk/print.o
SDK_TINYGL_OBJ := build/sdk/tinygl.o
SDK_MESA_OBJ := build/sdk/mesa.o
SDK_A := build/sdk/libblesk.a
SDK_TINYGL_A := build/sdk/libblesk_tinygl.a
SDK_MESA_A := build/sdk/libblesk_mesa.a
BEARSSL_SOURCES := $(wildcard libs/bearssl/src/*.c libs/bearssl/src/*/*.c)
BEARSSL_OBJS := $(patsubst %.c,build/%.o,$(BEARSSL_SOURCES))
BEARSSL_A := build/system/libs/bearssl/libbearssl.a

# Conservado sólo para que reglas antiguas puedan limpiarlo; `all` ya no lo
# construye ni inicializa el controlador de disquete.
DISK_IMG   := build/bleskernos-$(EDITION).img
ATA_IMG    := build/bleskernos-ata-$(EDITION).img
USB_IMG    := build/bleskernos-usb-$(EDITION).img
VMWARE_IMG := build/bleskernos-vmware-$(EDITION).vmdk
# Reserva máxima para el kernel crudo desde LBA 9 y para la FAT32.
# reservado al inicio de la imagen FAT32 para el kernel crudo.
# Reserva máxima de 1 MiB; Stage 2 carga solamente los sectores reales.
KERNEL_STAGE2_SECTORS := 2048
KERNEL_LOAD_SECTORS = 1280
FAT_RESERVED_SECTORS := $(shell expr 9 + $(KERNEL_STAGE2_SECTORS))
QEMU_AUDIO_ARGS ?= -device sb16
EXTRA_PROGRAMS_IMG := tests/fixtures/disk-images/programas-extra.img
EXTRA_CODEX_OBJ := programas extras/Bles-Codex32-standalone/build/CODEX.O
EXTRA_HYPERZIP_OBJ := programas extras/HyperZip-BlesKernOS-Standalone/build/HYPERZIP.O
EXTRA_VIEWER_OBJ := programas extras/viewer/build/VIEWER.O
EXTRA_LEXINET_OBJ := programas extras/LexiNet/build/LEXINET.O
EXTRA_CIV2_OBJ := programas extras/Civ2-BlesKernOS-Standalone/build/CIV2.O

ifdef CDROM_IMG
QEMU_CDROM_ARGS := -drive file=$(CDROM_IMG),media=cdrom,if=ide,readonly=on
else
QEMU_CDROM_ARGS :=
endif

# Layout del floppy FAT12:
# Sector 0       = Stage 1 + BPB FAT12
# Sectores 1-4   = Stage 2
# Sectores 9..(8 + KERNEL_STAGE2_SECTORS) = Kernel crudo reservado para stage2
# 1152 sectores cargan desde 0x10000 hasta 0xA0000 (limite antes de VGA).
# Sectores siguientes                    = FAT12 + root dir + data
#
# Estructura de la imagen principal FAT32:
#   /SYSTEM/PROGRAMS/*.O     ← aplicaciones nativas
#   /SYSTEM/CORE/*.O         ← componentes centrales del escritorio
#   /SYSTEM/WIN32/*.EXE      ← aplicaciones Win32

.PHONY: all user developer editions clean run run-svga run-net run-ac97 run-usb run-uhci build win32-tests sdk mesa35 netsurf extra-programs-image graphics-pak vmware-image FORCE

# Los proyectos de "programas extras" son repositorios independientes. No
# deben bloquear la compilación normal del SO; su imagen se genera sólo con
# `make extra-programs-image` cuando se necesita para una prueba concreta.
all: $(ATA_IMG) $(USB_IMG) $(VMWARE_IMG)

user:
	$(MAKE) EDITION=user all

developer:
	$(MAKE) EDITION=developer all

editions:
	$(MAKE) EDITION=user all
	$(MAKE) EDITION=developer all
	@echo ""
	@echo "============================================"
	@echo "  BlesKernOS User + Developer listos"
	@echo "  User ATA:      build/bleskernos-ata-user.img"
	@echo "  User USB:      build/bleskernos-usb-user.img"
	@echo "  User VMware:   build/bleskernos-vmware-user.vmdk"
	@echo "  Developer ATA: build/bleskernos-ata-developer.img"
	@echo "  Developer USB: build/bleskernos-usb-developer.img"
	@echo "  Developer VMware: build/bleskernos-vmware-developer.vmdk"
	@echo "============================================"

build:
	@mkdir -p build

# Standalone programs are kept out of the OS image, but bundled in a separate
# FAT fixture so they can be attached or copied into a test image as needed.
extra-programs-image: $(EXTRA_PROGRAMS_IMG)

graphics-pak:
	$(PYTHON) tools/build_graphics_pak.py --download

$(EXTRA_PROGRAMS_IMG): FORCE tools/build_extra_programs_image.py
	@$(MAKE) -C "programas extras/Bles-Codex32-standalone" BLESKERNOS_SDK=../../sdk
	@$(MAKE) -C "programas extras/HyperZip-BlesKernOS-Standalone" SDK_INCLUDE=../../sdk/include
	@$(MAKE) -C "programas extras/viewer" ROOT=../..
	@$(MAKE) -C "programas extras/LexiNet" ROOT=../..
	@$(MAKE) -C "programas extras/Civ2-BlesKernOS-Standalone" ROOT=../..
	@echo "[IMG]  Empaquetando programas extra en $@..."
	@$(PYTHON) tools/build_extra_programs_image.py "$@" "$(EXTRA_CODEX_OBJ)" "$(EXTRA_HYPERZIP_OBJ)" "$(EXTRA_VIEWER_OBJ)" "$(EXTRA_LEXINET_OBJ)" "$(EXTRA_CIV2_OBJ)"
	@echo "[OK]   Imagen de programas extra: $@"

FORCE:

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

$(BOOT1_CD_BIN): $(BOOT1_CD_SRC) | build
	@echo "[NASM] Ensamblando Stage 1 especial de instalacion CD..."
	$(NASM) $(NASM_FLAGS) -DRESERVED_SECTORS=$(FAT_RESERVED_SECTORS) -DBOOT_MODE_MAGIC=0x54534E49 $< -o $@
	@SIZE=$$(wc -c < $@); \
	if [ $$SIZE -ne 512 ]; then \
		echo "[ERROR] Stage 1 CD debe ser 512 bytes, es $$SIZE bytes!"; \
		exit 1; \
	fi
	@echo "[OK]   Stage 1 CD: $$(wc -c < $@) bytes"

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
$(BOOT2_BIN): $(BOOT2_SRC) | build $(KERNEL_BIN)
	@echo "[NASM] Ensamblando Stage 2..."
	@SECTORS=$$(( ($$(wc -c < $(KERNEL_BIN)) + 511) / 512 )); \
	 echo "[NASM] Kernel real: $$SECTORS sectores"; \
	 $(NASM) $(NASM_FLAGS) -DKERNEL_SECTORS=$$SECTORS $< -o $@
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

# Trampoline SMP escrito en GNU assembler para no depender de relocalizaciones
# al copiarlo a memoria baja.
build/%.o: %.S | build
	@mkdir -p $(dir $@)
	@echo "[GAS]  Compilando $<..."
	$(CC) -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -c $< -o $@

# Mesa 3.5: compilar el backend híbrido GFX3D/SWRAST con headers freestanding.
$(MESA35_OBJS): build/%.o: %.c | build
	@mkdir -p $(dir $@)
	@echo "[MESA] Compilando $<..."
	$(CC) $(MESA35_CFLAGS) -c $< -o $@

# Rutas calientes del escritorio. -O2 conserva compatibilidad i486 y
# optimiza la planificación para Pentium; el resto del sistema conserva -Os.
# memory.c queda fuera: su ensamblador inline necesita el perfil compacto -Os
# para que GCC pueda asignar registros válidos al compilar para i486.
HOT_GUI_OBJS := \
	build/gui/gfx.o \
	build/gui/compositor.o \
	build/gui/desktop.o \
	build/gui/windows.o \
	build/gui/widget.o \
	build/gui/font.o \
	build/gui/image.o

$(HOT_GUI_OBJS): build/%.o: %.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC:HOT] Compilando $<..."
	$(CC) $(CC_FLAGS) -O2 -mtune=pentium $(INCLUDE_FLAGS) -c $< -o $@

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

$(WINE_A): $(WINE_OBJS) | build
	@mkdir -p $(dir $@)
	@echo "[AR]   Unificando compatibilidad Win32 en WINE.A..."
	$(AR) rcs $@ $^
	@test -s $@ || { echo "[ERROR] WINE.A vacío"; rm -f $@; exit 1; }
	@echo "[OK]   WINE.A: $$(wc -c < $@) bytes"

$(TINYGL_A): $(TINYGL_OBJS) | build
	@mkdir -p $(dir $@)
	@echo "[AR]   Creando TinyGL externa..."
	$(AR) rcs $@ $^

$(MESA35_A): $(MESA35_OBJS) | build
	@mkdir -p $(dir $@)
	@echo "[AR]   Creando Mesa 3.5 GFX3D/OSMesa..."
	$(AR) rcs $@ $^
	@test -s $@ || { echo "[ERROR] Mesa 3.5 vacia"; rm -f $@; exit 1; }

mesa35: $(MESA35_A)
	@echo "[OK]   Mesa 3.5: $(MESA35_A)"

$(SDK_SYSCALL_OBJ): sdk/lib/syscalls.c sdk/include/bleskernos.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando ABI de user-space..."
	$(CC) $(SDK_CFLAGS) -c $< -o $@

$(SDK_THREAD_OBJ): sdk/lib/thread.c sdk/include/bleskernos.h \
		sdk/include/bleskernos_thread.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando hilos y job pool del SDK..."
	$(CC) $(SDK_CFLAGS) -c $< -o $@

$(SDK_PRINT_OBJ): sdk/lib/print.c sdk/include/bleskernos_print.h \
		sdk/include/bleskernos_api.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando API de impresion..."
	$(CC) $(SDK_CFLAGS) -c $< -o $@

$(SDK_A): $(SDK_SYSCALL_OBJ) $(SDK_THREAD_OBJ) $(SDK_PRINT_OBJ) | build
	@mkdir -p $(dir $@)
	@rm -f $@
	$(AR) rcs $@ $^

$(SDK_TINYGL_OBJ): sdk/lib/tinygl.c sdk/include/bleskernos_tinygl.h \
	sdk/include/TGL/gl.h sdk/include/tinygl/zbuffer.h sdk/include/tinygl/zfeatures.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando API TinyGL del SDK..."
	$(CC) $(SDK_CFLAGS) -c $< -o $@

$(SDK_TINYGL_A): $(SDK_TINYGL_OBJ) $(TINYGL_A) | build
	@mkdir -p $(dir $@)
	@rm -f $@
	@mkdir -p build/sdk/tinygl-merge
	@rm -f build/sdk/tinygl-merge/*.o
	@cd build/sdk/tinygl-merge && $(AR) x ../../../$(TINYGL_A)
	@cp $(SDK_TINYGL_OBJ) build/sdk/tinygl-merge/bleskernos_tinygl.o
	$(AR) rcs $@ build/sdk/tinygl-merge/*.o

$(SDK_MESA_OBJ): sdk/lib/mesa.c sdk/include/bleskernos_mesa.h \
		sdk/include/GL/gl.h sdk/include/GL/glext.h sdk/include/GL/osmesa.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando API Mesa 3.5 del SDK..."
	$(CC) $(SDK_CFLAGS) -c $< -o $@

$(SDK_MESA_A): $(SDK_MESA_OBJ) $(MESA35_A) | build
	@mkdir -p $(dir $@)
	@rm -f $@
	@mkdir -p build/sdk/mesa35-merge
	@rm -f build/sdk/mesa35-merge/*.o
	@cd build/sdk/mesa35-merge && $(AR) x ../../../$(MESA35_A)
	@cp $(SDK_MESA_OBJ) build/sdk/mesa35-merge/bleskernos_mesa.o
	$(AR) rcs $@ build/sdk/mesa35-merge/*.o

sdk: $(SDK_A) $(SDK_TINYGL_A) $(SDK_MESA_A)
	@echo "[OK]   SDK nativo: $(SDK_A)"
	@echo "[OK]   SDK TinyGL: $(SDK_TINYGL_A)"
	@echo "[OK]   SDK Mesa 3.5: $(SDK_MESA_A)"

$(KERNEL_ELF): $(KERNEL_ENTRY_OBJ) $(KERNEL_ASM_OBJS) $(KERNEL_GAS_OBJS) $(KERNEL_OBJS) $(WINE_A) $(LIBC_A) kernel/linker.ld | build
	@echo "[LD]   Linkeando kernel..."
	$(LD) $(LD_FLAGS) $(KERNEL_ENTRY_OBJ) $(KERNEL_ASM_OBJS) $(KERNEL_GAS_OBJS) $(KERNEL_OBJS) --whole-archive $(WINE_A) --no-whole-archive $(LIBC_A) -o $@

# Kernel: extraer binario plano del ELF
$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "[OBJCOPY] Extrayendo binario..."
	objcopy -O binary $< $@
	@SIZE=$$(wc -c < $@); \
	if [ $$SIZE -gt 1048576 ]; then \
		echo "[ERROR] kernel binario supera 1 MiB: $$SIZE bytes"; \
		rm -f $@; \
		exit 1; \
	fi
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
NETSURF_OBJ := build/programs/netsurf.o
SETUP_OBJ := build/programs/setup.o
SYSTEM_TOOL_NAMES := help_center find_files archive_manager disk_tools network_status clipboard_viewer performance_monitor
SYSTEM_TOOL_OBJS := $(addprefix build/programs/,$(addsuffix .o,$(SYSTEM_TOOL_NAMES)))
NETSURF_VENDOR_SOURCES := \
	programs/netsurf/vendor/libparserutils/src/charset/aliases.c \
	programs/netsurf/vendor/libparserutils/src/charset/codec.c \
	programs/netsurf/vendor/libparserutils/src/charset/codecs/codec_8859.c \
	programs/netsurf/vendor/libparserutils/src/charset/codecs/codec_ascii.c \
	programs/netsurf/vendor/libparserutils/src/charset/codecs/codec_ext8.c \
	programs/netsurf/vendor/libparserutils/src/charset/codecs/codec_utf16.c \
	programs/netsurf/vendor/libparserutils/src/charset/codecs/codec_utf8.c \
	programs/netsurf/vendor/libparserutils/src/charset/encodings/utf16.c \
	programs/netsurf/vendor/libparserutils/src/charset/encodings/utf8.c \
	programs/netsurf/vendor/libparserutils/src/input/filter.c \
	programs/netsurf/vendor/libparserutils/src/input/inputstream.c \
	programs/netsurf/vendor/libparserutils/src/utils/buffer.c \
	programs/netsurf/vendor/libparserutils/src/utils/errors.c \
	programs/netsurf/vendor/libparserutils/src/utils/stack.c \
	programs/netsurf/vendor/libparserutils/src/utils/vector.c \
	programs/netsurf/vendor/libwapcaplet/src/libwapcaplet.c \
	programs/netsurf/vendor/libhubbub/src/charset/detect.c \
	programs/netsurf/vendor/libhubbub/src/parser.c \
	programs/netsurf/vendor/libhubbub/src/tokeniser/entities.c \
	programs/netsurf/vendor/libhubbub/src/tokeniser/tokeniser.c \
	$(filter-out programs/netsurf/vendor/libhubbub/src/treebuilder/autogenerated-element-type.c,$(wildcard programs/netsurf/vendor/libhubbub/src/treebuilder/*.c)) \
	programs/netsurf/vendor/libhubbub/src/utils/errors.c \
	programs/netsurf/vendor/libhubbub/src/utils/string.c \
	$(wildcard programs/netsurf/vendor/libcss/src/*.c) \
	$(wildcard programs/netsurf/vendor/libcss/src/charset/*.c) \
	$(wildcard programs/netsurf/vendor/libcss/src/lex/*.c) \
	$(wildcard programs/netsurf/vendor/libcss/src/parse/*.c) \
	$(filter-out programs/netsurf/vendor/libcss/src/parse/properties/css_property_parser_gen.c,$(wildcard programs/netsurf/vendor/libcss/src/parse/properties/*.c)) \
	$(wildcard programs/netsurf/vendor/libcss/src/select/*.c) \
	$(wildcard programs/netsurf/vendor/libcss/src/select/properties/*.c) \
	$(wildcard programs/netsurf/vendor/libcss/src/utils/*.c)
NETSURF_VENDOR_OBJS := $(patsubst programs/netsurf/%.c,build/programs/netsurf/%.o,$(NETSURF_VENDOR_SOURCES))
NETSURF_RAW_OBJS := \
	build/programs/netsurf/main.o \
	build/programs/netsurf/platform.o \
	build/programs/netsurf/html_parser.o \
	build/programs/netsurf/dom_tree.o \
	build/programs/netsurf/css_engine.o \
	build/programs/netsurf/css_compat.o \
	build/programs/netsurf/layout.o \
	build/programs/netsurf/plotters.o \
	build/programs/netsurf/compat.o \
	$(NETSURF_VENDOR_OBJS)
NETSURF_CFLAGS := $(SDK_CFLAGS) -DNDEBUG -DWITHOUT_ICONV_FILTER -D_ALIGNED="__attribute__((aligned))" -DSTMTEXPR=1 \
	-fno-strict-aliasing -fno-asynchronous-unwind-tables -fno-unwind-tables \
	-Iprograms/netsurf/compat/include \
	-Iprograms/netsurf \
	-Iprograms/netsurf/vendor/libparserutils/include \
	-Iprograms/netsurf/vendor/libparserutils/src \
	-Iprograms/netsurf/vendor/libhubbub/include \
	-Iprograms/netsurf/vendor/libhubbub/src \
	-Iprograms/netsurf/vendor/libwapcaplet/include \
	-Iprograms/netsurf/vendor/libcss/include
PHASE2_RAW_OBJ := build/tests/userland/phase2.raw.o
PHASE2_OBJ := build/programs/phase2.o
RING3PROXY_OBJ := build/programs/ring3proxy.o
COMMAND_NAMES := help about uname hostname uptime date time shutdown reboot sleep bosrecovery \
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
WIN32_WIN98COMPAT_EXE := build/win32/WIN98COMPAT.EXE
WIN32_WIN95COMPAT2_EXE := build/win32/WIN95COMPAT2.EXE
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
INTERNET_CPL_OBJ := build/system/control/internet.o
MODEM_CPL_OBJ := build/system/control/modem.o
LANGUAGE_CPL_OBJ := build/system/control/language.o
CONTROL_OBJS := \
	$(CONTROL_PANEL_OBJ) \
	$(APPEAR_CPL_OBJ) \
	$(DISPLAY_CPL_OBJ) \
	$(SOUND_CPL_OBJ) \
	$(DATETIME_CPL_OBJ) \
	$(MOUSE_CPL_OBJ) \
	$(KEYBOARD_CPL_OBJ) \
	$(SYSTEM_CPL_OBJ) \
	$(DEVMGR_CPL_OBJ) \
	$(INTERNET_CPL_OBJ) \
	$(MODEM_CPL_OBJ) \
	$(LANGUAGE_CPL_OBJ)
GEARS_OBJ := build/programs/gears.o
GEARS_RAW_OBJ := build/programs/gears.raw.o
SCREENSAVERD_OBJ := build/system/services/screensaverd.o
PRINTSPOOL_OBJ := build/system/services/printspool.o
PRINT_TEST_RAW_OBJ := build/programs/print_test.raw.o
PRINT_TEST_OBJ := build/programs/print_test.o
SB16_DVR := build/system/drivers/SB16.DVR
AC97_DVR := build/system/drivers/AC97.DVR
MAESTRO3_DVR := build/system/drivers/MAESTRO3.DVR
RTC_DVR := build/system/drivers/CMOSRTC.DVR
ISO9660_DVR := build/system/drivers/ISO9660.DVR
PS2MOUSE_DVR := build/system/drivers/PS2MOUSE.DVR
USBCLASS_DVR := build/system/drivers/USBCLASS.DVR
VESA_DVR := build/system/drivers/VESA.DVR
VMWARESVGA_DVR := build/system/drivers/VMWARESVGA.DVR
ATIR128_DVR := build/system/drivers/ATIR128.DVR
ATIR1283D_DVR := build/system/drivers/ATIR1283D.DVR
VMWARESVGA3D_DVR := build/system/drivers/VMWARESVGA3D.DVR
VIRTIOGPU_DVR := build/system/drivers/VIRTIOGPU.DVR
INTELGMA_DVR := build/system/drivers/INTELGMA.DVR
THREEC90X_DVR := build/system/drivers/3C90X.DVR
NETSTACK_DVR := build/system/drivers/NETSTACK.DVR
RTL8139_DVR := build/system/drivers/RTL8139.DVR
TLS_DVR := build/system/drivers/TLS.DVR
TLS_RAW_OBJ := build/system/drivers/tls.raw.o
DRIVER_OBJS := $(AC97_DVR) $(MAESTRO3_DVR) $(SB16_DVR) $(RTC_DVR) $(ISO9660_DVR) \
	$(PS2MOUSE_DVR) $(USBCLASS_DVR) $(VESA_DVR) $(VMWARESVGA_DVR) $(VMWARESVGA3D_DVR) $(VIRTIOGPU_DVR) $(INTELGMA_DVR) $(THREEC90X_DVR) $(NETSTACK_DVR) $(RTL8139_DVR) \
	$(TLS_DVR)

SSLOGO_OBJ := build/system/screensavers/ss_logo.o
SSPIPES_OBJ := build/system/screensavers/ss_pipes.o
SSPIPES_RAW_OBJ := build/system/screensavers/ss_pipes.raw.o
SSBALLS_OBJ := build/system/screensavers/ss_balls.o
SSBALLS_RAW_OBJ := build/system/screensavers/ss_balls.raw.o

USER_EXTERNAL_APP_OBJS := \
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
	$(PAINT_OBJ) \
	$(SCANDISK_OBJ) \
	$(WINE_OBJ) \
	$(NETSURF_OBJ) \
	$(SYSTEM_TOOL_OBJS) \
	$(COMMAND_OBJS) \
	$(CONTROL_OBJS) \
	$(SCREENSAVERD_OBJ) \
	$(PRINTSPOOL_OBJ) \
	$(SSLOGO_OBJ) \
	$(SSPIPES_OBJ) \
	$(SSBALLS_OBJ)

DEVELOPER_EXTERNAL_APP_OBJS := \
	$(APITEST_OBJ) \
	$(PHASE2_OBJ) \
	$(RING3PROXY_OBJ) \
	$(GEARS_OBJ) \
	$(PRINT_TEST_OBJ)

ifeq ($(EDITION),developer)
EXTERNAL_APP_OBJS := $(USER_EXTERNAL_APP_OBJS) $(DEVELOPER_EXTERNAL_APP_OBJS)
WIN32_EDITION_DEPS := $(WIN32_HELLO_EXE) $(WIN32_NOTEPAD_EXE) $(WIN32_MSGBOX_EXE) $(WIN32_DYNLOAD_EXE) $(WIN32_DLLTEST_EXE) $(WIN32_TEST_DLL) $(WIN32_TLSTEST_EXE) $(WIN32_THREADTEST_EXE) $(WIN32_SYNCTEST_EXE) $(WIN32_RESOURCETEST_EXE) $(WIN32_MENUTEST_EXE) $(WIN32_DIALOGTEST_EXE) $(WIN32_SEHTEST_EXE) $(WIN32_WINECALC_COMPAT_EXE) $(WIN32_EDITTEST_EXE) $(WIN32_WIN98COMPAT_EXE) $(WIN32_WIN95COMPAT2_EXE)
else
EXTERNAL_APP_OBJS := $(USER_EXTERNAL_APP_OBJS)
WIN32_EDITION_DEPS :=
endif

$(SYSTEM_TOOL_OBJS): build/programs/%.o: programs/%.c programs/system_tools_common.h sdk/include/bleskernos_api.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando utilidad $*..."
	$(CC) $(SDK_CFLAGS) -c $< -o $@
	@echo "[OK]   Utilidad: $@"

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

$(SB16_DVR): kernel/drivers/audio/sound.c kernel/include/driver.h kernel/include/sound.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando driver de sonido separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(AC97_DVR): kernel/drivers/audio/ac97.c kernel/include/driver.h kernel/include/sound.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando driver AC97 ICH-compatible separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(MAESTRO3_DVR): kernel/drivers/audio/maestro3.c kernel/drivers/audio/maestro3_regs.h \
		kernel/drivers/audio/maestro3_firmware.h kernel/include/driver.h \
		kernel/include/pci.h kernel/include/sound.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando driver externo ESS Maestro3..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(RTC_DVR): kernel/drivers/platform/rtc.c kernel/include/driver.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando driver CMOS/RTC separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(ISO9660_DVR): kernel/drivers/storage/iso9660.c kernel/include/driver.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando driver ISO9660 separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(PS2MOUSE_DVR): kernel/drivers/input/mouse.c kernel/include/driver.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando driver de mouse PS/2 separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(USBCLASS_DVR): kernel/drivers/usb/usbclass.c kernel/include/driver.h \
		kernel/include/usb_core.h kernel/include/keyboard.h kernel/include/mouse.h \
		kernel/include/lpt.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando USBCLASS.DVR (HID, hubs e impresoras USB)..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(VESA_DVR): kernel/drivers/graphics/vesa.c kernel/include/driver.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando driver VESA separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(VMWARESVGA_DVR): kernel/drivers/graphics/vmware_svga_dvr.c kernel/include/driver.h \
		kernel/include/gfx.h kernel/include/gfx_driver.h kernel/include/pci.h \
		kernel/include/svga_transport.h kernel/include/svga3d_protocol.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando VMWARESVGA.DVR (VMware SVGA-II 2D)..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@
$(ATIR128_DVR): kernel/drivers/graphics/ati_rage128_dvr.c kernel/include/driver.h \
		kernel/include/gfx.h kernel/include/gfx_driver.h kernel/include/pci.h \
		kernel/include/vesa.h kernel/include/rage128_engine.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando ATIR128.DVR (Rage Mobility M3 2D/cursor/overlay)..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(ATIR1283D_DVR): kernel/drivers/graphics/ati_rage128_3d_dvr.c \
		kernel/drivers/graphics/ati_rage128_cce_ucode.h kernel/include/driver.h \
		kernel/include/gfx.h kernel/include/gfx3d.h \
		kernel/include/gfx3d_driver.h kernel/include/pci.h \
		kernel/include/rage128_engine.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando ATIR1283D.DVR (CCE/PM4 fixed-function)..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(VMWARESVGA3D_DVR): kernel/drivers/graphics/vmware_svga3d.c kernel/include/driver.h \
		kernel/include/gfx3d.h kernel/include/gfx3d_driver.h \
		kernel/include/svga3d_protocol.h kernel/include/svga_transport.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando VMWARESVGA3D.DVR (extension 3D)..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(VIRTIOGPU_DVR): kernel/drivers/graphics/virtio_gpu_dvr.c kernel/include/driver.h \
		kernel/include/gfx.h kernel/include/gfx_driver.h kernel/include/pci.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando VIRTIOGPU.DVR (VirtIO GPU moderno)..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(INTELGMA_DVR): kernel/drivers/graphics/intel_gma9xx_dvr.c \
		kernel/include/driver.h kernel/include/gfx.h kernel/include/gfx_driver.h \
		kernel/include/pci.h kernel/include/vesa.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando INTELGMA.DVR (GMA 9xx)..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(THREEC90X_DVR): kernel/drivers/network/3c90x.c kernel/include/driver.h \
		kernel/include/network.h kernel/include/pci.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando driver Ethernet externo 3c90x..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(NETSTACK_DVR): kernel/drivers/network/netstack.c kernel/include/driver.h \
		kernel/include/network.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando pila IPv4 externa..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(RTL8139_DVR): kernel/drivers/network/rtl8139.c kernel/include/driver.h \
		kernel/include/network.h kernel/include/pci.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando driver Ethernet externo RTL8139..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

build/libs/bearssl/%.o: libs/bearssl/%.c | build
	@mkdir -p $(dir $@)
	$(CC) $(CC_FLAGS) -Ikernel -Ilibs/bearssl/inc -Ilibs/bearssl/src \
		-DBR_RDRAND=0 -DBR_AES_X86NI=0 -DBR_SSE2=0 \
		-DBR_USE_URANDOM=0 -DBR_USE_UNIX_TIME=0 \
		-DBR_USE_WIN32_RAND=0 -DBR_USE_WIN32_TIME=0 -c $< -o $@

$(BEARSSL_A): $(BEARSSL_OBJS) | build
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(TLS_RAW_OBJ): kernel/drivers/network/tls.c kernel/drivers/network/tls_trust_anchors.c \
		kernel/include/driver.h kernel/include/network.h libs/bearssl/inc/bearssl.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando proveedor TLS externo BearSSL..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -Ilibs/bearssl/inc \
		-DBR_RDRAND=0 -DBR_AES_X86NI=0 -DBR_SSE2=0 \
		-DBR_USE_URANDOM=0 -DBR_USE_UNIX_TIME=0 \
		-DBR_USE_WIN32_RAND=0 -DBR_USE_WIN32_TIME=0 -c $< -o $@

$(TLS_DVR): $(TLS_RAW_OBJ) $(BEARSSL_A) $(LIBC_A)
	$(LD) -m elf_i386 -r $(TLS_RAW_OBJ) $(BEARSSL_A) $(LIBC_A) -o $@
	@echo "[OK]   Proveedor TLS externo: $@"



$(SETUP_OBJ): programs/setup.c programs/system_tools_common.h sdk/include/bleskernos_api.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando asistente grafico SETUP.BEX..."
	$(CC) $(SDK_CFLAGS) -c $< -o $@
	@echo "[OK]   Setup object: $@"

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

build/programs/netsurf/vendor/libcss/%.o: programs/netsurf/vendor/libcss/%.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando libcss: $<..."
	$(CC) -Iprograms/netsurf/vendor/libcss/src $(NETSURF_CFLAGS) -c $< -o $@

build/programs/netsurf/%.o: programs/netsurf/%.c programs/netsurf/platform.h \
		programs/netsurf/html_parser.h programs/netsurf/dom_tree.h \
		programs/netsurf/layout.h programs/netsurf/css_engine.h \
		sdk/include/bleskernos_api.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando frontend NetSurf/Hubbub: $<..."
	$(CC) $(NETSURF_CFLAGS) -c $< -o $@

$(NETSURF_OBJ): $(NETSURF_RAW_OBJS)
	@mkdir -p $(dir $@)
	$(LD) -m elf_i386 -r $^ -o $@
	@echo "[OK]   NetSurf frontend + Hubbub DOM + libcss layout: $@"

netsurf: $(NETSURF_OBJ)

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

$(WIN32_WIN98COMPAT_EXE): tests/win32/win98compat.c | build
	@mkdir -p $(dir $@)
	@command -v $(WIN32_CC) >/dev/null 2>&1 || { echo "[ERROR] Falta $(WIN32_CC)."; exit 1; }
	@echo "[MINGW] Construyendo WIN98COMPAT.EXE..."
	$(WIN32_CC) $(WIN32_CFLAGS) $< $(WIN32_LDFLAGS) -lkernel32 -luser32 -lole32 -loleaut32 -lversion -lwinmm -lshlwapi -lrpcrt4 -limm32 -lgcc -o $@
	@echo "[OK]   Win95/98 compatibility test: $@"

$(WIN32_WIN95COMPAT2_EXE): tests/win32/win95compat2.c | build
	@mkdir -p $(dir $@)
	@command -v $(WIN32_CC) >/dev/null 2>&1 || { echo "[ERROR] Falta $(WIN32_CC)."; exit 1; }
	@echo "[MINGW] Construyendo WIN95COMPAT2.EXE..."
	$(WIN32_CC) $(WIN32_CFLAGS) $< $(WIN32_LDFLAGS) -lkernel32 -luser32 -lwsock32 -lshell32 -lcomctl32 -lddraw -ldsound -llz32 -lwininet -lmsacm32 -lavifil32 -lmpr -lgcc -o $@
	@echo "[OK]   Win95 compatibility batch 2: $@"

win32-tests: $(WIN32_HELLO_EXE) $(WIN32_NOTEPAD_EXE) $(WIN32_MSGBOX_EXE) $(WIN32_DYNLOAD_EXE) $(WIN32_DLLTEST_EXE) $(WIN32_TEST_DLL) $(WIN32_TLSTEST_EXE) $(WIN32_THREADTEST_EXE) $(WIN32_SYNCTEST_EXE) $(WIN32_RESOURCETEST_EXE) $(WIN32_MENUTEST_EXE) $(WIN32_DIALOGTEST_EXE) $(WIN32_SEHTEST_EXE) $(WIN32_WINECALC_COMPAT_EXE) $(WIN32_EDITTEST_EXE) $(WIN32_WIN98COMPAT_EXE) $(WIN32_WIN95COMPAT2_EXE)
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

$(GEARS_RAW_OBJ): programs/gears.c sdk/include/bleskernos_tinygl.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando gears..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -Isdk/include -c $< -o $@

$(GEARS_OBJ): $(GEARS_RAW_OBJ) $(SDK_TINYGL_A)
	@echo "[LD]   Asociando gears con TinyGL..."
	$(LD) -m elf_i386 -r $< $(SDK_TINYGL_A) -o $@
	@echo "[OK]   Gears object: $@"

$(SCREENSAVERD_OBJ): system/services/screensaverd.c | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando screensaverd como programa separado..."
	$(CC) $(CC_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@
	@echo "[OK]   ScreenSaver daemon object: $@"

$(PRINTSPOOL_OBJ): system/services/printspool.c \
		sdk/include/bleskernos_api.h sdk/include/bleskernos_print.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando PrintSpool Ring 3..."
	$(CC) $(SDK_CFLAGS) -c $< -o $@
	@echo "[OK]   PrintSpool: $@"

$(PRINT_TEST_RAW_OBJ): programs/print_test.c \
		sdk/include/bleskernos_api.h sdk/include/bleskernos_print.h | build
	@mkdir -p $(dir $@)
	@echo "[GCC]  Compilando prueba de impresion..."
	$(CC) $(SDK_CFLAGS) -c $< -o $@

$(PRINT_TEST_OBJ): $(PRINT_TEST_RAW_OBJ) $(SDK_A)
	@mkdir -p $(dir $@)
	$(LD) -m elf_i386 -r $< $(SDK_A) -o $@
	@echo "[OK]   Prueba de impresion: $@"

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
$(DISK_IMG): $(WIN32_EDITION_DEPS)
$(ATA_IMG): $(WIN32_EDITION_DEPS) $(DRIVER_OBJS)

$(DISK_IMG): $(BOOT1_BIN) $(BOOT2_BIN) $(KERNEL_BIN) $(EXTERNAL_APP_OBJS) $(WIN32_EDITION_DEPS) $(TINYGL_A) $(LIBC_A) Desktop.INI Associations.INI tools/build_fat_floppy.py $(wildcard assets/icons/*.BMP) $(wildcard assets/icons/*.bmp) $(wildcard assets/wallpapers/*) assets/gif/abount.gif
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
	BLES_EDITION=$(EDITION) $(PYTHON) tools/build_fat_floppy.py $@ $(SHELL_OBJ) $(FILEBROWSER_OBJ) $(TEXTEDITOR_OBJ) $(CALCULATOR_OBJ) $(MIDAMP_OBJ) $(PROCESSMANAGER_OBJ) $(CALENDAR_OBJ) Desktop.INI assets/icons assets/gif/abount.gif Associations.INI $(FAT_RESERVED_SECTORS) $(SCREENSAVERD_OBJ) $(SSLOGO_OBJ) $(SSPIPES_OBJ) "" $(TINYGL_A) $(LIBC_A) $(CONTROL_OBJS) $(SSBALLS_OBJ)
	@echo "[OK]   Imagen: $$(wc -c < $@) bytes"

$(ATA_IMG): $(BOOT1_FAT32_BIN) $(BOOT2_BIN) $(KERNEL_BIN) $(EXTERNAL_APP_OBJS) $(DRIVER_OBJS) $(WIN32_EDITION_DEPS) $(TINYGL_A) $(MESA35_A) $(LIBC_A) Desktop.INI Associations.INI tools/build_fat32_ata.py tools/build_fat_floppy.py tools/build_icons_pak.py tools/build_bvi_pak.py assets/graphics/GRAPHICS.PAK $(wildcard assets/icons/*.BMP) $(wildcard assets/icons/*.bmp) $(wildcard assets/icons-bvi/*.bvi) $(wildcard assets/wallpapers/*) $(wildcard gui/cursor/*.cur) assets/gif/abount.gif assets/sounds/startup.wav system/leng/ESPANOL.LNG system/leng/ENGLISH.LNG system/leng/ITALIANO.LNG $(wildcard system/printers/*.BPD) docs/printing.md docs/usbclass.md $(WINE_A)
	@echo "[ATA]  Creando imagen FAT32 ATA..."
	BLES_EDITION=$(EDITION) $(PYTHON) tools/build_fat32_ata.py $@ $(SHELL_OBJ) $(FILEBROWSER_OBJ) $(TEXTEDITOR_OBJ) $(CALCULATOR_OBJ) $(MIDAMP_OBJ) $(PROCESSMANAGER_OBJ) $(CALENDAR_OBJ) Desktop.INI assets/icons assets/gif/abount.gif Associations.INI $(FAT_RESERVED_SECTORS) $(SCREENSAVERD_OBJ) $(SSLOGO_OBJ) $(SSPIPES_OBJ) "" $(BOOT1_FAT32_BIN) $(BOOT2_BIN) $(KERNEL_BIN) $(KERNEL_STAGE2_SECTORS) $(TINYGL_A) $(LIBC_A) $(CONTROL_OBJS) $(SSBALLS_OBJ) $(MESA35_A)
	@echo "[OK]   ATA: $$(wc -c < $@) bytes"

$(USB_IMG): $(ATA_IMG) $(MBR_BIN) tools/build_usb_image.py
	@echo "[USB]  Creando imagen USB-HDD con MBR..."
	$(PYTHON) tools/build_usb_image.py $@ $(ATA_IMG) $(MBR_BIN)
	@echo "[OK]   USB: $$(wc -c < $@) bytes"

# Disco VMware con MBR/particion activa. monolithicSparse evita inflar el
# build a 65 MiB y compat6 funciona tanto en VMware Player como Workstation.
$(VMWARE_IMG): $(USB_IMG)
	@command -v $(QEMU_IMG) >/dev/null 2>&1 || { \
		echo "[ERROR] qemu-img es necesario para crear el disco VMware"; exit 1; \
	}
	@echo "[VMDK] Creando disco VMware IDE..."
	$(QEMU_IMG) convert -f raw -O vmdk \
		-o subformat=monolithicSparse,compat6=on,adapter_type=ide \
		$(USB_IMG) $@
	@echo "[OK]   VMware: $@ ($$(wc -c < $@) bytes)"

vmware-image: $(VMWARE_IMG)

# Ejecutar en QEMU (requiere qemu-system-i386)
run: $(ATA_IMG)
	@echo "[QEMU] Iniciando BleskernOS..."
	$(QEMU) \
		-cpu qemu32,+rdrand \
		-drive file=$(ATA_IMG),format=raw,if=ide \
		-device rtl8139,netdev=bknet0 \
		-netdev user,id=bknet0 \
		$(QEMU_CDROM_ARGS) \
		$(QEMU_AUDIO_ARGS) \
		-boot c \
		-m 128M \
		-no-reboot \
		-no-shutdown

# VMware SVGA-II: framebuffer 32 bpp + FIFO 2D (UPDATE/FILL/COPY).
run-svga: $(ATA_IMG)
	@echo "[QEMU] Iniciando prueba VMware SVGA-II..."
	$(QEMU) \
		-cpu qemu32,+rdrand \
		-drive file=$(ATA_IMG),format=raw,if=ide \
		-vga vmware \
		-boot c \
		-m 128M \
		-serial stdio \
		-no-reboot \
		-no-shutdown

# VirtIO GPU 2D moderno. La variante virtio-vga conserva compatibilidad VGA
# para el bootsplash y luego VIRTIOGPU.DVR cambia al scanout nativo VirtIO.
run-virtio-gpu: $(ATA_IMG)
	@echo "[QEMU] Iniciando prueba VirtIO GPU 2D..."
	$(QEMU) \
		-cpu qemu32,+rdrand \
		-drive file=$(ATA_IMG),format=raw,if=ide \
		-device virtio-vga \
		-boot c \
		-m 256M \
		-serial stdio \
		-no-reboot \
		-no-shutdown

# VirGL requiere OpenGL en el host y virglrenderer en la compilacion de QEMU.
# Incluye backend gfx3d VirGL para TinyGL: shaders TGSI, textura, Z y DRAW_VBO.
run-virtio-gpu-gl: $(ATA_IMG)
	@echo "[QEMU] Iniciando prueba VirtIO GPU + VirGL..."
	$(QEMU) \
		-cpu qemu32,+rdrand \
		-drive file=$(ATA_IMG),format=raw,if=ide \
		-device virtio-vga-gl \
		-display gtk,gl=on,zoom-to-fit=off,show-cursor=on \
		-boot c \
		-m 256M \
		-serial stdio \
		-no-reboot \
		-no-shutdown

# Entorno reproducible para verificar RTL8139, TCP/IP y TLS. RDRAND es la
# fuente de entropia exigida por TLS.DVR; SLiRP entrega DHCP/DNS/Internet.
run-net: $(ATA_IMG)
	@echo "[QEMU] Iniciando prueba completa RTL8139 + red..."
	$(QEMU) \
		-cpu qemu32,+rdrand \
		-drive file=$(ATA_IMG),format=raw,if=ide \
		-device rtl8139,netdev=bknet0 \
		-netdev user,id=bknet0 \
		-boot c \
		-m 256M \
		-serial stdio \
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
run-usb: $(USB_IMG)
	@echo "[QEMU] Iniciando BleskernOS desde imagen USB-HDD..."
	$(QEMU) \
		-device usb-ehci,id=ehci \
		-drive id=usbdisk,file=$(USB_IMG),format=raw,if=none \
		-device usb-storage,drive=usbdisk,bootindex=1 \
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

run-debug: $(ATA_IMG)
	$(QEMU) \
		-drive file=$(ATA_IMG),format=raw,if=ide \
		$(QEMU_CDROM_ARGS) \
		$(QEMU_AUDIO_ARGS) \
		-boot c \
		-m 128M \
		-serial stdio \
		-no-reboot

clean:
	@echo "[CLEAN] Limpiando build..."
	rm -rf build/
	@echo "[OK]"

# Rebuild C objects when one of their kernel/program headers changes.
ALL_C_DEPS := $(WINE_OBJS:.o=.d) $(KERNEL_OBJS:.o=.d) $(LIBC_OBJS:.o=.d) $(TINYGL_OBJS:.o=.d) $(MESA35_OBJS:.o=.d) $(EXTERNAL_APP_OBJS:.o=.d) $(SETUP_OBJ:.o=.d) $(COMMAND_RAW_OBJS:.o=.d) $(COMMAND_COMMON_OBJ:.o=.d) $(APPEAR_CPL_RAW_OBJ:.o=.d) $(DISPLAY_CPL_RAW_OBJ:.o=.d) $(SDK_SYSCALL_OBJ:.o=.d) $(SDK_THREAD_OBJ:.o=.d) $(SDK_TINYGL_OBJ:.o=.d) $(SDK_MESA_OBJ:.o=.d) $(PHASE2_RAW_OBJ:.o=.d)
-include $(ALL_C_DEPS)


# =============================================================================
# Installer CD ISO9660 (El Torito)
# =============================================================================
ISO_IMAGE    := build/bleskernos-cd-user.iso
ISO_ROOT     := build/cdroot-user
ISO_BOOT_IMG := build/cdboot-user.img
ISO_BOOT_SECTORS := 2880
INSTALL_TARGET ?= build/installer-target.img
INSTALL_TARGET_MB ?= 64

.PHONY: iso iso-user run-iso run-installed installer-target reset-installer-target clean-iso

# La imagen El Torito contiene Stage 1 especial, Stage 2 y el kernel.
# El magic de Stage 1 fuerza el instalador en modo texto; discos normales no lo hacen.
$(ISO_BOOT_IMG): $(BOOT1_CD_BIN) $(BOOT2_BIN) $(KERNEL_BIN) | build
	@echo "[CD]   Creando imagen de arranque El Torito..."
	@SECTORS=$$(( ($$(wc -c < $(KERNEL_BIN)) + 511) / 512 )); \
	if [ $$((9 + SECTORS)) -gt $(ISO_BOOT_SECTORS) ]; then \
		echo "[ERROR] Kernel demasiado grande para cdboot.img: $$SECTORS sectores."; \
		exit 1; \
	fi
	@dd if=/dev/zero of=$@ bs=512 count=$(ISO_BOOT_SECTORS) status=none
	@dd if=$(BOOT1_CD_BIN) of=$@ bs=512 seek=0 conv=notrunc status=none
	@dd if=$(BOOT2_BIN) of=$@ bs=512 seek=1 conv=notrunc status=none
	@dd if=$(KERNEL_BIN) of=$@ bs=512 seek=9 conv=notrunc status=none
	@echo "[OK]   Boot image: $@"

$(ISO_IMAGE): $(ATA_IMG) $(ISO_BOOT_IMG) $(SETUP_OBJ) tools/build_cd_bkl_installer.py
	@command -v mcopy >/dev/null 2>&1 || { \
		echo "[ERROR] Falta mcopy (paquete mtools)."; \
		echo "        sudo apt install mtools xorriso"; exit 1; }
	@command -v xorriso >/dev/null 2>&1 || { \
		echo "[ERROR] Falta xorriso."; \
		echo "        sudo apt install mtools xorriso"; exit 1; }
	@echo "[CD]   Preparando instalador BKL3 por componentes..."
	@rm -rf $(ISO_ROOT)
	@mkdir -p $(ISO_ROOT)/.full-root $(ISO_ROOT)/INSTALL
	@mcopy -s -n -i $(ATA_IMG) ::/* $(ISO_ROOT)/.full-root/
	@$(PYTHON) tools/build_cd_bkl_installer.py \
		--source-root $(ISO_ROOT)/.full-root \
		--output $(ISO_ROOT)/INSTALL/BOOTSTRAP \
		--setup-exe $(SETUP_OBJ)
	@rm -rf $(ISO_ROOT)/.full-root
	@cp $(ISO_BOOT_IMG) $(ISO_ROOT)/BOOT.IMG
	@cp $(BOOT1_FAT32_BIN) $(ISO_ROOT)/INSTALL/BOOTF32.BIN
	@cp $(BOOT2_BIN) $(ISO_ROOT)/INSTALL/STAGE2.BIN
	@cp $(KERNEL_BIN) $(ISO_ROOT)/INSTALL/KERNEL.BIN
	@printf 'BlesKernOS 0.8 User Installer CD\nText bootstrap + graphical first boot Setup using BKL3 packages.\n' > $(ISO_ROOT)/CDINFO.TXT
	@echo "[ISO]  Generando $(ISO_IMAGE)..."
	@xorriso -as mkisofs -quiet \
		-V "BLES_USER" \
		-iso-level 3 -J -R \
		-b BOOT.IMG -c BOOT.CAT \
		$(ISO_ROOT) -o $(ISO_IMAGE)
	@echo "[OK]   Installer CD BKL3: $(ISO_IMAGE)"

iso: $(ISO_IMAGE)

iso-user: iso

installer-target: $(INSTALL_TARGET)

$(INSTALL_TARGET): | build
	@echo "[IMG]  Creando disco vacio de $(INSTALL_TARGET_MB) MiB para Setup..."
	@dd if=/dev/zero of=$@ bs=1M count=$(INSTALL_TARGET_MB) status=none
	@echo "[OK]   Target: $@"

reset-installer-target:
	@rm -f $(INSTALL_TARGET)
	@$(MAKE) $(INSTALL_TARGET) INSTALL_TARGET=$(INSTALL_TARGET) INSTALL_TARGET_MB=$(INSTALL_TARGET_MB)

run-iso: $(ISO_IMAGE) $(INSTALL_TARGET)
	@echo "[QEMU] Arrancando instalador CD User..."
	@echo "[QEMU] Disco destino: $(INSTALL_TARGET)"
	$(QEMU) \
		-cpu qemu32,+rdrand \
		-drive file=$(INSTALL_TARGET),format=raw,if=ide \
		-drive file=$(ISO_IMAGE),media=cdrom,if=ide,readonly=on \
		-device rtl8139,netdev=bknet0 \
		-netdev user,id=bknet0 \
		$(QEMU_AUDIO_ARGS) \
		-boot order=dc,once=d -m 256M -serial stdio -no-shutdown

run-installed: $(INSTALL_TARGET)
	@echo "[QEMU] Arrancando instalacion desde $(INSTALL_TARGET)..."
	$(QEMU) \
		-cpu qemu32,+rdrand \
		-drive file=$(INSTALL_TARGET),format=raw,if=ide \
		-device rtl8139,netdev=bknet0 \
		-netdev user,id=bknet0 \
		$(QEMU_AUDIO_ARGS) \
		-boot c -m 256M -serial stdio -no-reboot -no-shutdown

clean-iso:
	@rm -rf build/cdroot-user build/cdroot-developer \
		build/cdboot-user.img build/cdboot-developer.img \
		build/bleskernos-cd-user.iso build/bleskernos-cd-developer.iso
	@echo "[ISO] Artefactos del Installer CD eliminados."

# =============================================================================
# Instalador multidisquete BKL3
# =============================================================================
FLOPPY_INSTALL_DIR := build/img
FLOPPY_INSTALL_STAMP := $(FLOPPY_INSTALL_DIR)/.floppy-installer.stamp

.PHONY: floppy-installer _floppy-installer clean-floppy-installer run-floppy-installer

floppy-installer:
	@$(MAKE) --no-print-directory EDITION=user _floppy-installer

_floppy-installer: $(FLOPPY_INSTALL_STAMP)
	@cat $(FLOPPY_INSTALL_DIR)/MANIFEST.TXT

$(FLOPPY_INSTALL_STAMP): $(ATA_IMG) $(BOOT1_CD_BIN) \
		$(BOOT1_FAT32_BIN) $(BOOT2_BIN) $(KERNEL_BIN) \
		tools/build_floppy_installer.py
	@mkdir -p $(FLOPPY_INSTALL_DIR)
	@echo "[BKL3] Generando instalador multidisquete en $(FLOPPY_INSTALL_DIR)/..."
	@$(PYTHON) tools/build_floppy_installer.py \
		--ata-image $(ATA_IMG) \
		--boot1-installer $(BOOT1_CD_BIN) \
		--boot-fat32 $(BOOT1_FAT32_BIN) \
		--stage2 $(BOOT2_BIN) \
		--kernel $(KERNEL_BIN) \
		--output $(FLOPPY_INSTALL_DIR)
	@touch $@

run-floppy-installer: floppy-installer $(INSTALL_TARGET)
	@echo "[QEMU] Arrancando disk01.img; cambie medios desde el monitor de QEMU."
	$(QEMU) \
		-cpu qemu32,+rdrand \
		-drive file=$(INSTALL_TARGET),format=raw,if=ide \
		-drive file=$(FLOPPY_INSTALL_DIR)/disk01.img,format=raw,if=floppy \
		-boot a -m 256M -serial stdio -no-shutdown

clean-floppy-installer:
	@rm -rf $(FLOPPY_INSTALL_DIR)
	@echo "[BKL3] Imagenes de disquete eliminadas."
