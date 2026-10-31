#!/usr/bin/env python3
"""Run the regression suite after `make test-build`, recording timings and logs."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/test-artifacts/suite"


def run(name, command):
    started = time.monotonic()
    print(f"RUN  {name}", flush=True)
    log = OUTPUT / f"{name}.log"
    with log.open("w") as stream:
        result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    seconds = round(time.monotonic() - started, 2)
    print(f"{'PASS' if result.returncode == 0 else 'FAIL'} {name} ({seconds:.2f}s); {log}", flush=True)
    if result.returncode:
        print(log.read_text(errors="replace")[-6000:], flush=True)
    return {"name": name, "seconds": seconds, "returncode": result.returncode}


def suite():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--cross-prefix", default=str(ROOT / ".tools/cross/bin/i686-elf-"))
    args = parser.parse_args()
    if not 1 <= args.jobs <= 4:
        parser.error("--jobs must be between 1 and 4 (use 1 on a constrained host)")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    results = []
    try:
        # Host FAT checks generate shared read fixtures; packaging creates rum.zip.
        # Finish both before any guest consumes them. Never invoke concurrent make
        # recipes: their build/staging outputs are shared even if guests are not.
        host = run("host", ["make", "-j1", f"CROSS_PREFIX={args.cross_prefix}",
                            "test-host", "test-user", "test-package"])
        results.append(host)
        if host["returncode"]:
            return 1
        suites = [
            ("storage-release", "tests/release-integration-test.py", []),
            ("process-tools", "tests/process-tools-test.py", []),
            ("smoke", "scripts/smoke-test.py", []),
            ("shell", "tests/userspace-shell-test.py", []),
            ("fat-writes", "tests/fat16-write-qemu-test.py", ["--ram", "16", "--faults"]),
            ("fat-reads", "tests/fat16-qemu-test.py", ["--ram", "16"]),
            ("block", "tests/block-qemu-test.py", ["--ram", "16"]),
            ("console", "tests/console-qemu-test.py", []),
            ("integration", "tests/integration-stress-test.py", []),
            ("system-volume", "tests/system-volume-qemu-test.py", []),
            ("lifecycle", "tests/lifecycle-qemu-test.py", []),
            ("sleep", "tests/sleep-qemu-test.py", []),
            ("preemption", "tests/preemption-qemu-test.py", []),
            ("file-syscalls", "tests/file-syscall-qemu-test.py", ["--ram", "16"]),
            ("process-syscalls", "tests/process-syscall-qemu-test.py", []),
        ]
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = [pool.submit(run, name, [sys.executable, script, "--qemu", args.qemu, *extra])
                       for name, script, extra in suites]
            # Keep collecting failures so the report describes the whole run.
            results.extend(future.result() for future in as_completed(futures))
        results.append(run("disk-launcher", [sys.executable, "tests/disk-tools-test.py", "--boot-output-checks"]))
        return int(any(result["returncode"] for result in results))
    finally:
        report = {"passed": len(results) == 17 and all(r["returncode"] == 0 for r in results),
                  "jobs": args.jobs, "seconds": round(time.monotonic() - started, 2),
                  "suites": sorted(results, key=lambda result: result["seconds"], reverse=True)}
        (OUTPUT / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"Suite: {report['seconds']:.2f}s; timings in {OUTPUT / 'result.json'}", flush=True)


def main():
    # On WSL, hundreds of disposable FAT fixtures on /mnt/c or /mnt/f are
    # dominated by DrvFs latency. Keep them on the host's temporary filesystem.
    with tempfile.TemporaryDirectory(prefix="rum-suite-fat-") as directory:
        previous = os.environ.get("RUM_TEST_FAT_IMAGES")
        os.environ["RUM_TEST_FAT_IMAGES"] = directory
        try:
            return suite()
        finally:
            if previous is None:
                os.environ.pop("RUM_TEST_FAT_IMAGES", None)
            else:
                os.environ["RUM_TEST_FAT_IMAGES"] = previous


if __name__ == "__main__":
    sys.exit(main())
