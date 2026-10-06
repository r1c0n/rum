#!/usr/bin/env python3
"""Write through real ATA PIO; validate disk bytes after QEMU exits."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "build/test-artifacts/fat16-write"


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


images = load("fat_write_images", "tests/fat16-write-images.py")
host = load("fat_write_host", "tests/fat16-write-host-test.py")
smoke = load("fat_write_smoke", "scripts/smoke-test.py")
launcher = load("fat_write_launcher", "scripts/run-qemu.py")


def run(qemu, ram, image, mode="exercise", fail=-1, tear=0):
    label = f"{ram}m-{image.stem}-{mode}-{fail}"
    with tempfile.TemporaryDirectory(prefix="rum-fat-write-qmp-") as temporary:
        serial, monitor = Path(temporary) / "serial.log", Path(temporary) / "qmp"
        arguments = launcher.boot_arguments(ROOT / "build/tests/fat16-write.elf", kernel=True, disk=image)
        process = subprocess.Popen([qemu, "-m", f"{ram}M", "-display", "none", "-no-reboot", "-no-shutdown",
            "-serial", f"file:{serial}", "-qmp", f"unix:{monitor},server=on,wait=off", *arguments,
            "-append", f"{mode} {fail} {tear}"], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 60
            while True:
                text = serial.read_text(errors="replace") if serial.exists() else ""
                if "rum_fat_write_failed" in text or "rum kernel panic" in text:
                    raise RuntimeError(f"{label}: {text[-4000:]}")
                if "rum_fat_write_ok" in text:
                    break
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError(f"{label}: stopped or timed out: {text[-4000:]}")
                time.sleep(0.02)
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
                connection.settimeout(10)
                connection.connect(str(monitor))
                with connection.makefile("rwb") as stream:
                    json.loads(stream.readline())
                    smoke.qmp_command(stream, "qmp_capabilities")
                    smoke.qmp_command(stream, "quit")
            process.wait(timeout=10)
            events = re.findall(r"rum_fat_write_event: ([^\n]+)", text)
            return events
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
            if serial.exists():
                (ARTIFACTS / f"{label}.serial.log").write_bytes(serial.read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--ram", type=int, choices=(16, 64), default=64)
    parser.add_argument("--faults", action="store_true")
    args = parser.parse_args()
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    for cluster in (1, 2, 8):
        # Use Linux's temporary filesystem for QEMU I/O rather than a WSL
        # mounted Windows path; retain the validated final image as an artifact.
        with tempfile.TemporaryDirectory(prefix="rum-fat-write-volume-") as temporary:
            image = Path(temporary) / f"qemu-{args.ram}m-cluster{cluster}.raw"
            images.build(image, cluster)
            before = image.read_bytes()
            events = run(args.qemu, args.ram, image)
            host.canaries(before, image.read_bytes(), events)
            saved = image.read_bytes()
            assert not run(args.qemu, args.ram, image, "verify") and image.read_bytes() == saved
            images.verify_outputs(image)
            shutil.copyfile(image, ARTIFACTS / image.name)
        print(f"PASS: QEMU {args.ram} MiB FAT16 writes, reboot reads and host validation, cluster={cluster}", flush=True)
    with tempfile.TemporaryDirectory(prefix="rum-fat-write-guests-") as temporary:
        base = Path(temporary) / "base.raw"
        images.build(base)
        original = base.read_bytes()
        baseline = images.audit(base)
        work = Path(temporary) / "failure.raw"
        work.write_bytes(original)
        assert not run(args.qemu, args.ram, work, "readonly") and work.read_bytes() == original
        print(f"PASS: QEMU {args.ram} MiB read-only block device", flush=True)
        if args.faults:
            for mode in ("replace", "delete", "mkdir", "growdir", "growmkdir"):
                work.write_bytes(original)
                events = run(args.qemu, args.ram, work, mode)
                images.audit(work)
                host.ordering(original, work.read_bytes(), events, mode)
                for stage in range(len(events)):
                    work.write_bytes(original)
                    trace = run(args.qemu, args.ram, work, mode, stage, 256)
                    assert len(trace) == stage + 1
                    data = work.read_bytes()
                    host.canaries(original, data, trace)
                    host.keep_canary(original, data, baseline)
                print(f"PASS: QEMU {args.ram} MiB {mode}: all {len(events)} write/flush interruptions", flush=True)


if __name__ == "__main__":
    main()
