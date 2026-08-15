#!/usr/bin/env python3
"""Create the FAT fixture containing the standalone extra programs."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


PROGRAMS = (
    ("CODEX.O", "codex"),
    ("HYPERZIP.O", "hyperzip"),
    ("VIEWER.O", "viewer"),
    ("LEXINET.O", "lexinet"),
    ("CIV2.O", "civ2"),
)


def run(*command: str) -> None:
    subprocess.run(command, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build a FAT image containing standalone BlesKernOS programs."
    )
    parser.add_argument("image", type=Path)
    for _, arg_name in PROGRAMS:
        parser.add_argument(arg_name, type=Path)
    args = parser.parse_args()

    for command in ("mkfs.fat", "mcopy", "mmd"):
        if shutil.which(command) is None:
            raise SystemExit(f"Missing {command}; install the mtools and dosfstools packages.")

    sources = {destination: getattr(args, arg_name) for destination, arg_name in PROGRAMS}
    for destination, source in sources.items():
        if not source.is_file() or source.stat().st_size == 0:
            raise SystemExit(f"Missing or empty {destination} input: {source}")

    image = args.image.resolve()
    image.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(dir=image.parent, suffix=".img")
    os.close(fd)
    temporary_image = Path(temporary_name)
    temporary_image.unlink()

    try:
        # 16 MiB is enough for these programs and is a widely supported FAT16 size.
        run("mkfs.fat", "-C", "-F", "16", "-n", "BKEXTRAS", str(temporary_image), "16384")
        run("mmd", "-i", str(temporary_image), "::/SYSTEM")
        run("mmd", "-i", str(temporary_image), "::/SYSTEM/PROGRAMS")
        for destination, _ in PROGRAMS:
            run("mcopy", "-o", "-i", str(temporary_image),
                str(sources[destination]), f"::/SYSTEM/PROGRAMS/{destination}")
        temporary_image.replace(image)
    finally:
        temporary_image.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
