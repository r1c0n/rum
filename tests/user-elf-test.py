#!/usr/bin/env python3
"""Check built user assets and reject malformed ELF/stripping inputs."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cross-prefix", required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
asset = root / "build/user/ramfs/hello.elf"
debug = root / "build/user/debug/hello.elf"
checker = root / "build/tools/check-user-elf"
original = asset.read_bytes()

def check(path, valid, debug_path=None):
    command = [str(checker), str(path)] + ([str(debug_path)] if debug_path else [])
    result = subprocess.run(command, capture_output=True, text=True)
    assert (result.returncode == 0) == valid, result.stdout + result.stderr

check(asset, True, debug)
private_include = subprocess.run(
    [args.cross_prefix + "gcc", "-ffreestanding", "-Iuser/include", "-Ibuild/user/include",
     "-x", "c", "-fsyntax-only", "-"], input="#include <rum/heap.h>\n",
    capture_output=True, text=True, cwd=root)
assert private_include.returncode != 0, "private kernel headers exposed to user build"
symbols = subprocess.check_output([args.cross_prefix + "nm", str(debug)], text=True)
assert all(name in symbols for name in (" _start", " main", " rum_syscall3"))
assert "kernel_main" not in symbols
assert not subprocess.check_output([args.cross_prefix + "nm", "--undefined-only", str(debug)])
debug_sections = subprocess.check_output([args.cross_prefix + "readelf", "-S", str(debug)], text=True)
asset_sections = subprocess.check_output([args.cross_prefix + "readelf", "-S", str(asset)], text=True)
assert ".debug_info" in debug_sections and ".symtab" in debug_sections
assert ".debug_info" not in asset_sections and ".symtab" not in asset_sections
phoff = struct.unpack_from("<I", original, 28)[0]
phnum = struct.unpack_from("<H", original, 44)[0]
headers = [struct.unpack_from("<8I", original, phoff + 32 * i) for i in range(phnum)]
loads = [i for i, p in enumerate(headers) if p[0] == 1 and p[5]]
stack = next(i for i, p in enumerate(headers) if p[0] == 0x6474E551)
first = phoff + loads[0] * 32
second = phoff + loads[1] * 32
stack_offset = phoff + stack * 32
cases = 0
with tempfile.TemporaryDirectory(prefix="rum-user-elf-") as temporary:
    bad = Path(temporary) / "case.elf"

    def mutation(offset, fmt, value, *, compare=False):
        global cases
        data = bytearray(original)
        struct.pack_into(fmt, data, offset, value)
        bad.write_bytes(data)
        check(bad, False, debug if compare else None)
        cases += 1

    for offset, fmt, value in (
        (4, "B", 2), (5, "B", 2), (7, "B", 3), (16, "<H", 3),
        (18, "<H", 62), (20, "<I", 0), (24, "<I", 0), (28, "<I", 0xFFFFFFF0),
        (40, "<H", 51), (42, "<H", 31), (44, "<H", 0), (44, "<H", 17),
        (first, "<I", 3), (first + 4, "<I", 0xFFFFFFF0),
        (first + 8, "<I", 0), (first + 8, "<I", 0xBFC00000),
        (first + 16, "<I", headers[loads[0]][5] + 1),
        (first + 20, "<I", 0x01000000), (first + 20, "<I", 0xFFFFFFFF),
        (first + 24, "<I", 7), (first + 28, "<I", 3),
        (first + 4, "<I", headers[loads[0]][1] + 1),
        (second + 8, "<I", headers[loads[0]][2]),
        (stack_offset, "<I", 0), (stack_offset + 24, "<I", 7),
    ):
        mutation(offset, fmt, value)
    for length in (0, 51, phoff + 32 * phnum - 1):
        bad.write_bytes(original[:length])
        check(bad, False)
        cases += 1
    bad.write_bytes(original + bytes(65537 - len(original)))
    check(bad, False)
    cases += 1
    bad.write_bytes(original + bytes(65536 - len(original)))
    check(bad, True, debug)  # Exact embedding boundary; trailing bytes are unused.
    text_offset = headers[loads[0]][1]
    mutation(text_offset, "B", original[text_offset] ^ 1, compare=True)
print(f"PASS: static user ELF32, isolated headers, separate debug symbols, unchanged load image and {cases} rejected inputs")
