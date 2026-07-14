#!/usr/bin/env python3
import struct
import sys
import os


# --- BlesKernOS ICONS.PAK hook ---
def bk_iconpak_try_prepare(argv):
    # Crea assets/icons/ICONS.PAK automáticamente antes de poblar la imagen FAT.
    # Por defecto reemplaza el directorio de iconos por build/icons_pak_only,
    # para que el disquete reciba solo ICONS.PAK y no todos los BMP sueltos.
    #
    # Para dejar también los BMP sueltos:
    #     BK_ICON_PAK_ONLY=0 make run
    try:
        import os
        import shutil
        import subprocess
        import sys
        from pathlib import Path

        root = Path.cwd()
        default_pak_only = "0" if Path(argv[0]).name == "build_fat_floppy.py" else "1"
        pak_only = os.environ.get("BK_ICON_PAK_ONLY", default_pak_only) != "0"

        for i, arg in enumerate(list(argv)):
            p = Path(arg)
            if not p.exists() or not p.is_dir():
                continue
            if p.name.lower() != "icons":
                continue

            out_pak = p / "ICONS.PAK"
            tool = root / "tools" / "build_icons_pak.py"

            if tool.exists():
                subprocess.check_call([sys.executable, str(tool), str(p), str(out_pak)])
            else:
                print("[ICONPAK] tools/build_icons_pak.py no existe; no se generó ICONS.PAK")
                return

            if pak_only:
                tmp = root / "build" / "icons_pak_only"
                tmp.mkdir(parents=True, exist_ok=True)
                shutil.copy2(out_pak, tmp / "ICONS.PAK")
                argv[i] = str(tmp)

            return
    except Exception as exc:
        print("[ICONPAK] warning:", exc)
# --- end ICONS.PAK hook ---


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

SECTOR_SIZE = 512
TOTAL_SECTORS = 2880
DEFAULT_RESERVED_SECTORS = 517
RESERVED_SECTORS = DEFAULT_RESERVED_SECTORS
FAT_COUNT = 2
FAT_SECTORS = 9
ROOT_ENTRIES = 224
ROOT_DIR_SECTORS = (ROOT_ENTRIES * 32 + SECTOR_SIZE - 1) // SECTOR_SIZE
DATA_START_SECTOR = RESERVED_SECTORS + FAT_COUNT * FAT_SECTORS + ROOT_DIR_SECTORS
END_OF_CHAIN = 0x0FFF


def short_name(name):
    upper = name.upper()
    if "." in upper:
        base, ext = upper.split(".", 1)
    else:
        base, ext = upper, ""
    base = base[:8].ljust(8)
    ext = ext[:3].ljust(3)
    return (base + ext).encode("ascii")


def dir_entry(name, attr, cluster, size):
    entry = bytearray(32)
    entry[0:11] = short_name(name)
    entry[11] = attr
    struct.pack_into("<H", entry, 26, cluster & 0xFFFF)
    struct.pack_into("<I", entry, 28, size)
    return bytes(entry)


def fat12_set(fat, cluster, value):
    offset = cluster + (cluster // 2)
    value &= 0x0FFF
    if cluster & 1:
        fat[offset] = (fat[offset] & 0x0F) | ((value << 4) & 0xF0)
        fat[offset + 1] = (value >> 4) & 0xFF
    else:
        fat[offset] = value & 0xFF
        fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F)


def cluster_offset(cluster):
    sector = DATA_START_SECTOR + (cluster - 2)
    return sector * SECTOR_SIZE


def write_cluster(image, cluster, payload):
    if len(payload) > SECTOR_SIZE:
        raise ValueError(f"cluster {cluster} overflow")
    start = cluster_offset(cluster)
    if start + SECTOR_SIZE > len(image):
        raise ValueError(f"cluster {cluster} queda fuera de la imagen FAT12")
    image[start:start + SECTOR_SIZE] = payload.ljust(SECTOR_SIZE, b"\x00")


def write_file_clusters(image, fat, start_cluster, payload):
    """Write a file spanning multiple clusters and update FAT chain."""
    if not payload:
        return start_cluster
    
    cluster = start_cluster
    offset = 0
    payload_size = len(payload)
    
    while offset < payload_size:
        chunk_size = min(SECTOR_SIZE, payload_size - offset)
        chunk = payload[offset:offset + chunk_size]
        write_cluster(image, cluster, chunk)
        offset += chunk_size
        
        # Update FAT
        if offset >= payload_size:
            # Last cluster
            fat12_set(fat, cluster, END_OF_CHAIN)
        else:
            # Point to next cluster
            next_cluster = cluster + 1
            fat12_set(fat, cluster, next_cluster)
            cluster = next_cluster
    
    return start_cluster


def cluster_count(payload):
    return (len(payload) + SECTOR_SIZE - 1) // SECTOR_SIZE


def omit_floppy_payload(label, payload):
    if payload:
        print(f"[FAT12] Omitiendo {label}: {len(payload)} bytes no caben en el floppy", file=sys.stderr)
    return b""


def build_directory(entries):
    payload = bytearray(SECTOR_SIZE)
    cursor = 0
    for entry in entries:
        payload[cursor:cursor + 32] = entry
        cursor += 32
    return bytes(payload)


def script_path(*parts):
    return os.path.normpath(os.path.join(SCRIPT_DIR, *parts))


def is_wallpaper_file(name):
    upper = name.upper()
    return (upper.endswith(".BMP") or upper.endswith(".PNG") or
            upper.endswith(".JPG") or upper.endswith(".JPE") or
            upper.endswith(".JPN") or upper.endswith(".JPNG"))


