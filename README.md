# BlesKernOS 5.0

**BlesKernOS** is an experimental 32-bit operating system written from scratch in **x86 Assembly** and **C**. It features a custom two-stage bootloader, a protected-mode kernel with preemptive multitasking, a graphical desktop environment with window management, a FAT12/16/32 filesystem with VFS layer, and native applications including an experimental Doom port.

---

## Current Status

BlesKernOS 5.0 includes:

### Boot & Kernel
- **Two-stage bootloader** (Stage 1: 512-byte MBR with valid FAT12 BPB; Stage 2: protected-mode transition, E820 memory detection, A20 line enable, VESA LFB setup)
- **32-bit protected-mode kernel** loaded at 0x10000 (64 KiB)
- **GDT** with flat 4 GiB code/data segments + TSS for task switching
- **IDT** with 256 entries, ISR/IRQ stubs in assembly, C handlers
- **Programmable Interval Timer (PIT)** at 1000 Hz for preemptive scheduling
- **Kernel panic handler** with register dump, stack trace, and visual "bomb" animation
- **Minimal libc** (string, stdio, stdlib, math, ctype, gcc runtime helpers)

### Memory Management
- **E820 memory map** detection via BIOS (Stage 2) and kernel parsing
- **Physical frame allocator** (bitmap-based) for 4 KiB pages
- **Kernel heap** (kmalloc/kfree/krealloc/kzalloc) starting at 2 MiB with free-list allocator
- **BSS zeroing** at kernel entry (above 1 MiB to avoid VGA/BIOS hole)
- **Memory info syscalls** for userspace (heap stats, system RAM)

### Task System (Preemptive Multitasking)
- **Round-robin scheduler** with time-slice (PIT-driven)
- **Task states**: READY, RUNNING, SLEEPING, ZOMBIE
- **Kernel threads** and **user-mode tasks** (ring 3 via TSS + IRET)
- **Per-task stack** (kernel + user), context switch via `task_schedule()`
- **Sleep/wake** with tick resolution, `task_yield()`, `task_exit()`
- **CPU usage tracking** (idle vs active ticks)
- **Task-window binding** for GUI integration
- **Process manager** application with live CPU/memory graphs

### Interrupts & Hardware Abstraction
- **PIC (8259A)** remapped to IRQ 0x20–0x2F, EOI handling, IRQ masking
- **PS/2 Keyboard** driver with scancode set 2, modifier tracking, key event queue
- **PS/2 Mouse** driver (intellimouse protocol), packet parsing, bounds clamping
- **RTC (CMOS)** time/date reading with BCD conversion
- **PCI enumeration** (bus/slot/function), BAR parsing, class code lookup
- **PIT** one-shot/periodic modes, tick counter, uptime seconds

### Storage & Filesystem
- **Block device layer** with read/write ops, device registry (floppy, ATA, ATAPI, RAM)
- **ATA/ATAPI driver**: PIO mode, LBA28, IDENTIFY, PACKET commands for CD-ROM
- **Floppy driver** (FDC): CHS addressing, motor control, recalibrate, seek, DMA-safe reads
- **FAT12/16/32** driver: BPB parsing, cluster chains, directory traversal, long filename (SFN), file read/write, mkdir, FAT table updates
- **ISO9660** driver: primary volume descriptor, directory records, file read (for Doom WAD)
- **VFS (Virtual File System)**: unified path namespace, mount points, `open/read/write/close/mkdir/chdir/listdir`, CWD tracking, multiple simultaneous mounts (FAT + ISO9660)
- **File associations** via `Associations.INI` (extension → icon mapping)
- **Desktop icons** via `Desktop.INI` (configurable entries with positions)

