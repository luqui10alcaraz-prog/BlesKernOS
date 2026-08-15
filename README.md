BlesKernOS 0.8

BlesKernOS is a lightweight 32-bit x86 operating system written primarily from scratch in C and x86 Assembly.

It includes its own bootloader and protected-mode kernel, preemptive multitasking, native Ring 3 applications, a graphical desktop, FAT/ISO9660 storage, networking with HTTPS, loadable drivers, multimedia support, an SDK for native software, experimental Win16/Win32 compatibility, and software/hardware-assisted 3D graphics.

BlesKernOS is still under active development. Hardware support and compatibility layers are incomplete, and some features described below remain experimental.

Screenshots

<img width="799" height="599" alt="BlesKernOS desktop" src="https://github.com/user-attachments/assets/e7134b00-7bca-4d60-8192-2383515afa01" />
<img width="799" height="598" alt="BlesKernOS desktop" src="https://github.com/user-attachments/assets/2a1cd55c-a47a-46b6-bca7-67945d70ed0f" />
<img width="796" height="602" alt="BlesKernOS desktop" src="https://github.com/user-attachments/assets/559f631b-f4c6-4dc3-8289-92f81bbff0f5" />
<img width="799" height="604" alt="BlesKernOS desktop" src="https://github.com/user-attachments/assets/8fe6fcc9-f26d-4d3d-8e04-677d1d784510" />
<img width="802" height="602" alt="BlesKernOS desktop" src="https://github.com/user-attachments/assets/7512fe19-1ada-4eeb-9eda-c0d91d44f89f" />
<img width="799" height="600" alt="BlesKernOS desktop" src="https://github.com/user-attachments/assets/6f9c2add-6da9-4880-a63b-575e7b3ccf2c" />
<img width="796" height="603" alt="BlesKernOS desktop" src="https://github.com/user-attachments/assets/01e41ead-1915-4838-b1f8-db01a0f76a2e" />
<img width="802" height="600" alt="BlesKernOS desktop" src="https://github.com/user-attachments/assets/38775fc8-bc0f-4db5-badc-7464a4f49fa9" />

What's new in 0.8

SMP support for up to 16 logical processors, with automatic single-CPU fallback.

Per-CPU scheduling infrastructure, Local APIC timers, task affinity and load distribution for native Ring 3 applications.

IA-32 paging enabled at boot, CR0.WP, dynamic RAM-sized heap and stronger user-pointer validation.

Automatic low-memory compatibility profiles from 4 MB to 15 MB of RAM.

Loadable .DVR driver system with a versioned driver ABI.

Expanded graphics stack with VMware SVGA-II, VirtIO GPU, ATI Rage 128 and experimental 3D backends.

Mesa 3.5/OpenGL 1.2 port alongside TinyGL, with GFX3D acceleration where supported and software fallback.

IPv4 networking with DHCP, DNS, ICMP, TCP, HTTP and HTTPS.

TLS 1.2 using BearSSL with X.509 certificate validation and a CA bundle.

NetSurf browser port and network utilities including ping, curl, wget, ipconfig and netstat.

USB UHCI support, USB storage and an external USB class driver for HID, hubs and USB printers.

Expanded audio support including Sound Blaster 16, Intel AC'97 and ESS Maestro3-family hardware.

Print spooler and printer profiles for text, PostScript, PCL 5 and ESC/P-family output over file, LPT and USB printer transports.

Experimental Win16 NE execution and substantially expanded Win32/Windows 95 compatibility APIs.

User and Developer editions, installer media and SDK improvements.

Improved desktop, graphics resources, language catalogs, system utilities and performance monitoring.

Features

Kernel and system

Custom x86 bootloader with FAT32-capable Stage 2.

32-bit protected-mode kernel.

Preemptive multitasking.

Native Ring 3 userspace and system-call ABI.

ELF32 loader for native applications and loadable modules.

SMP for up to 16 logical CPUs.

ACPI MADT and Intel MP-table CPU discovery.

xAPIC startup using INIT/SIPI and Local APIC scheduler timers.

Per-CPU scheduler state, kernel stacks, TSS and x87 state.

