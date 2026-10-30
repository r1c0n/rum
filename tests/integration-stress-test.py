#!/usr/bin/env python3
"""Mix real ATA/FAT16 I/O, CPU workers, input, timers and process teardown."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "build/test-artifacts/integration-stress"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


images = load("stress_images", "tests/fat16-write-images.py")
smoke = load("stress_smoke", "scripts/smoke-test.py")
launcher = load("stress_launcher", "scripts/run-qemu.py")


def pattern(seed, iteration):
    return bytes(((i * 37) ^ (i >> 7) ^ (seed * 23) ^ (iteration * 11)) & 255 for i in range(4097))


def exercise(qemu, kind, ram):
    label = f"{kind}-{ram}m"
    with tempfile.TemporaryDirectory(prefix="rum-integration-") as temporary:
        directory = Path(temporary)
        disk, serial, monitor = directory / "stress.raw", directory / "serial", directory / "qmp"
        images.build(disk)
        original = disk.read_bytes()
        original_files = images.audit(disk)["files"]
        if os.name == "nt":
            family = socket.AF_INET
            with socket.socket(family, socket.SOCK_STREAM) as reservation:
                reservation.bind(("127.0.0.1", 0)); address = reservation.getsockname()
            qmp = f"tcp:127.0.0.1:{address[1]},server=on,wait=off"
        else:
            family, address = socket.AF_UNIX, str(monitor)
            qmp = f"unix:{monitor},server=on,wait=off"
        image = ROOT / f"build/tests/integration.{kind}"
        # The launcher places the CD on primary IDE, leaving secondary master
        # for the optional ATA disk. QEMU's -cdrom shorthand defaults to index 2.
        arguments = launcher.boot_arguments(image, kernel=kind == "elf", disk=disk, system_image=False)
        process = subprocess.Popen([qemu, "-machine", "pc", "-accel", "tcg", "-m", f"{ram}M", "-display", "none",
            "-serial", f"file:{serial}", "-qmp", qmp, "-no-reboot", "-no-shutdown", *arguments],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        typed = cancelled = 0
        try:
            deadline = time.monotonic() + 180
            while True:
                connection = socket.socket(family, socket.SOCK_STREAM)
                try:
                    connection.connect(address); break
                except (FileNotFoundError, ConnectionRefusedError):
                    connection.close()
                    assert process.poll() is None and time.monotonic() < deadline, "QMP did not start"
                    time.sleep(0.02)
            with connection:
                connection.settimeout(10)
                with connection.makefile("rwb") as stream:
                    assert "QMP" in json.loads(stream.readline())
                    smoke.qmp_command(stream, "qmp_capabilities")
                    send, _, type_text = smoke.guest_keyboard(stream, serial)
                    while True:
                        text = serial.read_text(errors="replace") if serial.exists() else ""
                        if "rum_integration_ok:" in text:
                            break
                        assert "rum_integration_failed:" not in text and "rum kernel panic" not in text and \
                            process.poll() is None and time.monotonic() < deadline, (label, text[-7000:])
                        if text.count("rum_integration_input_ready") > typed:
                            type_text("island\n"); typed += 1
                        if text.count("rum_integration_cancel_ready") > cancelled:
                            send("ctrl", "c"); cancelled += 1
                        time.sleep(0.02)
                    assert typed == cancelled == 4
                    assert text.count("rum_integration_round_ok:") == 16
                    match = re.search(r"rum_integration_ok: (\d+) processes, (\d+) switches", text)
                    assert match and int(match[1]) >= 64 and int(match[2]) >= 100
                    smoke.qmp_command(stream, "quit")
            process.wait(timeout=10)
        finally:
            if process.poll() is None:
                process.kill(); process.wait(timeout=5)
            if serial.exists():
                (ARTIFACTS / f"{label}.serial.log").write_bytes(serial.read_bytes())
            (ARTIFACTS / f"{label}.qemu.log").write_bytes(process.stderr.read())
        final = images.audit(disk)
        for path, data in original_files.items():
            assert final["files"][path] == data, (label, "unrelated file changed", path)
        for name, seed, iteration in (("IOA", 1, 7), ("IOB", 2, 7), ("HOLD", 3, 0), ("FAULT", 4, 0)):
            path = f"/STRESS/{name}.BIN"
            assert images.file_bytes(disk, path) == pattern(seed, iteration), (label, path)
        end = images.fat.geometry(original)["total"] * 512
        assert disk.read_bytes()[end:] == original[end:], "write escaped the FAT volume"
        result = subprocess.run(["fsck.fat", "-n", str(disk)], capture_output=True, text=True, timeout=20)
        (ARTIFACTS / f"{label}.fsck.log").write_text(result.stdout + result.stderr)
        assert result.returncode == 0, (label, result.stdout, result.stderr)
        report = {"processes": int(match[1]), "switches": int(match[2]), "rounds": 16,
                  "disk_sha256": hashlib.sha256(disk.read_bytes()).hexdigest()}
        (ARTIFACTS / f"{label}.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: mixed integration {label}: {report['processes']} processes, {report['switches']} switches, exact FAT bytes, clean fsck and resource baselines", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    args = parser.parse_args()
    if not shutil.which("fsck.fat"):
        parser.error("fsck.fat is required; install dosfstools in Ubuntu/WSL or add its directory to PATH")
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    for kind in ("elf", "iso"):
        for ram in (16, 64):
            exercise(args.qemu, kind, ram)


if __name__ == "__main__":
    main()
