#!/usr/bin/env python3
"""Check public process lifecycle packets, permissions, orphans and ID limits."""
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
    artifacts = ROOT / "build/test-artifacts/lifecycle"
    artifacts.mkdir(parents=True, exist_ok=True)
    for case in ("lifecycle", "lifecycle-ids"):
        for boot, ram in (("elf", 16), ("iso", 64)):
            with tempfile.TemporaryDirectory(prefix="rum-lifecycle-") as temporary:
                serial = Path(temporary) / "serial"
                image = ROOT / f"build/tests/{case}.{boot}"
                image_args = ["-kernel", str(image)] if boot == "elf" else ["-boot", "d", "-cdrom", str(image)]
                process = subprocess.Popen([
                    args.qemu, "-machine", "pc", "-accel", "tcg", "-m", f"{ram}M", "-display", "none",
                    "-serial", f"file:{serial}", "-no-reboot", "-no-shutdown", *image_args,
                ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                label = f"{case}-{boot}-{ram}m"
                try:
                    deadline = time.monotonic() + 120
                    marker = case.replace("-", "_")
                    while True:
                        text = serial.read_text(errors="replace") if serial.exists() else ""
                        if f"rum_{marker}_ok" in text:
                            break
                        if "_failed:" in text or "rum kernel panic" in text or process.poll() is not None or time.monotonic() > deadline:
                            raise AssertionError(f"{label}: {text[-5000:]}")
                        time.sleep(0.02)
                finally:
                    if process.poll() is None:
                        process.kill()
                    process.wait(timeout=5)
                    (artifacts / f"{label}.qemu.log").write_bytes(process.stderr.read())
                    if serial.exists():
                        (artifacts / f"{label}.serial.log").write_bytes(serial.read_bytes())
            print(f"PASS: {label}: lifecycle requests, ownership, permissions, failed copies, cleanup and limits", flush=True)


if __name__ == "__main__":
    main()
