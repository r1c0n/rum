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
    selected = module.pack(directory, ["a.elf"])
    assert struct.unpack_from("<I", selected, 16)[0] == 1 and selected.endswith(b"\x7fELF")
    for names in (("a.elf", "a.elf"), ("../a.elf",), (".",), ("missing.elf",)):
        try:
            module.pack(directory, names)
        except (ValueError, OSError):
            pass
        else:
            raise AssertionError("invalid explicit manifest accepted")
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
    for index in range(16):
        (directory / f"file{index}").write_bytes(bytes(65536))
    rejected(directory)  # Total archive limit, independent of per-file/count limits.
    for index in range(64):
        (directory / f"file{index}").write_bytes(b"")
    rejected(directory)

image = (ROOT / "build/rum-system.img").read_bytes()
assert image[:8] == b"RUMSYS1\0"
count = struct.unpack_from("<I", image, 16)[0]
kernel_bytes = (ROOT / "build/rum.elf").read_bytes()
installed = {}
for index in range(count):
    name, offset, size, first, second = struct.unpack_from("<64s4I", image, 32 + index * 80)
    name = name.split(b"\0", 1)[0].decode("ascii")
    expected = (ROOT / "build/user/system" / name).read_bytes()
    assert not first and not second and image[offset:offset + size] == expected
    assert expected not in kernel_bytes, f"user ELF embedded in kernel: {name}"
    installed[name] = expected
assert "shell.elf" in installed and "cat.elf" in installed and "cd.elf" in installed
kernel = (ROOT / "build/embedded-files.c").read_text()
assert "shell.elf" not in kernel and "hello.elf" not in kernel
print("PASS: separate system image, exact ELF bytes, explicit manifest, deterministic layout and build limits")
