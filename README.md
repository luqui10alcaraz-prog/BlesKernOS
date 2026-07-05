# BlesKernOS

BlesKernOS is an 32-bit operating system written from scratch in x86 Assembly and C.

It features a custom bootloader, a protected-mode kernel, a graphical desktop environment, a FAT filesystem, native applications, and an Doom port.


---

## Screenshot

---

## Features

### Kernel
- Custom two-stage bootloader
- 32-bit protected mode
- Preemptive multitasking
- Ring 3 userspace (work in progress)
- ELF program loader
- Minimal freestanding C library

### Drivers
- ATA / ATAPI
- Floppy Disk Controller
- FAT12 / FAT16 / FAT32
- ISO9660
- PCI detection
- PS/2 keyboard & mouse
- Sound Blaster 16
- VESA graphics

### Desktop
- Window manager
- Taskbar
- Desktop icons
- Image loading (BMP/GIF)
- Configurable desktop via INI files

### Applications
- File Browser
- Text Editor
- Calculator
- Calendar
- Settings
- Process Manager
- MidAmp (MIDI Player)
- Paint
- Doom (experimental)
- Shell

---

## Building

Requirements:

- NASM
- GCC (32-bit)
- Python 3
- QEMU

Build:

```bash
make
```

Run:

```bash
qemu-system-i386 \
    -drive file=build/bleskernos.img,format=raw \
    -m 128M
```

---

## Project Structure

```
boot/       Bootloader
kernel/     Kernel
gui/        Window system
programs/   Native applications
assets/     Icons and images
tools/      Build tools
```

---

## Roadmap

Current focus for version 0.6:

- Complete Ring 3 migration
- Improve userspace API
- More native applications
- GUI improvements
- Better filesystem support

---

## License

MIT License.

---

## Acknowledgments

- DoomGeneric
- TinyGL
- OSDev Wiki
- James Molloy's Kernel Tutorial
- Bochs VBE documentation
