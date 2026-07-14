#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from build_fat_floppy import bk_iconpak_try_prepare


SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent
ATA_SIZE_KB = 64 * 1024
SECTOR_SIZE = 512
FAT32_CODE_OFFSET = 90


def run(cmd):
    subprocess.check_call([str(part) for part in cmd])


def load(path, label):
    if not path:
        return None
    try:
        data = Path(path).read_bytes()
        print(f"{label} size: {len(data)} bytes", file=sys.stderr)
        return data
    except Exception as exc:
        print(f"Warning: No se pudo leer {path}: {exc}", file=sys.stderr)
        return None


def crlf(data):
    return data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")


def write_payload(tmpdir, name, data):
    path = tmpdir / name
    path.write_bytes(data)
    return path


def is_wallpaper_file(path):
    suffix = path.suffix.upper()
    return suffix in {".BMP", ".PNG", ".JPG", ".JPE", ".JPN", ".JPNG"}


def mmd(image, path):
    run(["mmd", "-i", image, path])


def mcopy(image, src, dst):
    run(["mcopy", "-o", "-i", image, src, dst])


def patch_boot_sector(image, boot_sector, sector):
    boot = Path(boot_sector).read_bytes()
    if len(boot) != SECTOR_SIZE:
        raise ValueError(f"boot sector must be 512 bytes: {boot_sector}")

    with image.open("r+b") as handle:
        handle.seek(sector * SECTOR_SIZE)
        current = bytearray(handle.read(SECTOR_SIZE))
        current[0:3] = boot[0:3]
        current[FAT32_CODE_OFFSET:SECTOR_SIZE] = boot[FAT32_CODE_OFFSET:SECTOR_SIZE]
        handle.seek(sector * SECTOR_SIZE)
        handle.write(current)


def read_sector(image, sector):
    with image.open("rb") as handle:
        handle.seek(sector * SECTOR_SIZE)
        return handle.read(SECTOR_SIZE)


def write_sector(image, sector, data):
    if len(data) != SECTOR_SIZE:
        raise ValueError("sector write must be exactly 512 bytes")
    with image.open("r+b") as handle:
        handle.seek(sector * SECTOR_SIZE)
        handle.write(data)


def set_fat32_info_sectors(image, fsinfo_sector, backup_boot_sector):
    for sector in (0, backup_boot_sector):
        boot = bytearray(read_sector(image, sector))
        boot[48:50] = fsinfo_sector.to_bytes(2, "little")
        boot[50:52] = backup_boot_sector.to_bytes(2, "little")
        write_sector(image, sector, bytes(boot))


def write_raw_sector_data(image, sector, data, max_sectors, label):
    if len(data) > max_sectors * SECTOR_SIZE:
        raise ValueError(f"{label} ocupa mas de {max_sectors} sectores")
    with image.open("r+b") as handle:
        handle.seek(sector * SECTOR_SIZE)
        handle.write(data)


def zero_sector_range(image, first_sector, sector_count):
    with image.open("r+b") as handle:
        handle.seek(first_sector * SECTOR_SIZE)
        handle.write(b"\x00" * (sector_count * SECTOR_SIZE))