Automatic uniprocessor fallback when SMP is unavailable.

IA-32 paging with PSE and write protection enabled.

Dynamic heap sized from the E820 memory map instead of a fixed 64 MB heap.

Per-process allocation tracking and cleanup on process exit.

Process fault containment for faults produced by native applications while crossing API/syscall boundaries.

Recovery Console.

Versioned public userspace API and SDK.

Freestanding C runtime used by native programs.

Paging is active in 0.8, but BlesKernOS still uses a shared identity-mapped address space. Full per-process virtual address-space isolation is not implemented yet.

Low-memory mode

BlesKernOS automatically selects a reduced profile according to detected physical RAM:

4 MB: minimal 320x200x8 desktop, two tasks and no Ring 3 applications.

5 MB: minimal profile with one application at a time.

6-7 MB: minimal profile with up to five tasks.

8-15 MB: reduced desktop profile, lower GUI refresh rate and disabled heavy subsystems.

More than 15 MB: normal feature set.

Low-memory profiles can automatically disable external drivers, network autoconfiguration, USB hotplug, printing, startup audio, screensavers, wallpaper, image icons and Win16/Win32 compatibility to preserve RAM.

Storage and filesystems

ATA / IDE.

ATAPI support.

FAT12 / FAT16 / FAT32.

ISO9660 CD-ROM filesystem.

Virtual filesystem and block-device layer.

USB Mass Storage.

Bootable ATA and USB images.

Text-mode installer capable of installing the system to writable ATA or USB media.

Loadable drivers

BlesKernOS can load resident ELF32 .DVR modules from /SYSTEM/DRIVERS.

Current driver modules include:

Sound Blaster 16.

Intel ICH-family AC'97.

ESS Maestro3 / Allegro-family audio.

CMOS/RTC.

ISO9660.

PS/2 mouse.

USB class support.

VESA.

VMware SVGA-II.

VirtIO GPU.

ATI Rage 128 / Rage Mobility.

Experimental ATI Rage 128 3D backend.

Experimental VMware SVGA3D backend.

Intel GMA9xx graphics support.

3Com EtherLink XL / 3c90x networking.

Realtek RTL8129 / RTL8139 networking.

Modular IPv4 network stack.

TLS 1.2 provider.

The boot-critical ATA, FAT/VFS, UHCI and USB-storage paths remain built into the kernel so the system can load external drivers from disk or USB.

Graphics

VGA fallback modes.

VESA linear framebuffer support.

VMware SVGA-II native 2D backend.

VirtIO GPU 2D backend.

ATI Rage 128 graphics support.

Intel GMA9xx graphics driver.

Dirty-rectangle presentation and accelerated rectangle operations on supported backends.

GPU-aware compositor infrastructure.

Hardware cursor support on selected drivers.

GFX3D backend abstraction for accelerated 3D rendering.

Software fallback when hardware acceleration is unavailable or an operation is unsupported.

3D and OpenGL

BlesKernOS 0.8 includes two 3D paths:

TinyGL, retained as a lightweight software renderer with optional GFX3D acceleration.

Mesa 3.5, ported as a freestanding OpenGL 1.2 implementation using the real Mesa TNL/OSMesa pipeline.

The Mesa integration can accelerate supported fixed-function rendering through GFX3D while retaining SWRAST fallback. The accelerated subset includes triangles, lines, points, depth testing, a single 2D texture unit, common filtering/wrapping modes, alpha blending, scissor, linear fog and polygon offset. Some classic OpenGL operations remain CPU-only.

Experimental GFX3D backends exist for VMware SVGA3D, VirtIO/VirGL and ATI Rage 128. Exact capabilities differ by driver and hardware.

Networking and Internet

3Com 3c90x and Realtek RTL8139 Ethernet drivers.

Ethernet and ARP.

IPv4.

ICMP / ping.

UDP internally.

DHCP client.

DNS resolver.

TCP client sockets for Ring 3 applications.

HTTP/1.1 client support.

HTTPS with TLS 1.2.

BearSSL 0.6 integration.

X.509 certificate-chain and hostname validation.

SNI support.

CA trust bundle.

