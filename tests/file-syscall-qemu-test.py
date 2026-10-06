#!/usr/bin/env python3
"""Exercise actual ring-3 filesystem calls and inspect persistent bytes on the host."""
import argparse
import importlib.util
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "build/test-artifacts/file-syscalls"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


launcher = load("file_syscall_launcher", "scripts/run-qemu.py")
smoke = load("file_syscall_smoke", "scripts/smoke-test.py")
images = load("file_syscall_images", "tests/fat16-write-images.py")


def run(qemu, ram, mode="n", disk=None):
    with tempfile.TemporaryDirectory(prefix="rum-file-syscall-qmp-") as temporary:
        serial, monitor = Path(temporary) / "serial.log", Path(temporary) / "qmp"
        arguments = launcher.boot_arguments(ROOT / "build/tests/file-syscall.elf", kernel=True, disk=disk)
        process = subprocess.Popen([qemu, "-m", f"{ram}M", "-display", "none", "-no-reboot", "-no-shutdown",
            "-serial", f"file:{serial}", "-qmp", f"unix:{monitor},server=on,wait=off", *arguments,
            "-append", mode], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 60
            while True:
                text = serial.read_text(errors="replace") if serial.exists() else ""
                if "rum_file_syscall_failed" in text or "rum kernel panic" in text:
                    raise RuntimeError(f"{ram} MiB mode {mode}: {text[-4000:]}")
                if "rum_file_syscall_ok" in text:
                    break
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError(f"{ram} MiB mode {mode}: stopped or timed out: {text[-4000:]}")
                time.sleep(0.02)
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
                connection.settimeout(10)
                connection.connect(str(monitor))
                with connection.makefile("rwb") as stream:
                    json.loads(stream.readline())
                    smoke.qmp_command(stream, "qmp_capabilities")
                    smoke.qmp_command(stream, "quit")
            process.wait(timeout=10)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
            if serial.exists():
                (ARTIFACTS / f"{ram}m-{mode}.serial.log").write_bytes(serial.read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--ram", type=int, choices=(16, 64), default=64)
    args = parser.parse_args()
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    run(args.qemu, args.ram)
    print(f"PASS: QEMU {args.ram} MiB ring-3 handles, validation, cross-page I/O, exhaustion, partial errors, exit/fault/cancel ledgers", flush=True)
    with tempfile.TemporaryDirectory(prefix="rum-file-syscall-volume-") as temporary:
        disk = Path(temporary) / "filesystem.raw"
        images.build(disk)
        images.command("mmd", "-i", str(disk), "::/DOCS")
        source = Path(temporary) / "source"
        source.write_bytes(b"persistent\r\n\0")
        images.command("mcopy", "-i", str(disk), str(source), "::/DOCS/NOTE.TXT")
        before = disk.read_bytes()
        run(args.qemu, args.ram, "r", disk)
        assert disk.read_bytes() == before
        run(args.qemu, args.ram, "d", disk)
        assert images.file_bytes(disk, "/DOCS/NOTE.TXT") == b"userspacet\r\n\0"
        before_geometry = images.fat.geometry(before)
        assert disk.read_bytes()[before_geometry["total"] * 512:] == before[before_geometry["total"] * 512:]
        audit = images.audit(disk)
        assert "/DOCS/CHILD" not in audit["directories"]
        (ARTIFACTS / f"{args.ram}m-fat16.raw").write_bytes(disk.read_bytes())
    print(f"PASS: QEMU {args.ram} MiB userspace FAT16 read/write, cwd, open-object removal, read-only rejection and exact host bytes", flush=True)


if __name__ == "__main__":
    main()
