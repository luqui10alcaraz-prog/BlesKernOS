#!/usr/bin/env python3
"""Build BlesKernOS GRAPHICS.PAK from the PNG assets in @react95/icons.

The package intentionally keeps one canonical bitmap per React95 component
name. 32x32 variants are preferred; when unavailable the closest/largest
variant is selected. PNG payloads stay compressed and are decoded by the OS.
"""

from __future__ import annotations

import argparse
import io
import re
import struct
import tarfile
import urllib.request
from dataclasses import dataclass
from pathlib import Path


MAGIC = b"BKGP"
VERSION = 1
ENTRY_SIZE = 64
NAME_SIZE = 48
DEFAULT_VERSION = "2.5.3"
PNG_NAME = re.compile(
    r"^(?P<name>.+)_(?P<width>[0-9]+)x(?P<height>[0-9]+)_"
    r"(?P<depth>[0-9]+)\.png$",
    re.IGNORECASE,
)


@dataclass(frozen=True)
class Candidate:
    name: str
    width: int
    height: int
    depth: int
    filename: str
    data: bytes


def candidate_score(candidate: Candidate) -> tuple[int, int, int, int, str]:
    exact_32 = candidate.width == 32 and candidate.height == 32
    distance = abs(candidate.width - 32) + abs(candidate.height - 32)
    area = candidate.width * candidate.height
    return (0 if exact_32 else 1, distance, -candidate.depth, -area,
            candidate.filename.lower())


def parse_candidate(filename: str, data: bytes) -> Candidate | None:
    match = PNG_NAME.match(Path(filename).name)
    if not match:
        return None
    name = match.group("name")
    encoded = name.encode("ascii", "strict")
    if not encoded or len(encoded) >= NAME_SIZE:
        raise ValueError(f"nombre de recurso invalido/largo: {name!r}")
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError(f"{filename}: PNG invalido")
    return Candidate(
        name=name,
        width=int(match.group("width")),
        height=int(match.group("height")),
        depth=int(match.group("depth")),
        filename=Path(filename).name,
        data=data,
    )


def candidates_from_directory(directory: Path) -> list[Candidate]:
    candidates: list[Candidate] = []
    for path in sorted(directory.glob("*.png")):
        candidate = parse_candidate(path.name, path.read_bytes())
        if candidate:
            candidates.append(candidate)
    return candidates


def download_tarball(version: str) -> bytes:
    url = (
        "https://registry.npmjs.org/@react95/icons/-/"
        f"icons-{version}.tgz"
    )
    print(f"[GRAPHICS] descargando @react95/icons {version}")
    with urllib.request.urlopen(url, timeout=60) as response:
        return response.read()


def candidates_from_tarball(raw_tarball: bytes) -> list[Candidate]:
    candidates: list[Candidate] = []
    with tarfile.open(fileobj=io.BytesIO(raw_tarball), mode="r:gz") as archive:
        for member in archive.getmembers():
            # The npm release also contains a duplicated png/png directory.
            if (not member.isfile() or
                    not member.name.startswith("package/png/") or
                    member.name.count("/") != 2):
                continue
            extracted = archive.extractfile(member)
            if not extracted:
                continue
            candidate = parse_candidate(member.name, extracted.read())
            if candidate:
                candidates.append(candidate)
    return candidates


def select_canonical(candidates: list[Candidate]) -> list[Candidate]:
    groups: dict[str, list[Candidate]] = {}
    for candidate in candidates:
        groups.setdefault(candidate.name.lower(), []).append(candidate)
    selected = [min(group, key=candidate_score) for group in groups.values()]
    return sorted(selected, key=lambda item: item.name.lower())


def build_package(selected: list[Candidate], output: Path,
                  manifest: Path | None, source_version: str) -> None:
    if not selected:
        raise ValueError("no se encontraron iconos React95")

    table_end = 12 + ENTRY_SIZE * len(selected)
    payload = bytearray()
    entries: list[tuple[Candidate, int, int]] = []
    for candidate in selected:
        while (table_end + len(payload)) & 3:
            payload.append(0)
        offset = table_end + len(payload)
        payload.extend(candidate.data)
        entries.append((candidate, offset, len(candidate.data)))

    package = bytearray()
    package.extend(MAGIC)
    package.extend(struct.pack("<II", VERSION, len(entries)))
    for candidate, offset, size in entries:
        name = candidate.name.encode("ascii")
        package.extend(name + bytes(NAME_SIZE - len(name)))
        package.extend(struct.pack(
            "<HHHHII", candidate.width, candidate.height,
            1, candidate.depth, offset, size,
        ))
    if len(package) != table_end:
        raise AssertionError("la tabla GRAPHICS.PAK no mide lo esperado")
    package.extend(payload)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(package)
    print(
        f"[GRAPHICS] {output}: {len(entries)} iconos, "
        f"{len(package)} bytes"
    )

    if manifest:
        lines = [
            "# GRAPHICS.PAK resource catalog",
            f"# Source: @react95/icons {source_version}",
            f"# Entries: {len(entries)}",
            "# name,width,height,source",
        ]
        lines.extend(
            f"{item.name},{item.width},{item.height},{item.filename}"
            for item in selected
        )
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "output", nargs="?", type=Path,
        default=Path("assets/graphics/GRAPHICS.PAK"),
    )
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--png-dir", type=Path)
    source.add_argument("--tarball", type=Path)
    source.add_argument("--download", action="store_true")
    parser.add_argument("--version", default=DEFAULT_VERSION)
    parser.add_argument(
        "--manifest", type=Path,
        default=Path("assets/graphics/GRAPHICS.CSV"),
    )
    args = parser.parse_args()

    if args.png_dir:
        candidates = candidates_from_directory(args.png_dir)
    elif args.tarball:
        candidates = candidates_from_tarball(args.tarball.read_bytes())
    else:
        candidates = candidates_from_tarball(download_tarball(args.version))

    build_package(
        select_canonical(candidates), args.output, args.manifest, args.version,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