Network commands including ipconfig, ping, curl, wget and netstat.

NetSurf browser port.

Current networking is intentionally small: IPv4 only, a single active NIC, client-oriented TCP and limited buffering. It is not intended to be a full modern TCP/IP implementation yet.

USB

UHCI USB 1.1 host controller support.

USB Mass Storage.

USB hotplug infrastructure.

USB core ABI for external class drivers.

HID keyboard/mouse class support.

USB hubs.

USB Printer Class exposed as USBPRN1 through USBPRN8.

Audio

Sound Blaster 16.

PC speaker fallback.

Intel ICH-family AC'97 PCM audio.

ESS Maestro3 / Allegro-family support.

MIDI playback through MidAmp.

Startup sound.

Printing

Ring 3 print API.

Persistent print spooler.

BPJ1 intermediate print-job format.

Queue retry, pause and cancellation states.

External .BPD printer profiles.

Text output.

PostScript output.

PCL 5 monochrome output.

ESC/P and basic ESC/P2 monochrome output.

File output for testing.

Parallel LPT transport.

USB Printer Class transport.

Desktop

Graphical desktop environment.

Window manager and compositor.

Deskbar / taskbar.

Desktop icons and shortcuts.

Configurable desktop through INI files.

Runtime resolution and display configuration infrastructure.

BMP, GIF, JPEG and SVG image support in the graphics stack.

Packed shared graphics resources through GRAPHICS.PAK.

Screensavers, including TinyGL-based screensavers.

Global text clipboard and Clipboard Viewer.

File-selection dialogs available to native applications.

Multiple language catalogs, including Spanish, English and Italian resources.

Native applications and utilities

The source tree includes native BlesKernOS applications and system tools such as:

File Browser.

Text Editor.

Calculator.

Calendar.

Paint.

Image Viewer.

Control Panel.

Device Manager.

Process Manager.

Performance Monitor.

MidAmp MIDI Player.

NetSurf browser.

Shell and Run box.

Help Center.

Find Files.

Archive Manager.

Disk Tools and ScanDisk.

Network Status.

Clipboard Viewer.

Setup / installer utilities.

3D/OpenGL test programs such as Gears.

Doom port support in the system resources/build tree.

The command-line environment also contains a larger collection of Unix/DOS-style utilities for files, processes, hardware inspection, networking, checksums, archives and system diagnostics.

Win16 compatibility — experimental

BlesKernOS contains an experimental NE/Win16 compatibility layer influenced by the architecture and API metadata of Wine.

Implemented foundations include:

NE executable loading and relocation.

16-bit GDT code/data selectors.

16:16 Ring 3 task startup.

KERNEL/USER/GDI relay infrastructure.

Basic local/global memory APIs.

Several KERNEL string/task helpers.

Basic MessageBox integration.

This is not a complete Windows 3.x or Wine implementation. Complex applications can still stop on missing USER/GDI, DLL, resource, callback or multimedia APIs.

Win32 / Windows 95 compatibility — experimental

The PE32 compatibility layer includes an expanding set of Windows 95/98-era APIs, including portions of:

KERNEL32.

USER32.

GDI32.

COMCTL32.

Registry APIs.

Process and synchronization APIs.

Virtual memory bookkeeping.

File and file-mapping APIs.

DIB and bitmap graphics.

WinSock / networking-related compatibility work.

Additional compatibility libraries assembled into the project's Wine compatibility library.

The compatibility layer is partial. It does not provide full Windows memory isolation or complete Win32 API coverage.

Building

Requirements

NASM

GCC with 32-bit x86 support

Binutils

Python 3

QEMU for testing

Some Win32 test targets additionally require a MinGW i686 cross-compiler.

Build the normal User edition:

make user

Build the Developer edition:

make developer

Build both editions:

make editions

Run the User edition:

make EDITION=user run

The main ATA images are written to:

build/bleskernos-ata-user.img
build/bleskernos-ata-developer.img

The build system can also create USB and VMware images. See docs/EDITIONS_AND_UTILITIES.md.

Installer CD

