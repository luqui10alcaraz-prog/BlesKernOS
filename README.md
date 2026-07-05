# BlesKernOS

BlesKernOS is an 32-bit operating system written from scratch in x86 Assembly and C.

It features a custom bootloader, a protected-mode kernel, a graphical desktop environment, a FAT filesystem, native applications, and an Doom port.


---

## Screenshot
<img width="799" height="599" alt="Captura de pantalla 2026-07-02 224002" src="https://github.com/user-attachments/assets/e7134b00-7bca-4d60-8192-2383515afa01" />
<img width="799" height="598" alt="Captura de pantalla 2026-07-02 224147" src="https://github.com/user-attachments/assets/2a1cd55c-a47a-46b6-bca7-67945d70ed0f" />
<img width="796" height="602" alt="Captura de pantalla 2026-07-05 032649" src="https://github.com/user-attachments/assets/559f631b-f4c6-4dc3-8289-92f81bbff0f5" />
<img width="799" height="604" alt="image" src="https://github.com/user-attachments/assets/8fe6fcc9-f26d-4d3d-8e04-677d1d784510" />
<img width="802" height="602" alt="Captura de pantalla 2026-07-02 224058" src="https://github.com/user-attachments/assets/7512fe19-1ada-4eeb-9eda-c0d91d44f89f" />
<img width="799" height="600" alt="Captura de pantalla 2026-07-02 224512" src="https://github.com/user-attachments/assets/6f9c2add-6da9-4880-a63b-575e7b3ccf2c" />
<img width="796" height="603" alt="Captura de pantalla 2026-07-05 031647" src="https://github.com/user-attachments/assets/01e41ead-1915-4838-b1f8-db01a0f76a2e" />
<img width="802" height="600" alt="Captura de pantalla 2026-07-05 032937" src="https://github.com/user-attachments/assets/38775fc8-bc0f-4db5-badc-7464a4f49fa9" />
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
- KolibriOS
