#!/usr/bin/env python3
"""Check RUN/REPLACE validation, nested foreground ownership and resource ledgers."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--qemu", default="qemu-system-i386")
args = parser.parse_args()
artifacts = ROOT / "build/test-artifacts/process-syscalls"
artifacts.mkdir(parents=True, exist_ok=True)
for ram in (16, 64):
    with tempfile.TemporaryDirectory(prefix="rum-process-calls-") as temporary:
        serial = Path(temporary) / "serial"
        process = subprocess.Popen([args.qemu, "-m", f"{ram}M", "-display", "none", "-no-reboot", "-no-shutdown",
            "-serial", f"file:{serial}", "-kernel", str(ROOT / "build/tests/process-syscall.elf")],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 45
            while True:
                text = serial.read_text(errors="replace") if serial.exists() else ""
                if "rum_process_syscall_ok" in text:
                    break
                if "failed" in text or "rum kernel panic" in text or process.poll() is not None or time.monotonic() > deadline:
                    raise AssertionError(f"{ram} MiB: {text[-5000:]}")
                time.sleep(0.02)
        finally:
            if process.poll() is None:
                process.kill(); process.wait(timeout=5)
            if serial.exists():
                (artifacts / f"{ram}m.serial.log").write_bytes(serial.read_bytes())
    print(f"PASS: {ram} MiB ring-3 RUN/REPLACE validation, cross-page packets, nested children, faults, cancellation, cwd/handle/page ledgers", flush=True)