The installer ISO boots a text-mode installer instead of the desktop. It can prepare a writable ATA or USB device, create the FAT32 system layout, install the boot stages and kernel, and copy the selected edition.

make iso-user
make EDITION=user run-iso
make run-installed

Use make reset-installer-target to recreate the default blank test disk.

Graphics test targets

The source contains QEMU targets for several graphics backends, including VMware SVGA and VirtIO GPU/VirGL. See the relevant documents under docs/ before testing experimental 3D acceleration.

SDK

Native software uses the public versioned BlesKernOS userspace API rather than private kernel structures.

Important SDK components include:

sdk/include/                 Public headers
build/sdk/                   Built SDK libraries
build/sdk/libblesk_tinygl.a  TinyGL integration
build/sdk/libblesk_mesa.a    Mesa 3.5 / OpenGL integration

See docs/API.md and sdk/README.md.

Kernel driver headers are not part of the native application ABI.

Project structure

boot/       Bootloader and boot stages
kernel/     Kernel, compatibility layers and built-in subsystems
gui/        Window system, compositor and graphics helpers
programs/   Native applications and ports
system/     Desktop components, commands, services, drivers, languages and Control Panel
libs/       TinyGL, Mesa 3.5, BearSSL and system libraries
sdk/        Public application SDK
docs/       Architecture and subsystem documentation
assets/     Icons, wallpapers, sounds and shared graphics resources
tools/      Image/build/resource generation tools
tests/      Kernel, userspace, hardware and compatibility tests

Runtime layout inside the FAT32 system image includes:

/SYSTEM/PROGRAMS/      Native applications
/SYSTEM/CORE/          Desktop core components
/SYSTEM/DRIVERS/       Loadable .DVR drivers
/SYSTEM/LIBS/          Runtime/static libraries
/SYSTEM/WIN32/         Win32 applications and compatibility files
/SYSTEM/PRINTERS/      Printer profiles
/SYSTEM/GRAPHICS.PAK   Shared graphical resources

Technical documentation

The repository contains more detailed documentation for individual subsystems, including:

docs/API.md — native userspace API.

docs/SMP.md — multiprocessor architecture.

docs/SMP_LOCKING.md — SMP locking model.

docs/PAGING.md — paging and process memory.

docs/LOW_MEMORY_MODE.md — 4-15 MB compatibility profiles.

docs/DRIVERS.md — loadable driver ABI.

docs/NETWORKING.md — IPv4, sockets, HTTP and TLS.

docs/printing.md — print subsystem.

docs/mesa35.md — Mesa 3.5/GFX3D integration.

docs/tinygl-gfx3d.md — TinyGL hardware acceleration path.

docs/VMWARE_SVGA_II.md — VMware SVGA-II.

docs/virtio_gpu.md — VirtIO GPU and VirGL.

docs/WIN16_PORT.md — experimental Win16 compatibility.

docs/WIN32_PHASE1_COMPAT.md — Win32 compatibility work.

Current limitations and future work

The 0.8 tree is considerably more capable than previous releases, but several architectural areas remain unfinished:

Per-process page tables and true virtual-memory isolation.

IOAPIC-based interrupt routing and more complete SMP IRQ distribution.

Broader hardware validation of experimental GPU drivers.

More complete OpenGL hardware acceleration coverage.

More complete TCP/IP behavior and server-side sockets.

EHCI/class-driver unification in the USB stack.

More complete Win16 and Win32 API compatibility.

Continued GUI, driver, filesystem and performance stabilization.

License

BlesKernOS is distributed under the MIT License unless a source file or third-party component states otherwise.

Third-party components retain their own licenses and notices. See THIRD_PARTY_LICENSES.md and the license files included with individual libraries.

Acknowledgments and third-party components

BlesKernOS uses, ports, adapts or references several external projects and public technical resources. These include:

DoomGeneric

TinyGL

Mesa 3.5

BearSSL

Wine documentation/source architecture for compatibility work where noted

React95 shared graphic resources where documented

OSDev Wiki

James Molloy's Kernel Tutorial

Bochs VBE documentation

KolibriOS and other open operating-system projects used as technical references

See THIRD_PARTY_LICENSES.md for licensing details.