def main():
    bk_iconpak_try_prepare(sys.argv)
    global RESERVED_SECTORS, DATA_START_SECTOR

    if len(sys.argv) < 2:
        print(
            "usage: build_fat_floppy.py <image> [shell.o] [filebrowser.o] "
            "[texteditor.o] [calculator.o] [midamp.o] [processmanager.o] [calendar.o] [desktop.ini] "
            "[icons_dir] [about.gif] [associations.ini] [reserved_sectors] "
            "[screensaverd.o] [ss_logo.o] [ss_pipes.o] [screensv.ini] "
            "[tinygl.a] [libc.a] [control.o] [appear.cpl] [display.cpl] "
            "[sound.cpl] [datetime.cpl] [mouse.cpl] [keyboard.cpl] "
            "[system.cpl] [devmgr.cpl] [ss_balls.o]",
            file=sys.stderr,
        )
        return 1

    image_path = sys.argv[1]
    shell_path = sys.argv[2] if len(sys.argv) > 2 else None
    filebrowser_path = sys.argv[3] if len(sys.argv) > 3 else None
    texteditor_path = sys.argv[4] if len(sys.argv) > 4 else None
    calculator_path = sys.argv[5] if len(sys.argv) > 5 else None
    midamp_path = sys.argv[6] if len(sys.argv) > 6 else None
    processmanager_path = sys.argv[7] if len(sys.argv) > 7 else None
    calendar_path = sys.argv[8] if len(sys.argv) > 8 else None
    desktop_path = sys.argv[9] if len(sys.argv) > 9 else None
    icons_path = sys.argv[10] if len(sys.argv) > 10 else None
    about_gif_path = sys.argv[11] if len(sys.argv) > 11 else script_path("..", "assets", "gif", "abount.gif")
    associations_path = sys.argv[12] if len(sys.argv) > 12 else None
    RESERVED_SECTORS = int(sys.argv[13]) if len(sys.argv) > 13 else DEFAULT_RESERVED_SECTORS
    screensaverd_path = sys.argv[14] if len(sys.argv) > 14 else None
    ss_logo_path = sys.argv[15] if len(sys.argv) > 15 else None
    ss_pipes_path = sys.argv[16] if len(sys.argv) > 16 else None
    screensv_path = sys.argv[17] if len(sys.argv) > 17 else None
    tinygl_path = sys.argv[18] if len(sys.argv) > 18 else None
    libc_path = sys.argv[19] if len(sys.argv) > 19 else None
    control_paths = [sys.argv[i] if len(sys.argv) > i else None
                     for i in range(20, 29)]
    ss_balls_path = sys.argv[29] if len(sys.argv) > 29 else None
    DATA_START_SECTOR = RESERVED_SECTORS + FAT_COUNT * FAT_SECTORS + ROOT_DIR_SECTORS
    
    with open(image_path, "rb") as handle:
        image = bytearray(handle.read())

    expected_size = TOTAL_SECTORS * SECTOR_SIZE
    if len(image) != expected_size:
        raise ValueError(f"unexpected image size: {len(image)}")

    for i in range(RESERVED_SECTORS * SECTOR_SIZE, expected_size):
        image[i] = 0

    fat = bytearray(FAT_SECTORS * SECTOR_SIZE)
    fat[0:3] = bytes((0xF0, 0xFF, 0xFF))

    # Pre-allocate clusters for known files
    allocations = {
        2: END_OF_CHAIN,   # PROGRAMS dir
        3: END_OF_CHAIN,   # DOCS dir
        4: END_OF_CHAIN,   # MISC dir
        5: END_OF_CHAIN,   # README.TXT
        6: END_OF_CHAIN,   # SHELL.TXT
        7: END_OF_CHAIN,   # DESKBAR.TXT
        8: END_OF_CHAIN,   # DOCS/README.TXT
        9: END_OF_CHAIN,   # DOCS/ROADMAP.TXT
        10: END_OF_CHAIN,  # MISC/PALETTE.TXT
        11: END_OF_CHAIN,  # MISC/THEMES.TXT
        12: END_OF_CHAIN,  # DESKTOP.INI
        13: END_OF_CHAIN,  # ICONS dir
    }
    for cluster, value in allocations.items():
        fat12_set(fat, cluster, value)

    for copy_index in range(FAT_COUNT):
        start = (RESERVED_SECTORS + copy_index * FAT_SECTORS) * SECTOR_SIZE
        image[start:start + len(fat)] = fat

    files = {
        5: b"BlesKernOS FAT12 floppy\r\nDeskbar ya lee archivos reales.\r\nPrograms, Docs y Misc viven aqui.\r\n",
        6: b"Shell detectado.\r\nDeskbar lo encontro en /Programs.\r\nSiguiente paso: launcher real.\r\n",
        7: b"Deskbar 90s listo.\r\nPrograms, Documents y Misc salen del FAT.\r\n",
        8: b"Documentos del sistema.\r\nEste floppy ya es visible desde VFS.\r\n",
        9: b"Roadmap\r\n- Lanzador real\r\n- Programas externos\r\n",
        10: b"Paleta GUI\r\n- celeste\r\n- gris 90s\r\n- verde suave\r\n",
        11: b"Themes\r\nClassic 90s\r\nHaiku Soft\r\n",
        12: b"",
    }

    if desktop_path:
        with open(desktop_path, "rb") as handle:
            files[12] = handle.read().replace(b"\n", b"\r\n")

    def load_program(path, label):
        if not path:
            return b""
        try:
            with open(path, "rb") as handle:
                data = handle.read()
            print(f"{label} size: {len(data)} bytes", file=sys.stderr)
            return data
        except Exception as e:
            print(f"Warning: No se pudo leer {path}: {e}", file=sys.stderr)
            return b""

    shell_data = load_program(shell_path, "Shell")
    filebrowser_data = load_program(filebrowser_path, "Filebrowser")
    texteditor_data = load_program(texteditor_path, "Texteditor")
    calculator_data = load_program(calculator_path, "Calculator")
    midamp_data = load_program(midamp_path, "Midamp")
    processmanager_data = load_program(processmanager_path, "Processmanager")
    calendar_data = load_program(calendar_path, "Calendar")
    about_data = load_program(script_path("..", "build", "programs", "about.o"),
                              "About")
    runbox_data = load_program(script_path("..", "build", "programs", "runbox.o"),
                               "Runbox")
    imageviewer_data = load_program(
        script_path("..", "build", "programs", "imageviewer.o"),
        "Imageviewer",
    )
    games_data = load_program(script_path("..", "build", "programs", "games.o"),
                              "Games")
    gears_data = load_program(script_path("..", "build", "programs", "gears.o"),
                              "Gears")
    paint_data = load_program(script_path("..", "build", "programs", "paint.o"),
                              "Paint")
    apitest_data = load_program(script_path("..", "build", "programs", "apitest.o"),
                                "APITest")
    wine_data = load_program(script_path("..", "build", "programs", "wine.o"),
                             "Wine PE launcher")
    hello_exe_data = load_program(
        script_path("..", "build", "win32", "HELLO.EXE"),
        "Win32 HELLO.EXE",
    )
    notepad_exe_data = load_program(
        script_path("..", "build", "win32", "NOTEPAD.EXE"),
        "Win32 NOTEPAD.EXE",
    )
    msgbox_exe_data = load_program(
        script_path("..", "build", "win32", "MSGBOX.EXE"),
        "Win32 MSGBOX.EXE",
    )
    dynload_exe_data = load_program(
        script_path("..", "build", "win32", "DYNLOAD.EXE"),
        "Win32 DYNLOAD.EXE",
    )
    dlltest_exe_data = load_program(
        script_path("..", "build", "win32", "DLLTEST.EXE"), "Win32 DLLTEST.EXE")
    tlstest_exe_data = load_program(
        script_path("..", "build", "win32", "TLSTEST.EXE"), "Win32 TLSTEST.EXE")
    threadtest_exe_data = load_program(
        script_path("..", "build", "win32", "THREADTEST.EXE"),
        "Win32 THREADTEST.EXE",
    )
    synctest_exe_data = load_program(
        script_path("..", "build", "win32", "SYNCTEST.EXE"),
        "Win32 SYNCTEST.EXE",
    )
    resourcetest_exe_data = load_program(
        script_path("..", "build", "win32", "RESOURCETEST.EXE"),
        "Win32 RESOURCETEST.EXE",
    )
    menutest_exe_data = load_program(
        script_path("..", "build", "win32", "MENUTEST.EXE"),
        "Win32 MENUTEST.EXE",
    )
    dialogtest_exe_data = load_program(
        script_path("..", "build", "win32", "DIALOGTEST.EXE"),
        "Win32 DIALOGTEST.EXE",
    )
    sehtest_exe_data = load_program(
        script_path("..", "build", "win32", "SEHTEST.EXE"),
        "Win32 SEHTEST.EXE",
    )
    winecalc_compat_exe_data = load_program(
        script_path("..", "build", "win32", "WINECALC_COMPAT.EXE"),
        "Win32 WineCalc compatibility test",
    )
    edittest_exe_data = load_program(
        script_path("..", "build", "win32", "EDITTEST.EXE"),
        "Win32 multiline EDIT test",
    )
    testdll_data = load_program(
        script_path("..", "build", "win32", "TESTDLL.DLL"), "Win32 TESTDLL.DLL")
    screensaverd_data = load_program(screensaverd_path, "ScreenSaver daemon")
    ss_logo_data = load_program(ss_logo_path, "ScreenSaver logo")
    ss_pipes_data = load_program(ss_pipes_path, "ScreenSaver pipes")
    ss_balls_data = load_program(ss_balls_path, "ScreenSaver balls")
    tinygl_data = load_program(tinygl_path, "TinyGL library")
    libc_data = load_program(libc_path, "C library")
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
    control_data = [load_program(path, label)
                    for path, (_, label) in zip(control_paths, control_names)]
    wallpaper_payloads = []

    screensv_data = (
        b"enabled=1\r\n"
        b"timeout=300\r\n"
        b"path=/SYSTEM/SCREENS/SSLOGO.SCV\r\n"
    )
    datetime_data = b"format=24\r\ntimezone=0\r\n"
    mouse_data = b"sensitivity=3\r\ntrail=0\r\n"
    if screensv_path:
        try:
            with open(screensv_path, "rb") as handle:
                screensv_data = handle.read().replace(b"\n", b"\r\n")
            print(f"ScreenSaver INI size: {len(screensv_data)} bytes", file=sys.stderr)
        except Exception as e:
            print(f"Warning: No se pudo leer {screensv_path}: {e}", file=sys.stderr)

    wallpapers_dir = script_path("..", "assets", "wallpapers")
    if os.path.isdir(wallpapers_dir):
        for wallpaper_name in sorted(os.listdir(wallpapers_dir)):
            wallpaper_path = os.path.join(wallpapers_dir, wallpaper_name)
            if (not os.path.isfile(wallpaper_path) or
                    not is_wallpaper_file(wallpaper_name)):
                continue
            wallpaper_data = load_program(wallpaper_path,
                                          f"Wallpaper {wallpaper_name}")
            if wallpaper_data:
                wallpaper_payloads.append((wallpaper_name, wallpaper_data))

    skin_dir = script_path("..", "programs", "winmap")
    midhdr_data = load_program(os.path.join(skin_dir, "hdr.gif"), "Midamp HDR")
    midbtn_data = load_program(os.path.join(skin_dir, "buttons.gif"), "Midamp BTN")
    midbot_data = load_program(os.path.join(skin_dir, "bottom.gif"), "Midamp BOT")
    about_gif_data = load_program(about_gif_path, "About GIF")
    associations_data = load_program(associations_path, "Associations INI")

    # FAT12 floppy images have very little room after the raw kernel reserve.
    # Keep the full payload for FAT32/ATA, but make the boot floppy a compact
    # compatibility image instead of overflowing past the end of the disk.
    for wallpaper_name, wallpaper_data in wallpaper_payloads:
        omit_floppy_payload(f"wallpaper {wallpaper_name}", wallpaper_data)
    wallpaper_payloads = []
    gears_data = omit_floppy_payload("GEARS.O", gears_data)
    tinygl_data = omit_floppy_payload("TINYGL.A", tinygl_data)
    ss_pipes_data = omit_floppy_payload("SSPIPES.SCV", ss_pipes_data)
    ss_balls_data = omit_floppy_payload("SSBALLS.SCV", ss_balls_data)
    if len(control_data) > 1:
        control_data[1] = omit_floppy_payload("APPEAR.CPL", control_data[1])
    if len(control_data) > 2:
        control_data[2] = omit_floppy_payload("DISPLAY.CPL", control_data[2])

    shell_cluster = 14
    shell_clusters = cluster_count(shell_data)
    filebrowser_cluster = shell_cluster + shell_clusters
    filebrowser_clusters = cluster_count(filebrowser_data)
    texteditor_cluster = filebrowser_cluster + filebrowser_clusters
    texteditor_clusters = cluster_count(texteditor_data)
    calculator_cluster = texteditor_cluster + texteditor_clusters
    calculator_clusters = cluster_count(calculator_data)
    midamp_cluster = calculator_cluster + calculator_clusters
    midamp_clusters = cluster_count(midamp_data)
    processmanager_cluster = midamp_cluster + midamp_clusters
    processmanager_clusters = cluster_count(processmanager_data)
    calendar_cluster = processmanager_cluster + processmanager_clusters
    calendar_clusters = cluster_count(calendar_data)
    about_cluster = calendar_cluster + calendar_clusters
    about_clusters = cluster_count(about_data)
    runbox_cluster = about_cluster + about_clusters
    runbox_clusters = cluster_count(runbox_data)
    imageviewer_cluster = runbox_cluster + runbox_clusters
    imageviewer_clusters = cluster_count(imageviewer_data)
    games_cluster = imageviewer_cluster + imageviewer_clusters
    games_clusters = cluster_count(games_data)
    gears_cluster = games_cluster + games_clusters
    gears_clusters = cluster_count(gears_data)
    paint_cluster = gears_cluster + gears_clusters
    paint_clusters = cluster_count(paint_data)
    apitest_cluster = paint_cluster + paint_clusters
    apitest_clusters = cluster_count(apitest_data)
    wine_cluster = apitest_cluster + apitest_clusters
    wine_clusters = cluster_count(wine_data)
    hello_exe_cluster = wine_cluster + wine_clusters
    hello_exe_clusters = cluster_count(hello_exe_data)
    notepad_exe_cluster = hello_exe_cluster + hello_exe_clusters
    notepad_exe_clusters = cluster_count(notepad_exe_data)
    msgbox_exe_cluster = notepad_exe_cluster + notepad_exe_clusters
    msgbox_exe_clusters = cluster_count(msgbox_exe_data)
    dynload_exe_cluster = msgbox_exe_cluster + msgbox_exe_clusters
    dynload_exe_clusters = cluster_count(dynload_exe_data)
    dlltest_exe_cluster = dynload_exe_cluster + dynload_exe_clusters
    dlltest_exe_clusters = cluster_count(dlltest_exe_data)
    tlstest_exe_cluster = dlltest_exe_cluster + dlltest_exe_clusters
    tlstest_exe_clusters = cluster_count(tlstest_exe_data)
    threadtest_exe_cluster = tlstest_exe_cluster + tlstest_exe_clusters
    threadtest_exe_clusters = cluster_count(threadtest_exe_data)
    synctest_exe_cluster = threadtest_exe_cluster + threadtest_exe_clusters
    synctest_exe_clusters = cluster_count(synctest_exe_data)
    resourcetest_exe_cluster = synctest_exe_cluster + synctest_exe_clusters
    resourcetest_exe_clusters = cluster_count(resourcetest_exe_data)
    menutest_exe_cluster = resourcetest_exe_cluster + resourcetest_exe_clusters
    menutest_exe_clusters = cluster_count(menutest_exe_data)
    dialogtest_exe_cluster = menutest_exe_cluster + menutest_exe_clusters
    dialogtest_exe_clusters = cluster_count(dialogtest_exe_data)
    sehtest_exe_cluster = dialogtest_exe_cluster + dialogtest_exe_clusters
    sehtest_exe_clusters = cluster_count(sehtest_exe_data)
    winecalc_compat_exe_cluster = sehtest_exe_cluster + sehtest_exe_clusters
    winecalc_compat_exe_clusters = cluster_count(winecalc_compat_exe_data)
    edittest_exe_cluster = winecalc_compat_exe_cluster + winecalc_compat_exe_clusters
    edittest_exe_clusters = cluster_count(edittest_exe_data)
    screensaverd_cluster = edittest_exe_cluster + edittest_exe_clusters
    screensaverd_clusters = cluster_count(screensaverd_data)
    ss_logo_cluster = screensaverd_cluster + screensaverd_clusters
    ss_logo_clusters = cluster_count(ss_logo_data)
    ss_pipes_cluster = ss_logo_cluster + ss_logo_clusters
    ss_pipes_clusters = cluster_count(ss_pipes_data)
    ss_balls_cluster = ss_pipes_cluster + ss_pipes_clusters
    ss_balls_clusters = cluster_count(ss_balls_data)
    midhdr_cluster = ss_balls_cluster + ss_balls_clusters
    midhdr_clusters = cluster_count(midhdr_data)
    midbtn_cluster = midhdr_cluster + midhdr_clusters
    midbtn_clusters = cluster_count(midbtn_data)
    midbot_cluster = midbtn_cluster + midbtn_clusters
    midbot_clusters = cluster_count(midbot_data)
    next_icon_cluster = midbot_cluster + midbot_clusters
    # /PROGRAMS puede superar las 16 entradas al sumar varios protectores.
    programs_extra_cluster = next_icon_cluster
    next_icon_cluster += 1
    # /ICONS supera las 16 entradas que caben en su cluster fijo (13).
    # Reservar un segundo cluster antes de asignar los datos de los BMP.
    icons_extra_cluster = next_icon_cluster
    next_icon_cluster += 1

    icon_files = []
    for icon_name in (
        "ICONS.PAK", "FILES.BMP", "SHELL.BMP", "EDITOR.BMP", "CALC.BMP",
        "PROCESOS.BMP", "MIDAMP.BMP", "CDROM.BMP", "ABOUT.BMP",
        "FILE.BMP", "TEXT.BMP", "CONFIG.BMP", "IMAGE.BMP",
        "OBJECT.BMP", "MIDI.BMP",
        "FOLDER.BMP", "HDD.BMP", "CD.BMP", "USB.BMP", "FLOPPY.BMP",
        "CONTROL.BMP", "DISPLAY.BMP", "SOUND.BMP", "DATETIME.BMP",
        "MOUSE.BMP", "KEYBOARD.BMP", "SYSTEM.BMP", "DEVICES.BMP",
        "MONITOR.BMP",
    ):
        icon_path = os.path.join(icons_path, icon_name) if icons_path else ""
        if icon_path and os.path.isfile(icon_path):
            with open(icon_path, "rb") as handle:
                icon_data = handle.read()
            if icon_name == "ICONS.PAK" or len(icon_data) > 64 * 1024:
                omit_floppy_payload(icon_name, icon_data)
                continue
            icon_files.append((icon_name, next_icon_cluster, icon_data))
            next_icon_cluster += cluster_count(icon_data)
    about_gif_cluster = 0
    if about_gif_data:
        about_gif_cluster = next_icon_cluster
        next_icon_cluster += cluster_count(about_gif_data)
    associations_cluster = 0
    if associations_data:
        associations_cluster = next_icon_cluster
        next_icon_cluster += cluster_count(associations_data)
    screensv_cluster = next_icon_cluster
    next_icon_cluster += cluster_count(screensv_data)
    datetime_cluster = next_icon_cluster
    next_icon_cluster += cluster_count(datetime_data)
    mouse_ini_cluster = next_icon_cluster
    next_icon_cluster += cluster_count(mouse_data)
    system_cluster = next_icon_cluster; next_icon_cluster += 1
    user_dir_cluster = next_icon_cluster; next_icon_cluster += 1
    user_config_dir_cluster = next_icon_cluster; next_icon_cluster += 1
    libs_cluster = next_icon_cluster; next_icon_cluster += 1
    tinygl_dir_cluster = next_icon_cluster; next_icon_cluster += 1
    libc_dir_cluster = next_icon_cluster; next_icon_cluster += 1
    wine_dir_cluster = next_icon_cluster; next_icon_cluster += 1
    services_cluster = next_icon_cluster; next_icon_cluster += 1
    screens_cluster = next_icon_cluster; next_icon_cluster += 1
    wallpapers_dir_cluster = next_icon_cluster; next_icon_cluster += 1
    wallpaper_files = []
    for wallpaper_name, wallpaper_data in wallpaper_payloads:
        wallpaper_cluster = next_icon_cluster
        wallpaper_files.append((wallpaper_name, wallpaper_cluster, wallpaper_data))
        next_icon_cluster += cluster_count(wallpaper_data)
    tinygl_cluster = next_icon_cluster
    next_icon_cluster += cluster_count(tinygl_data)
    libc_cluster = next_icon_cluster
    next_icon_cluster += cluster_count(libc_data)
    testdll_cluster = next_icon_cluster
    next_icon_cluster += cluster_count(testdll_data)
    control_dir_cluster = next_icon_cluster
    next_icon_cluster += 1
    control_clusters = []
    for payload in control_data:
        cluster = next_icon_cluster
        control_clusters.append(cluster)
        next_icon_cluster += cluster_count(payload)

    max_next_cluster = TOTAL_SECTORS - DATA_START_SECTOR + 2
    if next_icon_cluster > max_next_cluster:
        used = next_icon_cluster - 2
        available = max_next_cluster - 2
        raise ValueError(
            f"contenido FAT12 demasiado grande: requiere {used} clusters, "
            f"hay {available}; use build/bleskernos-ata.img para la imagen completa"
        )

    root_dir = bytearray(ROOT_DIR_SECTORS * SECTOR_SIZE)
    root_entries = [
        dir_entry("PROGRAMS", 0x10, 2, 0),
        dir_entry("DOCS", 0x10, 3, 0),
        dir_entry("MISC", 0x10, 4, 0),
        dir_entry("README.TXT", 0x20, 5, len(files[5])),
        dir_entry("ICONS", 0x10, 13, 0),
        dir_entry("SYSTEM", 0x10, system_cluster, 0),
    ]
    if midhdr_data:
        root_entries.append(dir_entry("MIDHDR.GIF", 0x20, midhdr_cluster, len(midhdr_data)))
    if midbtn_data:
        root_entries.append(dir_entry("MIDBTN.GIF", 0x20, midbtn_cluster, len(midbtn_data)))
    if midbot_data:
        root_entries.append(dir_entry("MIDBOT.GIF", 0x20, midbot_cluster, len(midbot_data)))
    if about_gif_data:
        root_entries.append(
            dir_entry("ABOUNT.GIF", 0x20, about_gif_cluster,
                      len(about_gif_data))
        )
    if associations_data:
        root_entries.append(
            dir_entry("ASSOC.INI", 0x20, associations_cluster,
                      len(associations_data))
        )
    for index, entry in enumerate(root_entries):
        root_dir[index * 32:(index + 1) * 32] = entry

    root_start = (RESERVED_SECTORS + FAT_COUNT * FAT_SECTORS) * SECTOR_SIZE
    image[root_start:root_start + len(root_dir)] = root_dir

    programs_entries = [
        dir_entry("SHELL.TXT", 0x20, 6, len(files[6])),
        dir_entry("DESKBAR.TXT", 0x20, 7, len(files[7])),
    ]
    
    # Agregar shell.o a la lista de programs si existe
    if shell_data:
        programs_entries.append(dir_entry("SHELL.O", 0x20, shell_cluster, len(shell_data)))
    if about_data:
        programs_entries.append(
            dir_entry("ABOUT.O", 0x20, about_cluster, len(about_data))
        )
    if runbox_data:
        programs_entries.append(dir_entry("RUNBOX.O", 0x20, runbox_cluster, len(runbox_data)))
    if filebrowser_data:
        programs_entries.append(
            dir_entry("FILES.O", 0x20, filebrowser_cluster, len(filebrowser_data))
        )
    if texteditor_data:
        programs_entries.append(
            dir_entry("TEXTEDIT.O", 0x20, texteditor_cluster, len(texteditor_data))
        )
    if calculator_data:
        programs_entries.append(
            dir_entry("CALC.O", 0x20, calculator_cluster, len(calculator_data))
        )
    if midamp_data:
        programs_entries.append(
            dir_entry("MIDAMP.O", 0x20, midamp_cluster, len(midamp_data))
        )
    if processmanager_data:
        programs_entries.append(
            dir_entry("PROCMAN.O", 0x20, processmanager_cluster,
                      len(processmanager_data))
        )
    if calendar_data:
        programs_entries.append(
            dir_entry("CALENDAR.O", 0x20, calendar_cluster,
                      len(calendar_data))
        )
    if gears_data:
        programs_entries.append(dir_entry("GEARS.O", 0x20, gears_cluster,
                                          len(gears_data)))
    if paint_data:
        programs_entries.append(dir_entry("PAINT.O", 0x20, paint_cluster,
                                          len(paint_data)))
    if imageviewer_data:
        programs_entries.append(
            dir_entry("VIEWER.O", 0x20, imageviewer_cluster,
                      len(imageviewer_data))
        )
    if games_data:
        programs_entries.append(
            dir_entry("GAMES.O", 0x20, games_cluster, len(games_data))
        )
    if apitest_data:
        programs_entries.append(
            dir_entry("APITEST.O", 0x20, apitest_cluster,
                      len(apitest_data))
        )
    if wine_data:
        programs_entries.append(
            dir_entry("WINE.O", 0x20, wine_cluster, len(wine_data))
        )
    if hello_exe_data:
        programs_entries.append(
            dir_entry("HELLO.EXE", 0x20, hello_exe_cluster,
                      len(hello_exe_data))
        )
    if notepad_exe_data:
        programs_entries.append(
            dir_entry("NOTEPAD.EXE", 0x20, notepad_exe_cluster,
                      len(notepad_exe_data))
        )
    if msgbox_exe_data:
        programs_entries.append(
            dir_entry("MSGBOX.EXE", 0x20, msgbox_exe_cluster,
                      len(msgbox_exe_data))
        )
    if dynload_exe_data:
        programs_entries.append(
            dir_entry("DYNLOAD.EXE", 0x20, dynload_exe_cluster,
                      len(dynload_exe_data))
        )
    if dlltest_exe_data:
        programs_entries.append(
            dir_entry("DLLTEST.EXE", 0x20, dlltest_exe_cluster,
                      len(dlltest_exe_data))
        )
    if tlstest_exe_data:
        programs_entries.append(
            dir_entry("TLSTEST.EXE", 0x20, tlstest_exe_cluster,
                      len(tlstest_exe_data))
        )
    if threadtest_exe_data:
        programs_entries.append(
            dir_entry("THREADTEST.EXE", 0x20, threadtest_exe_cluster,
                      len(threadtest_exe_data))
        )
    if synctest_exe_data:
        programs_entries.append(
            dir_entry("SYNCTEST.EXE", 0x20, synctest_exe_cluster,
                      len(synctest_exe_data))
        )
    if resourcetest_exe_data:
        programs_entries.append(
            dir_entry("RESOURCETEST.EXE", 0x20, resourcetest_exe_cluster,
                      len(resourcetest_exe_data))
        )
    if menutest_exe_data:
        programs_entries.append(
            dir_entry("MENUTEST.EXE", 0x20, menutest_exe_cluster,
                      len(menutest_exe_data))
        )
    if dialogtest_exe_data:
        programs_entries.append(
            dir_entry("DIALOGTEST.EXE", 0x20, dialogtest_exe_cluster,
                      len(dialogtest_exe_data))
        )
    if sehtest_exe_data:
        programs_entries.append(
            dir_entry("SEHTEST.EXE", 0x20, sehtest_exe_cluster,
                      len(sehtest_exe_data))
        )

    if winecalc_compat_exe_data:
        programs_entries.append(
            dir_entry("WCCOMPAT.EXE", 0x20, winecalc_compat_exe_cluster,
                      len(winecalc_compat_exe_data))
        )

    if edittest_exe_data:
        programs_entries.append(
            dir_entry("EDITTEST.EXE", 0x20, edittest_exe_cluster,
                      len(edittest_exe_data))
        )

    programs_dir = build_directory(programs_entries)
    docs_dir = build_directory([
        dir_entry("README.TXT", 0x20, 8, len(files[8])),
        dir_entry("ROADMAP.TXT", 0x20, 9, len(files[9])),
    ])
    misc_dir = build_directory([
        dir_entry("PALETTE.TXT", 0x20, 10, len(files[10])),
        dir_entry("THEMES.TXT", 0x20, 11, len(files[11])),
    ])
    icons_entries = [
        dir_entry(name, 0x20, cluster, len(payload))
        for name, cluster, payload in icon_files
    ]
    icons_dir = build_directory(icons_entries)
    system_dir = build_directory([
        dir_entry("USER", 0x10, user_dir_cluster, 0),
        dir_entry("LIBS", 0x10, libs_cluster, 0),
        dir_entry("SERVICES", 0x10, services_cluster, 0),
        dir_entry("SCREENS", 0x10, screens_cluster, 0),
        dir_entry("CONTROL", 0x10, control_dir_cluster, 0),
        dir_entry("WALLPAPR", 0x10, wallpapers_dir_cluster, 0),
    ])
    user_dir = build_directory([
        dir_entry("CONFIG", 0x10, user_config_dir_cluster, 0),
    ])
    user_config_dir = build_directory([
        dir_entry("DESKTOP.INI", 0x20, 12, len(files[12])),
        dir_entry("SCREENSV.INI", 0x20, screensv_cluster, len(screensv_data)),
        dir_entry("DATETIME.INI", 0x20, datetime_cluster, len(datetime_data)),
        dir_entry("MOUSE.INI", 0x20, mouse_ini_cluster, len(mouse_data)),
    ])
    libs_dir = build_directory([
        entry for entry in (
            dir_entry("TINYGL", 0x10, tinygl_dir_cluster, 0) if tinygl_data else None,
            dir_entry("LIBC", 0x10, libc_dir_cluster, 0) if libc_data else None,
            dir_entry("WINE", 0x10, wine_dir_cluster, 0) if testdll_data else None,
        ) if entry
    ])
    tinygl_dir = build_directory([
        dir_entry("TINYGL.A", 0x20, tinygl_cluster, len(tinygl_data)),
    ] if tinygl_data else [])
    libc_dir = build_directory([
        dir_entry("LIBC.A", 0x20, libc_cluster, len(libc_data)),
    ] if libc_data else [])
    wine_dir = build_directory([
        dir_entry("TESTDLL.DLL", 0x20, testdll_cluster, len(testdll_data)),
    ] if testdll_data else [])
    services_dir = build_directory([
        dir_entry("SCREENSV.O", 0x20, screensaverd_cluster, len(screensaverd_data)),
    ] if screensaverd_data else [])
    screens_dir = build_directory([
        entry for entry in (
            dir_entry("SSLOGO.SCV", 0x20, ss_logo_cluster, len(ss_logo_data)) if ss_logo_data else None,
            dir_entry("SSPIPES.SCV", 0x20, ss_pipes_cluster, len(ss_pipes_data)) if ss_pipes_data else None,
            dir_entry("SSBALLS.SCV", 0x20, ss_balls_cluster, len(ss_balls_data)) if ss_balls_data else None,
        ) if entry
    ])
    wallpapers_dir = build_directory([
        dir_entry(name, 0x20, cluster, len(payload))
        for name, cluster, payload in wallpaper_files
    ])
    control_dir = build_directory([
        dir_entry(name, 0x20, cluster, len(payload))
        for (name, _), cluster, payload in
        zip(control_names, control_clusters, control_data)
        if payload
    ])

    if len(programs_dir) > SECTOR_SIZE * 2:
        raise ValueError("directorio /PROGRAMS supera dos clusters")
    write_cluster(image, 2, programs_dir[:SECTOR_SIZE])
    if len(programs_dir) > SECTOR_SIZE:
        write_cluster(image, programs_extra_cluster, programs_dir[SECTOR_SIZE:])
        fat12_set(fat, 2, programs_extra_cluster)
        fat12_set(fat, programs_extra_cluster, END_OF_CHAIN)
    write_cluster(image, 3, docs_dir)
    write_cluster(image, 4, misc_dir)
    if len(icons_dir) > SECTOR_SIZE * 2:
        raise ValueError("directorio /ICONS supera dos clusters")
    write_cluster(image, 13, icons_dir[:SECTOR_SIZE])
    if len(icons_dir) > SECTOR_SIZE:
        write_cluster(image, icons_extra_cluster, icons_dir[SECTOR_SIZE:])
        fat12_set(fat, 13, icons_extra_cluster)
        fat12_set(fat, icons_extra_cluster, END_OF_CHAIN)
    for cluster, directory in (
        (system_cluster, system_dir),
        (user_dir_cluster, user_dir),
        (user_config_dir_cluster, user_config_dir),
        (libs_cluster, libs_dir),
        (tinygl_dir_cluster, tinygl_dir), (libc_dir_cluster, libc_dir),
        (wine_dir_cluster, wine_dir),
        (services_cluster, services_dir), (screens_cluster, screens_dir),
        (wallpapers_dir_cluster, wallpapers_dir), (control_dir_cluster, control_dir),
    ):
        write_cluster(image, cluster, directory)
        fat12_set(fat, cluster, END_OF_CHAIN)

    for cluster, payload in files.items():
        write_cluster(image, cluster, payload)

    # Write program objects using non-overlapping multi-cluster FAT chains.
    if shell_data:
        write_file_clusters(image, fat, shell_cluster, shell_data)
    if about_data:
        write_file_clusters(image, fat, about_cluster, about_data)
    if runbox_data:
        write_file_clusters(image, fat, runbox_cluster, runbox_data)
    if filebrowser_data:
        write_file_clusters(image, fat, filebrowser_cluster, filebrowser_data)
    if texteditor_data:
        write_file_clusters(image, fat, texteditor_cluster, texteditor_data)
    if calculator_data:
        write_file_clusters(image, fat, calculator_cluster, calculator_data)
    if midamp_data:
        write_file_clusters(image, fat, midamp_cluster, midamp_data)
    if processmanager_data:
        write_file_clusters(image, fat, processmanager_cluster,
                            processmanager_data)
    if calendar_data:
        write_file_clusters(image, fat, calendar_cluster, calendar_data)
    if imageviewer_data:
        write_file_clusters(image, fat, imageviewer_cluster, imageviewer_data)
    if games_data:
        write_file_clusters(image, fat, games_cluster, games_data)
    if gears_data:
        write_file_clusters(image, fat, gears_cluster, gears_data)
    if paint_data:
        write_file_clusters(image, fat, paint_cluster, paint_data)
    if apitest_data:
        write_file_clusters(image, fat, apitest_cluster, apitest_data)
    if wine_data:
        write_file_clusters(image, fat, wine_cluster, wine_data)
    if hello_exe_data:
        write_file_clusters(image, fat, hello_exe_cluster, hello_exe_data)
    if notepad_exe_data:
        write_file_clusters(image, fat, notepad_exe_cluster, notepad_exe_data)
    if msgbox_exe_data:
        write_file_clusters(image, fat, msgbox_exe_cluster, msgbox_exe_data)
    if dynload_exe_data:
        write_file_clusters(image, fat, dynload_exe_cluster, dynload_exe_data)
    if dlltest_exe_data:
        write_file_clusters(image, fat, dlltest_exe_cluster, dlltest_exe_data)
    if tlstest_exe_data:
        write_file_clusters(image, fat, tlstest_exe_cluster, tlstest_exe_data)
    if threadtest_exe_data:
        write_file_clusters(image, fat, threadtest_exe_cluster, threadtest_exe_data)
    if synctest_exe_data:
        write_file_clusters(image, fat, synctest_exe_cluster, synctest_exe_data)
    if resourcetest_exe_data:
        write_file_clusters(image, fat, resourcetest_exe_cluster,
                            resourcetest_exe_data)
    if menutest_exe_data:
        write_file_clusters(image, fat, menutest_exe_cluster,
                            menutest_exe_data)
    if dialogtest_exe_data:
        write_file_clusters(image, fat, dialogtest_exe_cluster,
                            dialogtest_exe_data)
    if sehtest_exe_data:
        write_file_clusters(image, fat, sehtest_exe_cluster,
                            sehtest_exe_data)
    if winecalc_compat_exe_data:
        write_file_clusters(image, fat, winecalc_compat_exe_cluster,
                            winecalc_compat_exe_data)
    if edittest_exe_data:
        write_file_clusters(image, fat, edittest_exe_cluster,
                            edittest_exe_data)
    if screensaverd_data:
        write_file_clusters(image, fat, screensaverd_cluster, screensaverd_data)
    if ss_logo_data:
        write_file_clusters(image, fat, ss_logo_cluster, ss_logo_data)
    if ss_pipes_data:
        write_file_clusters(image, fat, ss_pipes_cluster, ss_pipes_data)
    if ss_balls_data:
        write_file_clusters(image, fat, ss_balls_cluster, ss_balls_data)
    if midhdr_data:
        write_file_clusters(image, fat, midhdr_cluster, midhdr_data)
    if midbtn_data:
        write_file_clusters(image, fat, midbtn_cluster, midbtn_data)
    if midbot_data:
        write_file_clusters(image, fat, midbot_cluster, midbot_data)
    for name, cluster, payload in icon_files:
        write_file_clusters(image, fat, cluster, payload)
    if about_gif_data:
        write_file_clusters(image, fat, about_gif_cluster, about_gif_data)
    if associations_data:
        write_file_clusters(image, fat, associations_cluster, associations_data)
    if screensv_data:
        write_file_clusters(image, fat, screensv_cluster, screensv_data)
    if datetime_data:
        write_file_clusters(image, fat, datetime_cluster, datetime_data)
    if mouse_data:
        write_file_clusters(image, fat, mouse_ini_cluster, mouse_data)
    for name, cluster, payload in wallpaper_files:
        write_file_clusters(image, fat, cluster, payload)
    if tinygl_data:
        write_file_clusters(image, fat, tinygl_cluster, tinygl_data)
    if libc_data:
        write_file_clusters(image, fat, libc_cluster, libc_data)
    if testdll_data:
        write_file_clusters(image, fat, testdll_cluster, testdll_data)
    for cluster, payload in zip(control_clusters, control_data):
        if payload:
            write_file_clusters(image, fat, cluster, payload)
    if (shell_data or about_data or runbox_data or filebrowser_data or
            texteditor_data or calculator_data or midamp_data or
            processmanager_data or calendar_data or imageviewer_data or
            games_data or gears_data or paint_data or
            apitest_data or wine_data or hello_exe_data or msgbox_exe_data or
            dynload_exe_data or
            dlltest_exe_data or tlstest_exe_data or threadtest_exe_data or
            testdll_data or
            screensaverd_data or ss_logo_data or ss_pipes_data or ss_balls_data or
            screensv_data or
            wallpaper_files or
            midhdr_data or midbtn_data or midbot_data or
            icon_files or about_gif_data or associations_data or
            any(control_data)):
        # Update both FAT copies
        for copy_index in range(FAT_COUNT):
            start = (RESERVED_SECTORS + copy_index * FAT_SECTORS) * SECTOR_SIZE
            image[start:start + len(fat)] = fat

    with open(image_path, "wb") as handle:
        handle.write(image)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
