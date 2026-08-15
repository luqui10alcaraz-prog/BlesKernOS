#!/usr/bin/env python3
import sys
from pathlib import Path

SECTOR_SIZE = 512
PARTITION_START_LBA = 2048
PARTITION_TYPE_FAT32_LBA = 0x0C


def chs_lba(lba):
    sectors_per_track = 63
    heads = 255
    cylinder = lba // (heads * sectors_per_track)
    temp = lba % (heads * sectors_per_track)
    head = temp // sectors_per_track
    sector = (temp % sectors_per_track) + 1
    if cylinder > 1023:
        return bytes((0xFE, 0xFF, 0xFF))
    return bytes((head & 0xFF, ((sector & 0x3F) | ((cylinder >> 2) & 0xC0)),
                  cylinder & 0xFF))


def patch_hidden_sectors(image, absolute_sector, hidden):
    offset = absolute_sector * SECTOR_SIZE
    image[offset + 28:offset + 32] = hidden.to_bytes(4, "little")


def main():
    if len(sys.argv) != 4:
        print("usage: build_usb_image.py <out.img> <partition-fat32.img> <mbr.bin>",
              file=sys.stderr)
        return 1

    out_path = Path(sys.argv[1])
    part_path = Path(sys.argv[2])
    mbr_path = Path(sys.argv[3])

    part = part_path.read_bytes()
    mbr = bytearray(mbr_path.read_bytes())
    if len(mbr) != SECTOR_SIZE:
        raise ValueError(f"MBR debe medir 512 bytes: {mbr_path}")
    if len(part) % SECTOR_SIZE:
        raise ValueError("La imagen FAT32 debe estar alineada a sectores")

    part_sectors = len(part) // SECTOR_SIZE
    total_sectors = PARTITION_START_LBA + part_sectors
    image = bytearray(total_sectors * SECTOR_SIZE)
    image[0:SECTOR_SIZE] = mbr
    part_offset = PARTITION_START_LBA * SECTOR_SIZE
    image[part_offset:part_offset + len(part)] = part

    entry = bytearray(16)
    entry[0] = 0x80
    entry[1:4] = chs_lba(PARTITION_START_LBA)
    entry[4] = PARTITION_TYPE_FAT32_LBA
    entry[5:8] = chs_lba(PARTITION_START_LBA + part_sectors - 1)
    entry[8:12] = PARTITION_START_LBA.to_bytes(4, "little")
    entry[12:16] = part_sectors.to_bytes(4, "little")
    image[446:462] = entry
    image[510:512] = b"\x55\xAA"

    patch_hidden_sectors(image, PARTITION_START_LBA, PARTITION_START_LBA)
    backup_boot_sector = int.from_bytes(
        image[part_offset + 50:part_offset + 52], "little")
    if backup_boot_sector:
        patch_hidden_sectors(
            image, PARTITION_START_LBA + backup_boot_sector,
            PARTITION_START_LBA)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(image)
    print(f"USB image: {out_path} ({len(image)} bytes, partition LBA {PARTITION_START_LBA})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
