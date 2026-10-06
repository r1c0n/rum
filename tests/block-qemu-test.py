#!/usr/bin/env python3
"""Compare real guest sector bytes and every host canary; inject backend errors."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "build/test-artifacts/block"


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


smoke = load(ROOT / "scripts/smoke-test.py", "smoke")
launcher = load(ROOT / "scripts/run-qemu.py", "launcher")


def run(qemu, label, arguments, marker, *, ram=64, dump=False, allow_readonly_refusal=False):
    serial_artifact = ARTIFACTS / f"{label}.serial.log"
    error_log = ARTIFACTS / f"{label}.qemu.log"
    with tempfile.TemporaryDirectory(prefix="rum-block-") as temporary, error_log.open("wb") as errors:
        serial = Path(temporary) / "serial.log"
        monitor = Path(temporary) / "qmp"
        port = None
        if os.name == "nt":
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as reservation:
                reservation.bind(("127.0.0.1", 0))
                port = reservation.getsockname()[1]
        monitor_spec = f"tcp:127.0.0.1:{port},server=on,wait=off" if port else f"unix:{monitor},server=on,wait=off"
        command = [qemu, "-m", f"{ram}M", "-display", "none", "-no-reboot", "-no-shutdown",
                   "-serial", f"file:{serial}", "-qmp", monitor_spec] + arguments
        process = subprocess.Popen(command, stdout=errors, stderr=errors,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        connection = socket.socket(socket.AF_INET if port else socket.AF_UNIX, socket.SOCK_STREAM)
        connection.settimeout(5)
        stream = None
        try:
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    diagnostic = error_log.read_text(errors="replace")
                    if allow_readonly_refusal and "read-only" in diagnostic.lower():
                        print(f"{label}: QEMU refused a read-only IDE backend (image preserved)")
                        return None
                    raise RuntimeError(f"{label}: QEMU exited: {diagnostic}")
                try:
                    connection.connect(("127.0.0.1", port) if port else str(monitor))
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    pass
                time.sleep(0.02)
            else:
                raise RuntimeError(f"{label}: monitor timed out")
            stream = connection.makefile("rwb")
            json.loads(stream.readline())
            smoke.qmp_command(stream, "qmp_capabilities")
            while time.monotonic() < deadline:
                text = serial.read_text(errors="replace") if serial.exists() else ""
                if "rum_block_failed" in text:
                    raise RuntimeError(f"{label}: {text}")
                if marker in text:
                    break
                if process.poll() is not None:
                    raise RuntimeError(f"{label}: QEMU exited: {error_log.read_text()}")
                time.sleep(0.02)
            else:
                raise RuntimeError(f"{label}: timed out; see {serial}")
            if marker == "rum_boot_ok":
                _, expect, type_text = smoke.guest_keyboard(stream, serial)
                type_text("echo disk check\n")
                expect("\r\ndisk check\r\n> ")
            smoke.qmp_command(stream, "stop")
            data = None
            if dump:
                address = smoke.elf_symbols(ROOT / "build/tests/block.elf")["block_test_data"]
                data = smoke.dump_ram(stream, address, 1024 * 512, ARTIFACTS / f"{label}.guest.bin")
            smoke.qmp_command(stream, "quit")
            process.wait(timeout=5)
            print(f"{label}: passed")
            return data
        except (OSError, RuntimeError):
            if allow_readonly_refusal:
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    raise
                if "read-only" in error_log.read_text(errors="replace").lower():
                    print(f"{label}: QEMU refused a read-only IDE backend (image preserved)")
                    return None
            raise
        finally:
            if stream: stream.close()
            connection.close()
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
            if serial.exists(): serial_artifact.write_bytes(serial.read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--ram", type=int, choices=(16, 64), default=64)
    args = parser.parse_args()
    def execute(label, arguments, marker, **kwargs):
        return run(args.qemu, f"{args.ram}m-{label}", arguments, marker, ram=args.ram, **kwargs)
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    fixture = ROOT / "build/tests/block.elf"
    before = bytes((sector * 13 + byte * 7) & 255 for sector in range(1024) for byte in range(512))
    after = bytearray(before)
    for sector in [0, 1023] + list(range(13, 270)):
        after[sector * 512:(sector + 1) * 512] = bytes((0xa5 ^ (sector * 11 + byte * 3)) & 255 for byte in range(512))
    with tempfile.TemporaryDirectory(prefix="rum-block-media-") as temporary:
        disk = Path(temporary) / "test disk,patterns.raw"
        disk.write_bytes(before)
        base = launcher.boot_arguments(fixture, kernel=True)
        attached = launcher.drive(disk, index=2, media="disk")
        assert execute("read", base + ["-append", "read"] + attached, "rum_block_ok", dump=True) == before
        assert disk.read_bytes() == before
        assert execute("write", base + ["-append", "write"] + attached, "rum_block_ok", dump=True) == after
        assert disk.read_bytes() == after, "write changed a canary or failed to persist"
        assert execute("reboot-read", base + ["-append", "read"] + attached, "rum_block_ok", dump=True) == after
        assert disk.read_bytes() == after
        execute("missing", base + ["-append", "missing"], "rum_block_ok")
        disk.write_bytes(before)
        readonly = launcher.drive(disk, index=2, media="disk", read_only=True)
        # QEMU releases that forbid read-only IDE disks must reject them before
        # boot; others return guest-visible ATA write errors. Neither may write.
        result = execute("readonly", base + ["-append", "write-error"] + readonly,
                     "rum_block_ok", dump=True, allow_readonly_refusal=True)
        assert result is None or result == before
        assert disk.read_bytes() == before
        for operation, iotype, errno in [("read", "read", 5), ("write", "write", 5),
                                          ("write-protected", "write", 13), ("flush", "flush", 5)]:
            config = Path(temporary) / "error.conf"
            once = "off" if operation == "read" else "on"
            # BIOS may inspect sector zero before a direct Multiboot handoff.
            # Keep its read failure armed; recovery is verified on sector one.
            sector = 'sector = "0"\n' if operation == "read" else ""
            config.write_text(f'[inject-error]\nevent = "none"\niotype = "{iotype}"\nerrno = "{errno}"\nonce = "{once}"\nimmediately = "on"\n{sector}')
            node = {"driver": "raw", "node-name": "rum-test-disk", "file": {
                "driver": "blkdebug", "config": str(config), "image": {"driver": "file", "filename": str(disk)}}}
            device = ["-blockdev", json.dumps(node), "-device",
                      "ide-hd,drive=rum-test-disk,bus=ide.1,unit=0,werror=report,rerror=report"]
            mode = "write-error" if operation == "write-protected" else operation + "-error"
            data = execute(operation + "-error", base + ["-append", mode] + device, "rum_block_ok", dump=True)
            expected = bytes([0xab]) * 512 + before[512:] if operation == "read" else before
            assert data == expected and disk.read_bytes() == before, f"{operation}: damaged canaries"
        # Exercise the actual launcher layout with both production boot paths.
        for kernel in (False, True):
            image = ROOT / ("build/rum.elf" if kernel else "build/rum.iso")
            for present in (False, True):
                options = launcher.boot_arguments(image, kernel, disk if present else None)
                label = ("elf" if kernel else "iso") + ("-disk" if present else "-missing")
                execute(label, options, "rum_boot_ok")
                log = (ARTIFACTS / f"{args.ram}m-{label}.serial.log").read_text()
                assert ("rum_disk: ok" if present else "rum_disk: no device") in log
                assert disk.read_bytes() == before, "normal boot modified the disk"
    print("QEMU block tests passed: exact bytes, persistence, canaries and errors")


if __name__ == "__main__":
    main()