def main():
    bk_iconpak_try_prepare(sys.argv)

    if len(sys.argv) < 2:
        print(
            "usage: build_fat32_ata.py <image> [shell.o] [filebrowser.o] "
            "[texteditor.o] [calculator.o] [midamp.o] [processmanager.o] "
            "[calendar.o] [desktop.ini] [icons_dir] [about.gif] "
            "[associations.ini] [reserved_sectors] [screensaverd.o] "
            "[ss_logo.o] [ss_pipes.o] [screensv.ini] [boot.bin] [stage2.bin] "
            "[kernel.bin] [kernel_sectors] [tinygl.a] [libc.a] "
            "[control.o] [appear.cpl] [display.cpl] [sound.cpl] "
            "[datetime.cpl] [mouse.cpl] [keyboard.cpl] [system.cpl] "
            "[devmgr.cpl] [ss_balls.o]",
            file=sys.stderr,
        )
        return 1

    image = Path(sys.argv[1])
    shell_path = sys.argv[2] if len(sys.argv) > 2 else None
    filebrowser_path = sys.argv[3] if len(sys.argv) > 3 else None
    texteditor_path = sys.argv[4] if len(sys.argv) > 4 else None
    calculator_path = sys.argv[5] if len(sys.argv) > 5 else None
    midamp_path = sys.argv[6] if len(sys.argv) > 6 else None
    processmanager_path = sys.argv[7] if len(sys.argv) > 7 else None
    calendar_path = sys.argv[8] if len(sys.argv) > 8 else None
    desktop_path = sys.argv[9] if len(sys.argv) > 9 else None
    icons_path = Path(sys.argv[10]) if len(sys.argv) > 10 and sys.argv[10] else None
    about_gif_path = (
        Path(sys.argv[11]) if len(sys.argv) > 11 and sys.argv[11]
        else ROOT_DIR / "assets" / "gif" / "abount.gif"
    )
    associations_path = sys.argv[12] if len(sys.argv) > 12 else None
    screensaverd_path = sys.argv[14] if len(sys.argv) > 14 else None
    ss_logo_path = sys.argv[15] if len(sys.argv) > 15 else None
    ss_pipes_path = sys.argv[16] if len(sys.argv) > 16 else None
    reserved_arg = int(sys.argv[13]) if len(sys.argv) > 13 else 1033
    screensv_path = sys.argv[17] if len(sys.argv) > 17 else None
    boot_sector_path = Path(sys.argv[18]) if len(sys.argv) > 18 and sys.argv[18] else None
    stage2_path = Path(sys.argv[19]) if len(sys.argv) > 19 and sys.argv[19] else None
    kernel_path = Path(sys.argv[20]) if len(sys.argv) > 20 and sys.argv[20] else None
    kernel_reserved_sectors = int(sys.argv[21]) if len(sys.argv) > 21 else 1024
    tinygl_path = sys.argv[22] if len(sys.argv) > 22 else None
    libc_path = sys.argv[23] if len(sys.argv) > 23 else None
    control_paths = [sys.argv[i] if len(sys.argv) > i else None
                     for i in range(24, 33)]
    ss_balls_path = sys.argv[33] if len(sys.argv) > 33 else None
    stage2_sectors = 8
    stage2_lba = 1
    kernel_lba = 9
    fsinfo_sector = reserved_arg
    backup_boot_sector = reserved_arg + 1
    backup_fsinfo_sector = reserved_arg + 2
    reserved_sectors = reserved_arg + 3

    image.parent.mkdir(parents=True, exist_ok=True)
    if image.exists():
        image.unlink()
    run([
        "mkfs.fat", "-C", "-F", "32", "-n", "BLESKERNOS",
        "-R", str(reserved_sectors), "-b", str(backup_boot_sector),
        image, str(ATA_SIZE_KB),
    ])

    fsinfo = read_sector(image, 1)
    write_sector(image, fsinfo_sector, fsinfo)
    write_sector(image, backup_fsinfo_sector, fsinfo)
    set_fat32_info_sectors(image, fsinfo_sector, backup_boot_sector)

    if boot_sector_path and stage2_path and kernel_path:
        zero_sector_range(image, stage2_lba, fsinfo_sector - stage2_lba)
        write_raw_sector_data(
            image, stage2_lba, stage2_path.read_bytes(), stage2_sectors,
            "stage2",
        )
        write_raw_sector_data(
            image, kernel_lba, kernel_path.read_bytes(), kernel_reserved_sectors,
            "kernel",
        )
        patch_boot_sector(image, boot_sector_path, 0)
        patch_boot_sector(image, boot_sector_path, backup_boot_sector)

    mmd(image, "::/DOCS")
    mmd(image, "::/MISC")
    mmd(image, "::/ICONS")
    mmd(image, "::/SYSTEM")
    mmd(image, "::/SYSTEM/PROGRAMS")
    mmd(image, "::/SYSTEM/COMMANDS")
    mmd(image, "::/SYSTEM/CORE")
    mmd(image, "::/SYSTEM/WIN32")
    mmd(image, "::/SYSTEM/USER")
    mmd(image, "::/SYSTEM/USER/CONFIG")
    mmd(image, "::/SYSTEM/LIBS")
    mmd(image, "::/SYSTEM/LIBS/TINYGL")
    mmd(image, "::/SYSTEM/LIBS/LIBC")
    mmd(image, "::/SYSTEM/LIBS/WINE")
    mmd(image, "::/SYSTEM/SERVICES")
    mmd(image, "::/SYSTEM/SCREENS")
    mmd(image, "::/SYSTEM/CONTROL")
    mmd(image, "::/SYSTEM/DRIVERS")
    mmd(image, "::/SYSTEM/SOUNDS")
    mmd(image, "::/SYSTEM/WALLPAPR")

    with tempfile.TemporaryDirectory(prefix="bleskernos-ata-") as tmp:
        tmpdir = Path(tmp)

        text_files = {
            "::/README.TXT": b"BlesKernOS FAT32 ATA disk\r\nApps: /SYSTEM/PROGRAMS\r\nCommands: /SYSTEM/COMMANDS\r\nCore: /SYSTEM/CORE\r\nWin32: /SYSTEM/WIN32\r\n",
            "::/DOCS/README.TXT": b"Documentos del sistema.\r\nEste disco ATA usa FAT32.\r\n",
            "::/DOCS/ROADMAP.TXT": b"Roadmap\r\n- Lanzador real\r\n- Programas externos\r\n",
            "::/MISC/PALETTE.TXT": b"Paleta GUI\r\n- celeste\r\n- gris 90s\r\n- verde suave\r\n",
            "::/MISC/THEMES.TXT": b"Themes\r\nClassic 90s\r\nHaiku Soft\r\n",
            "::/SYSTEM/USER/CONFIG/DATETIME.INI": b"format=24\r\ntimezone=0\r\n",
            "::/SYSTEM/USER/CONFIG/MOUSE.INI": b"sensitivity=3\r\ntrail=0\r\n",
            "::/SYSTEM/USER/CONFIG/SOUND.INI": (
                b"[SOUND]\r\nStartupEnabled=1\r\n"
                b"StartupSound=/SYSTEM/SOUNDS/START.WAV\r\n"
            ),
        }

        if desktop_path:
            desktop = load(desktop_path, "Desktop INI")
            if desktop is not None:
                text_files["::/SYSTEM/USER/CONFIG/DESKTOP.INI"] = crlf(desktop)

        associations = load(associations_path, "Associations INI")
        if associations is not None:
            text_files["::/ASSOC.INI"] = crlf(associations)

        screensv = (
            b"enabled=1\r\n"
            b"timeout=300\r\n"
            b"path=/SYSTEM/SCREENS/SSLOGO.SCV\r\n"
        )
        if screensv_path:
            data = load(screensv_path, "ScreenSaver INI")
            if data is not None:
                screensv = crlf(data)
        text_files["::/SYSTEM/USER/CONFIG/SCREENSV.INI"] = screensv

        for index, (dst, data) in enumerate(text_files.items()):
            src = write_payload(tmpdir, f"text{index}.tmp", data)
            mcopy(image, src, dst)

        native_programs = [
            ("shell.o", shell_path, "Shell"),
            ("about.o", ROOT_DIR / "build" / "programs" / "about.o", "About"),
            ("runbox.o", ROOT_DIR / "build" / "programs" / "runbox.o", "Runbox"),
            ("file.o", filebrowser_path, "Filebrowser"),
            ("texteditor.o", texteditor_path, "Texteditor"),
            ("calculator.o", calculator_path, "Calculator"),
            ("midamp.o", midamp_path, "Midamp"),
            ("processmanager.o", processmanager_path, "Processmanager"),
            ("calendar.o", calendar_path, "Calendar"),
            ("gears.o", ROOT_DIR / "build" / "programs" / "gears.o", "Gears"),
            ("paint.o", ROOT_DIR / "build" / "programs" / "paint.o", "Paint"),
            ("imageviewer.o", ROOT_DIR / "build" / "programs" / "imageviewer.o", "Imageviewer"),
            ("games.o", ROOT_DIR / "build" / "programs" / "games.o", "Games"),
            ("apitest.o", ROOT_DIR / "build" / "programs" / "apitest.o", "APITest"),
            ("scandisk.o", ROOT_DIR / "build" / "programs" / "scandisk.o", "ScanDisk"),
            ("phase2.o", ROOT_DIR / "build" / "programs" / "phase2.o", "Phase II syscall test"),
            ("ring3proxy.o", ROOT_DIR / "build" / "programs" / "ring3proxy.o", "Ring 3 API proxy self-test"),
            ("wine.o", ROOT_DIR / "build" / "programs" / "wine.o", "Wine PE launcher"),
        ]
        win32_programs = [
            ("HELLO.EXE", ROOT_DIR / "build" / "win32" / "HELLO.EXE", "Win32 HELLO.EXE"),
            ("NOTEPAD.EXE", ROOT_DIR / "build" / "win32" / "NOTEPAD.EXE", "Win32 NOTEPAD.EXE"),
            ("MSGBOX.EXE", ROOT_DIR / "build" / "win32" / "MSGBOX.EXE", "Win32 MSGBOX.EXE"),
            ("DYNLOAD.EXE", ROOT_DIR / "build" / "win32" / "DYNLOAD.EXE", "Win32 DYNLOAD.EXE"),
            ("DLLTEST.EXE", ROOT_DIR / "build" / "win32" / "DLLTEST.EXE", "Win32 DLLTEST.EXE"),
            ("TLSTEST.EXE", ROOT_DIR / "build" / "win32" / "TLSTEST.EXE", "Win32 TLSTEST.EXE"),
            ("THREADTEST.EXE", ROOT_DIR / "build" / "win32" / "THREADTEST.EXE", "Win32 THREADTEST.EXE"),
            ("SYNCTEST.EXE", ROOT_DIR / "build" / "win32" / "SYNCTEST.EXE", "Win32 SYNCTEST.EXE"),
            ("RESOURCETEST.EXE", ROOT_DIR / "build" / "win32" / "RESOURCETEST.EXE", "Win32 RESOURCETEST.EXE"),
            ("MENUTEST.EXE", ROOT_DIR / "build" / "win32" / "MENUTEST.EXE", "Win32 MENUTEST.EXE"),
            ("DIALOGTEST.EXE", ROOT_DIR / "build" / "win32" / "DIALOGTEST.EXE", "Win32 DIALOGTEST.EXE"),
            ("SEHTEST.EXE", ROOT_DIR / "build" / "win32" / "SEHTEST.EXE", "Win32 SEHTEST.EXE"),
            ("WCCOMPAT.EXE", ROOT_DIR / "build" / "win32" / "WINECALC_COMPAT.EXE", "Win32 WineCalc compatibility test"),
            ("EDITTEST.EXE", ROOT_DIR / "build" / "win32" / "EDITTEST.EXE", "Win32 multiline EDIT test"),
        ]
        for dst_name, src_path, label in native_programs:
            data = load(src_path, label)
            if data is not None:
                src = write_payload(tmpdir, dst_name, data)
                mcopy(image, src, f"::/SYSTEM/PROGRAMS/{dst_name}")

        command_names = (
            "help about uname hostname uptime date time shutdown reboot sleep "
            "fdisk format mount unmount label checkdisk fsinfo backup dir ls copy move "
            "delete mkdir rmdir rename touch tree find attrib chmod type more cat "
            "diff ps kill tasklist taskkill top nice pci usb lspci lsusb "
            "cpuinfo mem soundtest ipconfig ping netstat ftp wget curl compile link "
            "objdump nm hexdump strings calc hexedit compress extract "
            "checksum benchmark start"
        ).split()
        for command_name in command_names:
            filename = f"{command_name}.o"
            command_object = ROOT_DIR / "build" / "system" / "commands" / filename
            command_data = load(command_object, f"Ring 3 command {command_name}")
            if command_data is not None:
                src = write_payload(tmpdir, filename, command_data)
                mcopy(image, src, f"::/SYSTEM/COMMANDS/{filename}")
        for dst_name, src_path, label in win32_programs:
            data = load(src_path, label)
            if data is not None:
                src = write_payload(tmpdir, dst_name, data)
                mcopy(image, src, f"::/SYSTEM/WIN32/{dst_name}")

        wallpapers_dir = ROOT_DIR / "assets" / "wallpapers"
        if wallpapers_dir.is_dir():
            for wallpaper_path in sorted(wallpapers_dir.iterdir()):
                if not wallpaper_path.is_file() or not is_wallpaper_file(wallpaper_path):
                    continue
                mcopy(image, wallpaper_path,
                      f"::/SYSTEM/WALLPAPR/{wallpaper_path.name}")

        system_files = [
            ("::/SYSTEM/CORE/deskbar.o", ROOT_DIR / "build" / "system" / "desktop" / "deskbar.o", "Deskbar core"),
            ("::/SYSTEM/CORE/deskmanager.o", ROOT_DIR / "build" / "system" / "desktop" / "deskmanager.o", "Desktop manager core"),
            ("::/SYSTEM/CORE/launcher.o", ROOT_DIR / "build" / "programs" / "launcher.o", "Program launcher core"),
            ("::/SYSTEM/DRIVERS/AC97.DVR", ROOT_DIR / "build" / "system" / "drivers" / "AC97.DVR", "ICH-compatible AC97 driver"),
            ("::/SYSTEM/DRIVERS/MAESTRO3.DVR", ROOT_DIR / "build" / "system" / "drivers" / "MAESTRO3.DVR", "ESS Allegro/Maestro3 DSP driver"),
            ("::/SYSTEM/DRIVERS/SB16.DVR", ROOT_DIR / "build" / "system" / "drivers" / "SB16.DVR", "Sound Blaster 16 driver"),
            ("::/SYSTEM/DRIVERS/CMOSRTC.DVR", ROOT_DIR / "build" / "system" / "drivers" / "CMOSRTC.DVR", "CMOS RTC driver"),
            ("::/SYSTEM/DRIVERS/ISO9660.DVR", ROOT_DIR / "build" / "system" / "drivers" / "ISO9660.DVR", "ISO9660 driver"),
            ("::/SYSTEM/DRIVERS/PS2MOUSE.DVR", ROOT_DIR / "build" / "system" / "drivers" / "PS2MOUSE.DVR", "PS/2 mouse driver"),
            ("::/SYSTEM/DRIVERS/VESA.DVR", ROOT_DIR / "build" / "system" / "drivers" / "VESA.DVR", "VESA framebuffer driver"),
            ("::/SYSTEM/SERVICES/SCREENSV.O", screensaverd_path, "ScreenSaver daemon"),
            ("::/SYSTEM/SCREENS/SSLOGO.SCV", ss_logo_path, "ScreenSaver logo"),
            ("::/SYSTEM/SCREENS/SSPIPES.SCV", ss_pipes_path, "ScreenSaver pipes"),
            ("::/SYSTEM/SCREENS/SSBALLS.SCV", ss_balls_path, "ScreenSaver balls"),
            ("::/SYSTEM/LIBS/TINYGL/TINYGL.A", tinygl_path, "TinyGL library"),
            ("::/SYSTEM/LIBS/LIBC/LIBC.A", libc_path, "C library"),
            ("::/SYSTEM/LIBS/WINE/TESTDLL.DLL", ROOT_DIR / "build" / "win32" / "TESTDLL.DLL", "Win32 test DLL"),
        ]
        control_names = [
            ("CONTROL.O", "Control Panel"),
            ("APPEAR.CPL", "Appearance CPL"),
            ("DISPLAY.CPL", "Display CPL"),
            ("SOUND.CPL", "Sound CPL"),
            ("DATETIME.CPL", "Date and Time CPL"),
            ("MOUSE.CPL", "Mouse CPL"),
            ("KEYBOARD.CPL", "Keyboard CPL"),
            ("SYSTEM.CPL", "System CPL"),
            ("DEVMGR.CPL", "Device Manager CPL"),
        ]
        for (name, label), src_path in zip(control_names, control_paths):
            system_files.append((f"::/SYSTEM/CONTROL/{name}", src_path, label))
        for dst, src_path, label in system_files:
            data = load(src_path, label)
            if data is not None:
                src = write_payload(tmpdir, Path(dst).name, data)
                mcopy(image, src, dst)

        startup_wav = ROOT_DIR / "assets" / "sounds" / "startup.wav"
        startup_data = load(startup_wav, "Startup WAV")
        if startup_data is not None:
            src = write_payload(tmpdir, "START.WAV", startup_data)
            mcopy(image, src, "::/SYSTEM/SOUNDS/START.WAV")

        skin_dir = ROOT_DIR / "programs" / "winmap"
        root_assets = [
            ("MIDHDR.GIF", skin_dir / "hdr.gif", "Midamp HDR"),
            ("MIDBTN.GIF", skin_dir / "buttons.gif", "Midamp BTN"),
            ("MIDBOT.GIF", skin_dir / "bottom.gif", "Midamp BOT"),
            ("ABOUNT.GIF", about_gif_path, "About GIF"),
        ]
        for dst_name, src_path, label in root_assets:
            data = load(src_path, label)
            if data is not None:
                src = write_payload(tmpdir, dst_name, data)
                mcopy(image, src, f"::/{dst_name}")

        if icons_path and icons_path.is_dir():
            for src_path in sorted(icons_path.iterdir()):
                if src_path.is_file():
                    mcopy(image, src_path, f"::/ICONS/{src_path.name.upper()}")

    print(f"[ATA] FAT32 image: {image} ({ATA_SIZE_KB // 1024} MiB)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
