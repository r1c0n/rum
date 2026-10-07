#!/usr/bin/env python3
"""Read host FAT images through real ATA PIO and compare guest bytes/listings."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "build/test-artifacts/fat16"


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


smoke = load(ROOT / "scripts/smoke-test.py", "fat_smoke")
launcher = load(ROOT / "scripts/run-qemu.py", "fat_launcher")


def run(qemu, ram, label, arguments, marker, case=None):
    with tempfile.TemporaryDirectory(prefix="rum-fat-") as temporary:
        serial = Path(temporary) / "serial.log"
        monitor = Path(temporary) / "qmp"
        process = subprocess.Popen([qemu, "-m", f"{ram}M", "-display", "none", "-no-reboot", "-no-shutdown",
            "-serial", f"file:{serial}", "-qmp", f"unix:{monitor},server=on,wait=off", *arguments],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 30
            while True:
                text = serial.read_text(errors="replace") if serial.exists() else ""
                if "rum_fat_failed" in text or "rum kernel panic" in text:
                    raise RuntimeError(f"{label}: {text}")
                if marker in text:
                    break
                if process.poll() is not None:
                    raise RuntimeError(f"{label}: {process.stderr.read().decode(errors='replace')}")
                if time.monotonic() > deadline:
                    raise RuntimeError(f"{label}: timeout: {text}")
                time.sleep(0.02)
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
                connection.settimeout(20)
                connection.connect(str(monitor))
                with connection.makefile("rwb") as stream:
                    json.loads(stream.readline())
                    smoke.qmp_command(stream, "qmp_capabilities")
                    if case is None:
                        _, expect, type_text = smoke.guest_keyboard(stream, serial)
                        type_text("echo disk is usable\n")
                        expect("\r\ndisk is usable\r\n/> ")
                        if "valid" in label:
                            assert "rum_fat16: ok\n" in text
                        else:
                            assert "rum_fat16: ok\n" not in text
                    else:
                        assert f"rum_fat_result: {case['phase']} {case['error']}\n" in text, text
                    smoke.qmp_command(stream, "stop")
                    if case and case["phase"] == "valid":
                        symbols = smoke.elf_symbols(ROOT / "build/tests/fat16.elf")
                        data = smoke.dump_ram(stream, symbols["fat16_test_data"], 10037, ARTIFACTS / f"{label}.bytes")
                        image = ROOT / "build/tests/fat16" / case["image"]
                        expected = subprocess.run(["mcopy", "-i", str(image), "::/BINARY.BIN", "-"],
                                                  check=True, capture_output=True).stdout
                        assert data == expected, f"{label}: guest binary bytes differ from host FAT read"
                        listing = smoke.dump_ram(stream, symbols["fat16_test_listing"], 8192,
                                                 ARTIFACTS / f"{label}.listing").split(b"\0", 1)[0]
                        assert sorted(listing.decode().splitlines()) == case["listing"], label
                    smoke.qmp_command(stream, "quit")
            process.wait(timeout=5)
            print(f"PASS: {label}", flush=True)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
            if serial.exists():
                (ARTIFACTS / f"{label}.serial.log").write_bytes(serial.read_bytes())


def exercise(qemu, ram, directory):
    cases = json.loads((directory / "manifest.json").read_text())
    for case in cases:
        image = directory / case["image"]
        before = hashlib.sha256(image.read_bytes()).digest()
        arguments = launcher.boot_arguments(ROOT / "build/tests/fat16.elf", kernel=True, disk=image)
        arguments += ["-append", case["phase"] + " " + case["path"]]
        run(qemu, ram, f"{ram}m-{case['name']}", arguments, "rum_fat_ok", case)
        assert hashlib.sha256(image.read_bytes()).digest() == before, "guest wrote the read-only volume"
    for kernel in (False, True):
        for name in ("valid", "signature"):
            image = directory / f"{name}.raw"
            before = hashlib.sha256(image.read_bytes()).digest()
            boot = ROOT / ("build/rum.elf" if kernel else "build/rum.iso")
            arguments = launcher.boot_arguments(boot, kernel=kernel, disk=image)
            run(qemu, ram, f"{ram}m-{'elf' if kernel else 'iso'}-{name}", arguments, "rum_boot_ok")
            assert hashlib.sha256(image.read_bytes()).digest() == before


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--ram", type=int, choices=(16, 64), default=64)
    args = parser.parse_args()
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    # DrvFs latency can consume a bounded ATA poll even for small reads while
    # parallel builds run. Keep guest I/O on Linux's temporary filesystem and
    # compare both staged and source images; never retry a failed guest check.
    source = ROOT / "build/tests/fat16"
    with tempfile.TemporaryDirectory(prefix="rum-fat16-read-volume-") as temporary:
        directory = Path(temporary)
        (directory / "manifest.json").write_bytes((source / "manifest.json").read_bytes())
        originals = {}
        for image in source.glob("*.raw"):
            data = image.read_bytes()
            originals[image.name] = hashlib.sha256(data).digest()
            (directory / image.name).write_bytes(data)
        exercise(args.qemu, args.ram, directory)
        for name, digest in originals.items():
            assert hashlib.sha256((source / name).read_bytes()).digest() == digest
            assert hashlib.sha256((directory / name).read_bytes()).digest() == digest


if __name__ == "__main__":
    main()
