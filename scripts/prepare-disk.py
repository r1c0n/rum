#!/usr/bin/env python3
"""Create a new FAT16 system disk. Existing images are never modified."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True, capture_output=True, timeout=120)


def prepare(path, size_mib=16, programs=None):
    path = Path(path)
    if not 16 <= size_mib <= 128:
        raise ValueError("system disk size must be between 16 and 128 MiB")
    for tool in ("mformat", "mcopy", "mmd"):
        if not shutil.which(tool):
            raise ValueError(f"missing {tool}; install mtools in Ubuntu/WSL")
    directory = ROOT / "build/user/system"
    if programs is None:
        programs = (ROOT / "build/user/programs.txt").read_text().splitlines()
    if not programs or len(programs) != len({name.upper() for name in programs}):
        raise ValueError("missing or duplicate system programs; run make user")
    for name in programs:
        if not re.fullmatch(r"[A-Za-z0-9_-]{1,8}\.elf", name):
            raise ValueError(f"invalid 8.3 program name: {name}")
        source = directory / name
        if source.is_symlink() or not source.is_file() or not 0 < source.stat().st_size <= 65536:
            raise ValueError(f"missing or invalid program: {name}; run make user")
    if "shell.elf" not in programs:
        raise ValueError("system programs must include shell.elf")
    # Exclusive creation refuses existing files and symlinks. Failure cleanup
    # removes only the image created by this invocation, never an existing disk.
    with path.open("xb") as image:
        image.truncate(size_mib * 1024 * 1024)
    try:
        # FAT requires a power-of-two sector count per cluster, including
        # disk sizes such as 24 MiB that fall between the usual powers of two.
        cluster_sectors = 2
        while cluster_sectors * 8 < size_mib:
            cluster_sectors *= 2
        run("mformat", "-i", path, "-T", size_mib * 2048, "-c", cluster_sectors, "-v", "RUM", "::")
        run("mmd", "-i", path, "::/RUM")
        for name in programs:
            run("mcopy", "-i", path, directory / name, "::/RUM/" + name.upper())
        for name in ("readme.txt", "welcome.txt"):
            run("mcopy", "-i", path, ROOT / "assets/disk" / name, "::/" + name.upper())
    except BaseException:
        path.unlink()
        raise
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="new raw disk image; parent must exist")
    parser.add_argument("--size-mib", type=int, default=16)
    args = parser.parse_args()
    try:
        prepare(args.path, args.size_mib)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"rum disk: {error}", file=sys.stderr)
        return 1
    print(f"Created {args.path}; attach it as the secondary IDE disk when starting rum.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
