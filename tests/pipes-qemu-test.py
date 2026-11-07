#!/usr/bin/env python3
"""Exercise bounded pipes through real user syscalls, waits and process exits."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    args = parser.parse_args()
    artifacts = ROOT / "build/test-artifacts/pipes"
    artifacts.mkdir(parents=True, exist_ok=True)
    for boot, ram in (("elf", 16), ("iso", 64)):
        with tempfile.TemporaryDirectory(prefix="rum-pipes-") as directory:
            serial = Path(directory) / "serial"
            image = ROOT / f"build/tests/pipes.{boot}"
            image_args = ["-kernel", str(image)] if boot == "elf" else ["-boot", "d", "-cdrom", str(image)]
            process = subprocess.Popen([args.qemu, "-machine", "pc", "-accel", "tcg", "-m", f"{ram}M",
                "-display", "none", "-serial", f"file:{serial}", "-no-reboot", "-no-shutdown", *image_args],
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            try:
                deadline = time.monotonic() + 45
                while True:
                    text = serial.read_text(errors="replace") if serial.exists() else ""
                    if "rum_pipes_ok" in text:
                        break
                    if "_failed:" in text or "rum kernel panic" in text or process.poll() is not None or time.monotonic() > deadline:
                        raise AssertionError(f"{boot}/{ram}m: {text[-6000:]}")
                    time.sleep(.02)
            finally:
                if process.poll() is None:
                    process.kill()
                process.wait(timeout=5)
                (artifacts / f"{boot}-{ram}m.qemu.log").write_bytes(process.stderr.read())
                if serial.exists():
                    (artifacts / f"{boot}-{ram}m.serial.log").write_bytes(serial.read_bytes())
        print(f"PASS: {boot}/{ram}m: binary pipes, atomic writes, endpoint closure, cancellation and cleanup", flush=True)


if __name__ == "__main__":
    main()