### Graphics & Display
- **VGA text mode** (80×25, 16-color) for early boot / panic screen
- **VGA Mode 13h** (320×200×8bpp) and **Mode 12h** (640×480×4bpp) fallback
- **VESA LFB** (Linear Framebuffer) detection via VBE 2.0+ (Stage 2), mode listing, mode setting (16/24/32 bpp)
- **Bochs VBE (BGA)** emulation support for QEMU `-vga std`
-vesa`
- **Graphics abstraction** (`gfx_info_t`): framebuffer pointer, width, height, pitch, bpp, palette ops
- **Palette management** (VGA DAC 6-bit, VESA direct color)

### GUI System (Composited Desktop)
- **Surface-based rendering** (32-bit ARGB, software blitting)
- **Compositor**: back-to-front window painting, dirty-rect tracking, cursor overlay
- **Window manager**: titlebar (icon, title, minimize/close), resizing (8-edge drag), minimize/restore/close, z-order (raise/focus), content clipping
- **Menu system**: per-window menu bar, popup submenus, keyboard navigation (Alt+key)
- **Widget toolkit**: buttons, labels, checkboxes, text inputs, list views, progress bars, custom draw callbacks
- **Event system**: queue (mouse move/down/up, key down/up, timer, window messages), polling + blocking `gui_event_next()`
- **Bitmap font** (8×8, 8×16) with scaling, clipping, color blending (alpha lerp)
- **Image loading**: BMP (1/4/8/24/32 bpp, RLE), GIF (static + animated, LZW decode, disposal methods)
- **Gradient fills**, rounded rectangles, line drawing, color blending utilities
- **Desktop program**: wallpaper (BMP, scaled), icon grid (hit-test, drag, double-click launch), drive icons (floppy, HDD, CD-ROM, USB) with media detection
- **Deskbar (taskbar)**: start button, quick-launch, window list, system tray (clock, CPU%), cascading menus (Programs, Documents, Drives), shutdown/reboot
- **Launcher**: application registration, `.O` executable loading via ELF loader
- **Resolution switching** at runtime (VESA mode list → `gui_change_resolution()`)

### Native Applications
| Application | Description |
|-------------|-------------|
| **About** | System info (CPU brand, RAM, uptime, kernel version), animated logo (GIF) |
| **Calculator** | Four-function, keyboard + mouse input, expression display |
| **Deskbar** | Taskbar/start menu, window list, drive browser, shutdown |
| **Desktop Manager** | Wallpaper, icons, drive detection, drag-drop, double-click launch |
| **File Browser** | Directory tree, drive roots, copy/paste, text file preview, execute `.O` |
| **Games** | Connect Four (AI opponent), extensible framework |
| **Image Viewer** | BMP/GIF, zoom/pan, fit/actual, playlist (next/prev), animated GIF playback |
| **Launcher** | `.O` ELF program execution, path resolution |
| **MidAmp** | MIDI player (format 0/1), playlist, custom skin (GIF), seek, transpose, channel mute, software synthesis via SB16 |
| **Process Manager** | Task list (PID, name, state, CPU%, memory), kill, live graphs |
| **Settings** | Display mode picker (VESA modes), wallpaper picker, theme preview |
| **Shell** | Built-in commands (help, mem, ls, cd, cat, mkdir, pci, video, vesa, mouse, fatmount, disks, cpuid, uptime, reboot, halt, beep, audio, cdinfo, usb, open/read/close, heap, tasks, gdt, idt, lsmem, lsirq), history, tab-completion ready |
| **Shell Launcher** | GUI terminal window, runs shell commands, scrollback, ANSI-ready |
| **Text Editor** | Multi-line, selection, copy/paste, undo/redo, file open/save, line/col status, menu bar |

### Doom Port (Experimental)
- Based on **doomgeneric** (minimal platform abstraction)
- **BlesKernOS backend** (`doomgeneric_bleskernos.c`): VESA LFB framebuffer, PIT timer, PS/2 keyboard/mouse, SB16 sound (PCM + MIDI), FAT/ISO9660 WAD loading
- **DOOM1.WAD** (shareware) included
- **CD-ROM ISO** (`doom-extra.iso`) with additional WADs
- Forced improvements: ELF loader, ISO9660, SB16 driver, userspace program loading, C library coverage

### Build System
- **Makefile** with dependency tracking (`-MMD -MP`)
- **NASM** for bootloader (bin) + kernel entry/ISR stubs (ELF32)
- **GCC** `-m32 -ffreestanding -fno-builtin -nostdlib -nostdinc -Os`
- **Linker script** (kernel at 0x10000, BSS at 1 MiB+, stack at 0x1FF000)
- **Python script** (`tools/build_fat_floppy.py`) creates 1.44 MiB FAT12 floppy image with:
  - Stage 1 (sector 0), Stage 2 (sectors 1–4), Kernel (sectors 5–1028)
  - FAT12 filesystem with `/PROGRAMS`, `/DOCS`, `/MISC`, `/ICONS`
  - Pre-built `.O` executables (shell, filebrowser, texteditor, calculator, midamp, processmanager)
  - Desktop.INI, Associations.INI, icons (BMP), animated GIFs
- **QEMU** integration targets (`run`, `run-debug`, KVM/WHPX flags)

---

## Project Structure

```
BlesKernOS 5.0/
├── boot/                    # Bootloader (Stage 1 + Stage 2)
│   ├── boot.asm             # 512-byte MBR, FAT12 BPB, LBA/CHS load Stage 2
│   └── stage2.asm           # E820, A20, GDT, VESA, load kernel, enter PM
├── kernel/                  # Kernel source
│   ├── kernel.c             # Main initialization, driver startup, GUI launch
│   ├── entry.asm            # _start: BSS zero → kernel_main, gdt_flush, tss_flush
│   ├── linker.ld            # Link script: 0x10000 load, BSS @ 1 MiB+
│   ├── gdt.c/.h             # GDT + TSS init, flush stubs
│   ├── idt.c/.h             # IDT, ISR/IRQ handlers, syscall entry
│   ├── isr_stubs.asm        # Assembly ISR/IRQ entry points (naked)
│   ├── memory.c/.h          # Frame allocator, kmalloc/kfree, E820 parse
│   ├── task.c/.h            # Scheduler, TCB, context switch, user-mode
│   ├── syscall.c/.h         # Syscall dispatch (write, open, read, close, etc.)
│   ├── usermode.c           # Ring-3 smoke test
│   ├── panic.c              # Panic screen, register dump, stack trace
│   ├── pic.c/.h             # 8259A remap, EOI, IRQ mask/unmask
│   ├── pit.c/.h             # PIT 1000 Hz, ticks, uptime
│   ├── keyboard.c/.h        # PS/2 keyboard, scancode→ASCII, event queue
│   ├── mouse.c/.h           # PS/2 mouse, packet parse, bounds
│   ├── vga.c/.h             # Text mode 80×25, kprintf, scroll, serial
│   ├── shell.c/.h           # Built-in command shell
│   ├── elf_loader.c/.h      # ELF32 load/execute (programs)
│   ├── libc/                # Minimal freestanding C library
│   │   ├── string.c, ctype.c, stdlib.c, math.c, stdio.c, gcc_runtime.c
│   ├── drivers/             # Hardware & filesystem drivers
│   │   ├── block.c/.h       # Block device registry, read/write
│   │   ├── ata.c/.h         # ATA/ATAPI PIO, LBA28, CD-ROM
│   │   ├── floppy.c/.h      # FDC, CHS, motor, DMA-safe read
│   │   ├── fat.c/.h         # FAT12/16/32, VFS integration
│   │   ├── iso9660.c/.h     # ISO9660 read-only (WAD support)
│   │   ├── vfs.c/.h         # Virtual FS, mount, path resolve, syscalls
│   │   ├── pci.c/.h         # PCI enum, BAR, class codes
│   │   ├── gfx.c/.h         # VGA/VESA modes, LFB, palette
│   │   ├── vesa.c/.h        # VBE 2.0+, mode list/set, BGA
│   │   ├── vga.c/.h         # VGA Mode 12h/13h, text mode
│   │   ├── sound.c/.h       # SB16 DSP, DMA, PCM, MIDI, beep
│   │   ├── rtc.c/.h         # CMOS RTC, BCD time/date
│   │   ├── pit.c/.h         # PIT driver (shared with kernel/pit.c)
│   │   ├── keyboard.c/.h    # Keyboard driver (shared)
│   │   └── mouse.c/.h       # Mouse driver (shared)
│   ├── include/             # Kernel public headers
│   └── sys/                 # Syscall numbers, userspace ABI
├── gui/                     # Graphical User Interface
│   ├── gui.h                # Public API: types, window, widget, event, desktop
│   ├── gui.c                # Init, main loop, CPU usage
│   ├── gfx.c/.h             # Surface, blit, rect, gradient, rounded, blend
│   ├── font.c/.h            # 8×8/8×16 bitmap font, scaling, clipping
│   ├── windows.c/.h         # Window create/destroy, titlebar, menus, resize
│   ├── widget.c/.h          # Button, label, checkbox, input, list, progress
│   ├── desktop.c/.h         # Desktop program: wallpaper, icons, drives
│   ├── compositor.c/.h      # Back-to-front paint, dirty rects, cursor
│   ├── event.c/.h           # Event queue, poll, push/pop, timer events
│   ├── image.c/.h           # BMP load, GIF load (static + animated)
│   └── image.h              # gui_image_t, gui_gif_animation_t
├── programs/                # Native applications (linked into kernel)
│   ├── about.c, calculator.c, deskbar.c, deskmanager.c
│   ├── filebrowser.c, games.c, imageviewer.c, launcher.c
│   ├── midamp.c, processmanager.c, settings.c, shell.c
│   ├── shelllauncher.c, texteditor.c, programs.h
│   ├── doom/                # Doom port (doomgeneric)
│   │   ├── doomapp.c, doom_port.h, doomgeneric_bleskernos.c
│   │   ├── doomgeneric/     # Upstream doomgeneric sources
│   │   ├── DOOM1.WAD        # Shareware WAD
│   │   └── Makefile         # Builds doom-extra.iso
│   └── winmap/              # MidAmp skin assets (GIF)
├── assets/                  # System assets
│   ├── icons/               # 20 BMP icons (16×16, 32×32)
│   └── gif/                 # Animated GIFs (about, MidAmp skin)
├── tools/
│   └── build_fat_floppy.py  # FAT12 floppy image builder
├── build/                   # Generated artifacts (gitignored)
│   ├── boot.bin, stage2.bin, entry.o, isr_stubs.o
│   ├── kernel.elf, kernel.bin
│   ├── bleskernos.img, bleskernos-ata.img
│   └── programs/*.o
├── Desktop.INI              # Desktop icon layout (name,x,y)
├── Associations.INI         # Extension → icon mapping
├── Makefile
├── LICENSE
└── README.md
```

---

## Build Requirements

**Recommended environment:** Linux or WSL2

| Tool | Version | Purpose |
|------|---------|---------|
| `nasm` | ≥ 2.15 | Bootloader & assembly stubs |
| `gcc` | ≥ 9 | 32-bit freestanding C (`-m32`) |
| `binutils` | ≥ 2.34 | `ld`, `objcopy` |
| `python3` | ≥ 3.8 | `build_fat_floppy.py` |
| `qemu-system-i386` | ≥ 6.0 | Emulation |
| `make` | ≥ 4.2 | Build orchestration |

**Debian/Ubuntu/WSL:**
```bash
sudo apt update
sudo apt install nasm gcc-multilib binutils python3 qemu-system-x86 make
```

**Arch Linux:**
```bash
sudo pacman -S nasm gcc-multilib binutils python qemu-system-x86 make
```

**Fedora:**
```bash
sudo dnf install nasm gcc glibc-devel.i686 binutils python3 qemu-system-x86 make
```

---

## Building

```bash
# Full build (floppy image + ATA image + Doom ISO)
make

# Clean build artifacts
make clean

# Rebuild from scratch
make clean && make
```

**Output artifacts in `build/`:**
- `bleskernos.img` — 1.44 MiB FAT12 floppy (bootable)
- `bleskernos-ata.img` — Copy for IDE/ATA testing
- `kernel.elf` — Kernel with symbols (for debugging)
- `kernel.bin` — Flat binary (loaded by Stage 2)
- `programs/*.o` — Pre-linked `.O` executables for floppy `/PROGRAMS`

---

## Running in QEMU

### Minimal (floppy only)
```bash
qemu-system-i386 -drive file=build/bleskernos.img,format=raw -m 128M
```

### Recommended (floppy + ATA disk + Doom CD + Sound)
```bash
qemu-system-i386 \
  -drive file=build/bleskernos.img,format=raw,if=floppy \
  -drive file=build/bleskernos-ata.img,format=raw,if=ide \
  -drive file=programs/doom/build/doom-extra.iso,media=cdrom,if=ide,readonly=on \
  -device sb16 \
  -vga std \
  -rtc base=localtime \
  -boot a \
  -no-reboot \
  -no-shutdown \
  -m 128M
```

### With KVM (Linux host, faster)
```bash
qemu-system-i386 \
  -enable-kvm \
  -cpu host \
  -smp 2 \
  -m 512M \
  -drive file=build/bleskernos.img,format=raw,if=floppy \
  -drive file=build/bleskernos-ata.img,format=raw,if=ide \
  -drive file=programs/doom/build/doom-extra.iso,media=cdrom,if=ide,readonly=on \
  -device sb16 \
  -vga std \
  -rtc base=localtime \
  -boot a
```

### Debug (serial console)
```bash
make run-debug
# or manually:
qemu-system-i386 \
  -drive file=build/bleskernos.img,format=raw,if=floppy \
  -drive file=build/bleskernos-ata.img,format=raw,if=ide \
  -serial stdio \
  -no-reboot \
  -no-shutdown \
  -m 128M
```

**Boot menu:** Press `F12` in QEMU BIOS to select boot device (floppy = `a`, HDD = `c`, CD-ROM = `d`).

---

## Disk Images

| Image | Purpose |
|-------|---------|
| `build/bleskernos.img` | Primary boot floppy (FAT12, Stage 1/2, kernel, programs, assets) |
| `build/bleskernos-ata.img` | Identical copy attached as IDE/ATA for HDD-style access |
| `programs/doom/build/doom-extra.iso` | CD-ROM with `DOOM1.WAD` + additional WADs for Doom port |

The floppy layout (managed by `build_fat_floppy.py`):
```
Sector 0          : Stage 1 (MBR + BPB)
Sectors 1–4       : Stage 2
Sectors 5–1028    : Kernel binary (raw, loaded by Stage 2)
Sectors 1029+     : FAT12 filesystem
  /PROGRAMS/      : SHELL.O, FILEBROWSER.O, TEXTEDIT.O, CALC.O, MIDAMP.O, PROCMAN.O
  /DOCS/          : README.TXT, ROADMAP.TXT
  /MISC/          : PALETTE.TXT, THEMES.TXT
  /ICONS/         : 20× BMP icons
  DESKTOP.INI     : Icon positions
  ASSOC.INI       : File associations
  ABOUNT.GIF      : About dialog animation
  MIDHDR.GIF, MIDBTN.GIF, MIDBOT.GIF : MidAmp skin
```

---

## Development Notes

**BlesKernOS is alpha software.** Expect crashes, graphical glitches, filesystem corruption, and incomplete features. Run only in emulators (QEMU, Bochs, VirtualBox) unless you fully understand the risks.

### Current Limitations & Known Issues
- **No memory protection** between kernel tasks (flat 4 GiB, ring 0 only for kernel threads)
- **User-mode (Ring 3) infrastructure exists but programs not yet adapted**: `task_create_user()` creates Ring 3 tasks with separate user/kernel stacks, GDT user selectors (0x1B/0x23), and syscall entry via `INT 0x80`. Syscall ABI v1 provides: `exit`, `write`, `getpid`, `yield`, `sleep`, `uptime_ms`, `abi_version`. Native applications (shell, filebrowser, calculator, etc.) still run as kernel threads (Ring 0) linked into the kernel ELF. Adapting them to Ring 3 requires: (1) compiling as standalone ELF executables with `bleskernos_program_main` entry point, (2) replacing direct kernel API calls with syscalls, (3) using userspace libc wrappers, (4) loading via `elf_execute_program()` from filesystem.
- **FAT write support** is basic (no wear leveling, no journaling, cluster allocation is linear)
- **No networking stack** (no NIC drivers, no TCP/IP)
- **No USB stack** (OHCI/UHCI/EHCI/XHCI not implemented)
- **VESA mode switching** may flicker or fail on real hardware
- **SB16 audio** uses legacy ISA DMA (may not work on all emulators/hardware)
- **Doom port** runs at low resolution, no sound in some configs, input latency
- **GUI** has no hardware acceleration (software rendering only)
- **No dynamic linking** (`.O` programs are statically linked into kernel ELF; floppy `.O` are raw ELF loaded by `elf_loader`)

### Development Focus (Roadmap)
- [ ] **Userspace hardening**: proper syscall ABI, `mmap`, `fork`/`exec`, signal handling
- [ ] **Memory protection**: per-process page directories, copy-on-write, guard pages
- [ ] **Filesystem**: ext2 read/write, journaling, larger volumes, USB mass storage
- [ ] **Networking**: RTL8139/e1000 driver, TCP/IP stack, sockets API
- [ ] **USB**: UHCI/OHCI host controller, HID (keyboard/mouse), mass storage
- [ ] **Audio**: AC'97 / Intel HDA, mixer, PCM + MIDI in userspace
- [ ] **Graphics**: double-buffered compositor, hardware cursor, video modes > 32 bpp
- [ ] **Doom**: fullscreen, sound, save/load, multiplayer (serial/IPX)
- [ ] **Shell**: scripting, pipes, redirection, job control, tab completion
- [ ] **Package manager**: `.pkg` format, dependency resolution, repo index
- [ ] **Documentation**: kernel internals, driver model, syscall reference, porting guide

---

## Version 5.0 Highlights

| Area | Improvement |
|------|-------------|
| **Boot** | Stage 2 now detects VESA LFB before PM, passes bootinfo to kernel |
| **Kernel** | BSS moved above 1 MiB, heap at 2 MiB, stack at 0x1FF000, ASSERT in linker |
| **Tasks** | Preemptive round-robin, user-mode entry via TSS, CPU usage tracking |
| **Storage** | ATAPI CD-ROM support, ISO9660, multi-mount VFS, FAT write (mkdir, create) |
| **GUI** | Animated GIF, alpha blending, rounded rects, gradients, menu bar, deskbar |
| **Apps** | MidAmp (MIDI + skin), Image Viewer (zoom/pan/playlist), Process Manager (graphs) |
| **Doom** | Integrated into build, SB16 sound, ISO9660 WAD loading, ELF program model |
| **Build** | Dependency tracking, Python floppy builder, QEMU targets, Doom sub-make |
| **Config** | Desktop.INI (icons), Associations.INI (file→icon), runtime resolution switch |

---

## License

See [`LICENSE`](LICENSE) (MIT-style permissive license).

---

## Disclaimer

**This project is experimental software.**

It may crash, corrupt disk images, or behave unpredictably. Run it in an emulator such as QEMU unless you know exactly what you are doing. The authors accept no liability for data loss or hardware damage.

---

## Screenshots

![Desktop](https://github.com/user-attachments/assets/fc251c04-33b6-4114-a258-ca5b93347c75)
![File Browser](https://github.com/user-attachments/assets/2cad2363-bba5-412a-9f6e-ad7283e8d32c)
![Process Manager](https://github.com/user-attachments/assets/7451965e-0d2f-4cde-a931-49de9e914632)
![Settings](https://github.com/user-attachments/assets/f9abbe57-e644-4f05-b0f3-da418d4c3914)
![Doom](https://github.com/user-attachments/assets/32541486-25b7-4e80-bd7a-d17e40167b0d)

---

## Contributing

Issues and pull requests are welcome. Please:
1. Test in QEMU before submitting
2. Follow the existing code style (tabs, 4-space indent in C, NASM for asm)
3. Update `CHANGELOG.md` (if present) or describe changes in the PR
4. Keep commits atomic and messages descriptive

---

## Acknowledgments

- **doomgeneric** (Ozkl) — minimal Doom porting layer
- **OSDev Wiki** — invaluable reference for x86, PIC, PIT, PCI, VESA, FAT, ATA
- **James Molloy's kernel tutorial** — early inspiration for IDT/GDT/task structure
- **Bochs VBE (BGA)** — VESA emulation for QEMU
- **TinyGL** - Used for Gears.C