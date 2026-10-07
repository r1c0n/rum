#!/usr/bin/env python3
"""Reject malformed system modules while retaining the RAM recovery console."""
import argparse
import importlib.util
from pathlib import Path
import struct
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("system_console", ROOT / "tests/userspace-shell-test.py")
console = importlib.util.module_from_spec(spec)
spec.loader.exec_module(console)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--valid-only", action="store_true", help="check exact system file bytes only")
    args = parser.parse_args()
    console.ARTIFACTS.mkdir(parents=True, exist_ok=True)
    with console.boot(args.qemu, 64, "system-valid-files") as guest:
        executable = (ROOT / "build/user/system/hello.elf").read_bytes()
        displayed = "".join(chr(byte) if 32 <= byte <= 126 or byte in (9, 10)
                            else f"\\x{byte:02X}" for byte in executable)
        displayed = displayed.replace("\n", "\r\n") + ("\r\n" if executable[-1] != 10 else "")
        start = guest.serial.stat().st_size
        guest.type("cat /rum/hello.elf\n")
        guest.wait(displayed + "/> ", timeout=20)
        assert guest.serial.read_bytes()[start:].decode() == "cat /rum/hello.elf\r\n" + displayed + "/> "
    print("PASS: exact binary bytes through system file handles, chunk boundaries and EOF", flush=True)
    if args.valid_only:
        return
    original = (ROOT / "build/rum-system.img").read_bytes()
    cases = {"short": original[:31], "truncated-index": original[:64],
             "truncated-data": original[:-1], "trailing": original + b"\0" * 4,
             "oversized": original + bytes(1024 * 1024)}
    for label, offset, value in (("version", 8, 2), ("size", 12, 0), ("empty", 16, 0),
        ("count", 16, 65), ("entry-size", 20, 79), ("header-size", 24, 31), ("reserved", 28, 1),
        ("offset", 96, 0xFFFFFFFC), ("overlap", 176, 0), ("file-size", 100, 65537),
        ("entry-reserved", 104, 1)):
        data = bytearray(original); struct.pack_into("<I", data, offset, value); cases[label] = data
    data = bytearray(original); data[0] ^= 1; cases["magic"] = data
    data = bytearray(original); data[32:96] = b"x" * 64; cases["unterminated-name"] = data
    data = bytearray(original); data[32] = ord("/"); cases["invalid-name"] = data
    data = bytearray(original); data[95] = 1; cases["name-padding"] = data
    data = bytearray(original); data[112:176] = data[32:96]; cases["duplicate"] = data
    with tempfile.TemporaryDirectory(prefix="rum-system-fixtures-") as temporary:
        image = Path(temporary) / "system.img"
        for label, data in cases.items():
            image.write_bytes(data)
            with console.boot(args.qemu, 16, "system-" + label, system_image=image,
                              marker="Cannot load the userspace shell.") as guest:
                assert "rum_system: invalid request" in guest.serial.read_text()
                guest.command("cat readme.txt", "rum")
        with console.boot(args.qemu, 16, "system-missing", system_image=False,
                          marker="Cannot load the userspace shell.") as guest:
            assert "rum_system: unavailable" in guest.serial.read_text()
            guest.command("echo RAM recovery works", "RAM recovery works")
    print(f"PASS: {len(cases)} malformed system modules and missing module retain usable RAM recovery", flush=True)


if __name__ == "__main__":
    main()
