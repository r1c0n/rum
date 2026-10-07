#!/usr/bin/env python3
"""Pack freestanding programs into rum's separate, read-only boot volume."""
import argparse
from pathlib import Path
import re
import struct

HEADER = struct.Struct("<8s6I")
ENTRY = struct.Struct("<64s4I")
IMAGE_LIMIT = 1024 * 1024


def pack(directory):
    directory = Path(directory)
    files = sorted(directory.iterdir(), key=lambda path: path.name)
    if not 1 <= len(files) <= 64:
        raise ValueError("system volume needs 1..64 regular files")
    index, data = bytearray(), bytearray()
    offset = HEADER.size + ENTRY.size * len(files)
    for path in files:
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"not a regular file: {path}")
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,63}", path.name) or path.name in (".", ".."):
            raise ValueError(f"invalid filename: {path.name}")
        content = path.read_bytes()
        if len(content) > 65536:
            raise ValueError(f"file exceeds 64 KiB: {path.name}")
        index += ENTRY.pack(path.name.encode("ascii"), offset, len(content), 0, 0)
        data += content
        data += b"\0" * (-len(content) % 4)
        offset = HEADER.size + len(files) * ENTRY.size + len(data)
    if offset > IMAGE_LIMIT:
        raise ValueError("system volume exceeds 1 MiB")
    return HEADER.pack(b"RUMSYS1\0", 1, offset, len(files), ENTRY.size, HEADER.size, 0) + index + data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        data = pack(args.directory)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        if not args.output.exists() or args.output.read_bytes() != data:
            temporary = args.output.with_suffix(args.output.suffix + ".tmp")
            temporary.write_bytes(data)
            temporary.replace(args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, f"rum: {error}\n")


if __name__ == "__main__":
    main()
