#!/usr/bin/env python3
import struct
import sys
import os

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


def build_directory(entries):
    payload = bytearray(SECTOR_SIZE)
    cursor = 0
    for entry in entries:
        payload[cursor:cursor + 32] = entry
        cursor += 32
    return bytes(payload)


def script_path(*parts):
    return os.path.normpath(os.path.join(SCRIPT_DIR, *parts))


def main():
    global RESERVED_SECTORS, DATA_START_SECTOR

    if len(sys.argv) < 2:
        print(
            "usage: build_fat_floppy.py <image> [shell.o] [filebrowser.o] "
            "[texteditor.o] [calculator.o] [midamp.o] [processmanager.o] [calendar.o] [desktop.ini] "
            "[icons_dir] [about.gif] [associations.ini] [reserved_sectors]",
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
    skin_dir = script_path("..", "programs", "winmap")
    midhdr_data = load_program(os.path.join(skin_dir, "hdr.gif"), "Midamp HDR")
    midbtn_data = load_program(os.path.join(skin_dir, "buttons.gif"), "Midamp BTN")
    midbot_data = load_program(os.path.join(skin_dir, "bottom.gif"), "Midamp BOT")
    about_gif_data = load_program(about_gif_path, "About GIF")
    associations_data = load_program(associations_path, "Associations INI")
    shell_cluster = 14
    shell_clusters = (len(shell_data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    filebrowser_cluster = shell_cluster + shell_clusters
    filebrowser_clusters = (len(filebrowser_data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    texteditor_cluster = filebrowser_cluster + filebrowser_clusters
    texteditor_clusters = (len(texteditor_data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    calculator_cluster = texteditor_cluster + texteditor_clusters
    calculator_clusters = (len(calculator_data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    midamp_cluster = calculator_cluster + calculator_clusters
    midamp_clusters = (len(midamp_data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    processmanager_cluster = midamp_cluster + midamp_clusters
    processmanager_clusters = (len(processmanager_data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    calendar_cluster = processmanager_cluster + processmanager_clusters
    calendar_clusters = (len(calendar_data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    midhdr_cluster = calendar_cluster + calendar_clusters
    midhdr_clusters = (len(midhdr_data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    midbtn_cluster = midhdr_cluster + midhdr_clusters
    midbtn_clusters = (len(midbtn_data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    midbot_cluster = midbtn_cluster + midbtn_clusters
    midbot_clusters = (len(midbot_data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    next_icon_cluster = midbot_cluster + midbot_clusters
    # /ICONS supera las 16 entradas que caben en su cluster fijo (13).
    # Reservar un segundo cluster antes de asignar los datos de los BMP.
    icons_extra_cluster = next_icon_cluster
    next_icon_cluster += 1

    icon_files = []
    for icon_name in (
        "FILES.BMP", "SHELL.BMP", "EDITOR.BMP", "CALC.BMP",
        "PROCESOS.BMP", "MIDAMP.BMP", "CDROM.BMP", "ABOUT.BMP",
        "FILE.BMP", "TEXT.BMP", "CONFIG.BMP", "IMAGE.BMP",
        "OBJECT.BMP", "MIDI.BMP",
        "FOLDER.BMP", "HDD.BMP", "CD.BMP", "USB.BMP", "FLOPPY.BMP",
    ):
        icon_path = os.path.join(icons_path, icon_name) if icons_path else ""
        if icon_path and os.path.isfile(icon_path):
            with open(icon_path, "rb") as handle:
                icon_data = handle.read()
            icon_files.append((icon_name, next_icon_cluster, icon_data))
            next_icon_cluster += (len(icon_data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    about_gif_cluster = 0
    if about_gif_data:
        about_gif_cluster = next_icon_cluster
        next_icon_cluster += (len(about_gif_data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    associations_cluster = 0
    if associations_data:
        associations_cluster = next_icon_cluster
        next_icon_cluster += (len(associations_data) + SECTOR_SIZE - 1) // SECTOR_SIZE

    root_dir = bytearray(ROOT_DIR_SECTORS * SECTOR_SIZE)
    root_entries = [
        dir_entry("PROGRAMS", 0x10, 2, 0),
        dir_entry("DOCS", 0x10, 3, 0),
        dir_entry("MISC", 0x10, 4, 0),
        dir_entry("README.TXT", 0x20, 5, len(files[5])),
        dir_entry("DESKTOP.INI", 0x20, 12, len(files[12])),
        dir_entry("ICONS", 0x10, 13, 0),
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
    if filebrowser_data:
        programs_entries.append(
            dir_entry("FILEBROWSER.O", 0x20, filebrowser_cluster, len(filebrowser_data))
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
    # GEARS.O is an internal app placeholder.
    # The launcher intercepts this name and calls gears_open_from_desktop().
    programs_entries.append(dir_entry("GEARS.O", 0x20, 0, 0))
    # PAINT.O is an internal app placeholder.
    # The launcher intercepts this name and calls paint_open_from_desktop().
    programs_entries.append(dir_entry("PAINT.O", 0x20, 0, 0))
    programs_entries.extend([
        dir_entry("VIEWER.O", 0x20, 0, 0),
        dir_entry("GAMES.O", 0x20, 0, 0),
        dir_entry("SETTINGS.O", 0x20, 0, 0),
    ])
    
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

    write_cluster(image, 2, programs_dir)
    write_cluster(image, 3, docs_dir)
    write_cluster(image, 4, misc_dir)
    if len(icons_dir) > SECTOR_SIZE * 2:
        raise ValueError("directorio /ICONS supera dos clusters")
    write_cluster(image, 13, icons_dir[:SECTOR_SIZE])
    if len(icons_dir) > SECTOR_SIZE:
        write_cluster(image, icons_extra_cluster, icons_dir[SECTOR_SIZE:])
        fat12_set(fat, 13, icons_extra_cluster)
        fat12_set(fat, icons_extra_cluster, END_OF_CHAIN)

    for cluster, payload in files.items():
        write_cluster(image, cluster, payload)

    # Write program objects using non-overlapping multi-cluster FAT chains.
    if shell_data:
        write_file_clusters(image, fat, shell_cluster, shell_data)
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
    if (shell_data or filebrowser_data or texteditor_data or calculator_data or
            midamp_data or processmanager_data or calendar_data or midhdr_data or midbtn_data or midbot_data or
            icon_files or about_gif_data or associations_data):
        # Update both FAT copies
        for copy_index in range(FAT_COUNT):
            start = (RESERVED_SECTORS + copy_index * FAT_SECTORS) * SECTOR_SIZE
            image[start:start + len(fat)] = fat

    with open(image_path, "wb") as handle:
        handle.write(image)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
