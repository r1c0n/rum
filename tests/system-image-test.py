#!/usr/bin/env python3
"""Check the independent userspace archive format and its build limits."""
import importlib.util
from pathlib import Path
import struct
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("system_image", ROOT / "scripts/pack-system.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def rejected(directory):
    try:
        module.pack(directory)
    except (ValueError, OSError):
        return
    raise AssertionError("invalid system directory accepted")


with tempfile.TemporaryDirectory(prefix="rum-system-") as temporary:
    directory = Path(temporary)
    rejected(directory)
    (directory / "z.txt").write_bytes(b"last\x00\xff")
    (directory / "a.elf").write_bytes(b"\x7fELF")
    data = module.pack(directory)
    assert module.pack(directory) == data
    assert struct.unpack_from("<8s6I", data) == (b"RUMSYS1\0", 1, len(data), 2, 80, 32, 0)
    for index, name, expected in ((0, b"a.elf", b"\x7fELF"), (1, b"z.txt", b"last\x00\xff")):
        entry, offset, size, first, second = struct.unpack_from("<64s4I", data, 32 + index * 80)
        assert entry.rstrip(b"\0") == name and not first and not second
        assert offset % 4 == 0 and data[offset:offset + size] == expected
    (directory / "z.txt").write_bytes(bytes(65536))
    module.pack(directory)
    (directory / "z.txt").write_bytes(bytes(65537))
    rejected(directory)
    (directory / "z.txt").unlink()
    (directory / "invalid name").write_bytes(b"")
    rejected(directory)
    (directory / "invalid name").unlink()
    (directory / ("x" * 64)).write_bytes(b"")
    rejected(directory)
    (directory / ("x" * 64)).unlink()
    (directory / "nested").mkdir()
    rejected(directory)
    (directory / "nested").rmdir()
    (directory / "link").symlink_to(directory / "a.elf")
    rejected(directory)
    (directory / "link").unlink()
    for index in range(64):
        (directory / f"file{index}").write_bytes(b"")
    rejected(directory)

image = (ROOT / "build/rum-system.img").read_bytes()
assert image[:8] == b"RUMSYS1\0"
kernel = (ROOT / "build/embedded-files.c").read_text()
assert "shell.elf" not in kernel and "hello.elf" not in kernel
print("PASS: deterministic separate system image, exact bytes, names, sizes, regular files and count limits")
