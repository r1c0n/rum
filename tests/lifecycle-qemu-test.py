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
    focus = parser.add_mutually_exclusive_group()
    focus.add_argument("--objects-only", action="store_true", help="Run only common object ownership checks")
    focus.add_argument("--vm-only", action="store_true", help="Run only private memory-region lifecycle checks")
    focus.add_argument("--runtime-only", action="store_true", help="Run only userspace heap/runtime checks")
    focus.add_argument("--executable-only", action="store_true", help="Run only ELF loading and large-executable checks")
    args = parser.parse_args()
    artifacts = ROOT / "build/test-artifacts/lifecycle"
    artifacts.mkdir(parents=True, exist_ok=True)
    if args.objects_only:
        cases = ("objects",)
    elif args.executable_only:
        cases = ("elf-loader", "large-loader")
    elif args.runtime_only:
        cases = ("runtime",)
    elif args.vm_only:
        cases = ("vm-regions",)
    else:
        cases = ("lifecycle", "lifecycle-ids", "vm-regions", "runtime", "large-loader", "objects")
    for case in cases:
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
                    marker = "elf_loader_test" if case == "elf-loader" else case.replace("-", "_")
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
            coverage = {
                "objects": "retained operations, retryable final close, IRQ/FS/block guards, yielding destruction and cleanup",
                "large-loader": "large streamed segments, BSS, parent descriptors, injected I/O/OOM and cleanup",
                "elf-loader": "ELF validation, permissions, guarded stacks and allocation rollback",
                "runtime": "userspace heap, file streams, clock, isolation, OOM and exit/fault/cancel cleanup",
                "vm-regions": "private regions, zeroing, isolation, OOM, quota and exit/fault/cancel cleanup",
            }.get(case, "lifecycle requests, ownership, permissions, failed copies, cleanup and limits")
            print(f"PASS: {label}: {coverage}", flush=True)


if __name__ == "__main__":
    main()
