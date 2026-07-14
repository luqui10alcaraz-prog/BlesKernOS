# BlesKernOS 0.7

BlesKernOS is a lightweight 32-bit operating system written from scratch in x86 Assembly and C.


> BlesKernOS is still under development. Bugs and limited hardware compatibility are expected.

---

## Screenshots

<img width="799" height="599" alt="BlesKernOS desktop" src="https://github.com/user-attachments/assets/e7134b00-7bca-4d60-8192-2383515afa01" />
<img width="799" height="598" alt="BlesKernOS applications" src="https://github.com/user-attachments/assets/2a1cd55c-a47a-46b6-bca7-67945d70ed0f" />
<img width="802" height="600" alt="BlesKernOS Doom port" src="https://github.com/user-attachments/assets/38775fc8-bc0f-4db5-badc-7464a4f49fa9" />

---

## What's new in 0.7

- Native applications now run as external Ring 3 programs.
- Public API and SDK for creating BlesKernOS software.
- External drivers, libraries, services and screensavers.
- Improved Control Panel and Device Manager.
- USB UHCI and USB storage support.
- AC'97, Sound Blaster 16 and ESS Maestro 3 audio drivers.
- Startup sound and improved multimedia support.
- Recovery Console.
- Larger collection of shell commands and system tools.
- Experimental PE32 and Win32 compatibility.

---

## Main features

### System

- Custom bootloader and 32-bit protected-mode kernel.
- Preemptive multitasking and Ring 3 userspace.
- ELF program loader and system calls.
- ATA, ATAPI, floppy and USB storage.
- FAT12, FAT16, FAT32 and ISO 9660 filesystems.
- PCI, PS/2 keyboard and mouse, VESA graphics and VGA fallback.
- Loadable `.DVR` driver modules.

### Desktop

- Window manager, compositor, desktop icons and taskbar.
- File associations and configurable desktop.
- BMP and GIF image support.
- Control Panel modules for display, appearance, sound, devices, keyboard, mouse, date and system settings.
- External screensavers, including TinyGL pipes and bouncing balls.

### Applications

- File Browser
- Text Editor
- Calculator
- Calendar
- Paint
- Image Viewer
- Process Manager
- ScanDisk
- MidAmp MIDI Player
- Shell and RunBox
- TinyGL Gears demo
- Doom

### Win32 compatibility

BlesKernOS includes an experimental PE32 loader and partial implementations of several Win32 libraries. Small test programs, dialogs, menus, DLLs, threads and simple applications can run, but compatibility is still limited.

---

## SDK

Native applications can use the public BlesKernOS API through the SDK located in `sdk/`.

Documentation:

- [`docs/API.md`](docs/API.md)
- [`docs/DRIVERS.md`](docs/DRIVERS.md)

---

## Building

Requirements:

- NASM
- GCC with 32-bit support
- Python 3
- QEMU
- DOS/FAT tools

Build the system:

```bash
make
```

Run the ATA image:

```bash
qemu-system-i386 -drive file=build/bleskernos-ata.img,format=raw,if=ide -m 128M -device sb16 -serial stdio
```

Run the USB image with UHCI:

```bash
qemu-system-i386 -device piix3-usb-uhci,id=uhci -drive id=usbdisk,file=build/bleskernos-usb.img,format=raw,if=none -device usb-storage,drive=usbdisk,bus=uhci.0,bootindex=1 -m 128M -serial stdio
```

---

## Project structure

```text
boot/       Bootloader
kernel/     Kernel, drivers and Win32 subsystem
gui/        Desktop and window system
programs/   Native applications
system/     Commands, settings, services and screensavers
libs/       External libraries
sdk/        Public development kit
assets/     Icons, sounds and wallpapers
tools/      Build tools
```

---

## Current limitations

- No paging-based process isolation yet.
- Hardware support is still limited.
- USB support currently focuses on UHCI controllers.
- Win32 compatibility is experimental and incomplete.

---

## License

MIT License.

## Acknowledgments

- DoomGeneric
- TinyGL
- OSDev Wiki
- James Molloy's Kernel Tutorial
- Bochs VBE documentation
- KolibriOS