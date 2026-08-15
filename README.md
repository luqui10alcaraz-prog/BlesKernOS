# BlesKernOS 0.8
---

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
- Taskbar / deskbar
- Desktop icons
- Configurable desktop via INI files
- BMP/GIF image loading
- Packed icon loading through ICONS.PAK
- Screensavers

### Applications
- File Browser
- Text Editor
- Calculator
- Calendar
- Control Panel with `.CPL` applets
- Process Manager
- MidAmp (MIDI Player)
- Paint
- Doom (experimental)
- Shell
- Help Center
- Find Files
- Archive Manager
- Disk Tools
- Network Status
- Global text clipboard and Clipboard Viewer

---

## Building

Requirements:

- NASM
- GCC (32-bit)
- Python 3
- QEMU

Build the normal desktop edition:

```bash
make user
```

Build the developer edition, or both editions:

```bash
make developer
make editions
```

Run the User edition:

```bash
make EDITION=user run
```

The ATA images are written to `build/bleskernos-ata-user.img` and
`build/bleskernos-ata-developer.img`. See
[`docs/EDITIONS_AND_UTILITIES.md`](docs/EDITIONS_AND_UTILITIES.md).

### Installer CD

The ISO boots a text-mode installer instead of the desktop. It can erase a
writable ATA or USB device, create the bootable FAT32 layout, install Stage 1,
Stage 2 and the kernel, and copy the selected User or Developer edition.

```bash
make iso-user
make EDITION=user run-iso
make run-installed
```

### Equipos antiguos (8–15 MB)

El kernel detecta automáticamente un perfil austero para equipos con entre 8 y
15 MB de RAM, como una configuración mínima del Dell Latitude C600. Conserva
el escritorio y las aplicaciones bajo demanda, pero usa VGA 640×480×4,
elimina el buffer frontal duplicado, no precarga wallpaper ni iconos, reduce
la frecuencia del temporizador a 100 Hz y deja red, USB hotplug, impresión,
salvapantallas y sonido de inicio desactivados. El sistema sigue arrancando
con 8 MB; las aplicaciones pesadas como NetSurf, Wine o Doom requieren más
RAM y conviene abrirlas sólo cuando sean necesarias.

Use `make reset-installer-target` to recreate the default blank 64 MiB test
disk. A 1.44 MiB floppy can be formatted from Setup, but uses FAT12; FAT32 is
not valid for that media and the complete operating system does not fit on it.

---

## Project Structure

```
boot/       Bootloader
kernel/     Kernel
gui/        Window system
programs/   Native applications
system/     Desktop components, services, screensavers, libraries and Control Panel
assets/     Icons and images
tools/      Build tools
```

Runtime layout inside the FAT32 system image:

```text
/SYSTEM/PROGRAMS/   Native applications (.o)
/SYSTEM/CORE/       Desktop core components
/SYSTEM/WIN32/      Win32 applications (.exe)
/SYSTEM/GRAPHICS.PAK Shared React95-compatible graphic resources
```

Native applications use the versioned public API documented in
[`docs/API.md`](docs/API.md). Kernel driver headers are not part of the app ABI.

---

## Roadmap

Current focus for version 0.6:

- Complete Ring 3 migration
- Continue expanding the versioned userspace API
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
- React95 (shared icon catalog; see THIRD_PARTY_LICENSES.md)
- OSDev Wiki
- James Molloy's Kernel Tutorial
- Bochs VBE documentation
- KolibriOS
