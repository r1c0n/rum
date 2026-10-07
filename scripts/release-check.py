#!/usr/bin/env python3
"""Build and test a committed source snapshot without deleting local build/disks."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def capture(*args):
    return subprocess.check_output(args, cwd=ROOT).decode().strip()


def run(command, cwd, log):
    print("Running: " + " ".join(command), flush=True)
    with log.open("w") as output:
        result = subprocess.run(command, cwd=cwd, stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"Command exited {result.returncode}; see {log}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cross-prefix", default=str(ROOT / ".tools/cross/bin/i686-elf-"))
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if os.name == "nt":
        parser.error("Run this build check in Ubuntu/WSL; the package test also runs on Windows.")
    if not 1 <= args.jobs <= 64:
        parser.error("--jobs must be between 1 and 64")
    if capture("git", "status", "--porcelain"):
        parser.error("Commit source changes before checking the exact release snapshot.")
    commit = capture("git", "rev-parse", "HEAD")
    output = ROOT / "build/release-check" / (commit[:12] + "-" + time.strftime("%Y%m%d-%H%M%S"))
    output.mkdir(parents=True, exist_ok=True)
    report = {"commit": commit, "passed": False}
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="rum-release-build-") as temporary:
        work = Path(temporary)
        try:
            snapshot = subprocess.check_output(["git", "archive", "--format=tar", commit], cwd=ROOT)
            with tarfile.open(fileobj=io.BytesIO(snapshot)) as archive:
                for member in archive.getmembers():
                    target = work / member.name
                    if not target.resolve().is_relative_to(work.resolve()):
                        raise RuntimeError(f"Unsupported snapshot path: {member.name}")
                    if member.isdir():
                        target.mkdir(parents=True, exist_ok=True)
                    elif member.isfile():
                        target.parent.mkdir(parents=True, exist_ok=True)
                        with archive.extractfile(member) as source:
                            target.write_bytes(source.read())
                        target.chmod(member.mode & 0o777)
                    else:
                        raise RuntimeError(f"Unsupported snapshot object: {member.name}")
            # A new temporary tree has no stale objects, staged user ELFs or disk images.
            make = ["make", f"-j{args.jobs}", f"CROSS_PREFIX={args.cross_prefix}", f"QEMU={args.qemu}"]
            run(make + ["all"], work, output / "build.log")
            # Serial test order keeps QEMU and shared packaging artifacts independent.
            run(["make", "-j1", f"CROSS_PREFIX={args.cross_prefix}", f"QEMU={args.qemu}", "test"],
                work, output / "tests.log")
            run([sys.executable, "tests/package-test.py"], work, output / "package.log")
            run([sys.executable, "tests/release-integration-test.py", "--qemu", args.qemu, "--packaged-only"],
                work, output / "packaged-boot.log")
            report["passed"] = True
        finally:
            artifacts = work / "build/test-artifacts"
            if artifacts.exists():
                shutil.copytree(artifacts, output / "test-artifacts", dirs_exist_ok=True)
            for name in ("rum.iso", "rum.elf", "rum-system.img"):
                source = work / "build" / name
                if source.is_file():
                    shutil.copy2(source, output / name)
            if (work / "rum.zip").is_file():
                shutil.copy2(work / "rum.zip", output / "rum.zip")
            report["elapsed_seconds"] = round(time.monotonic() - started, 1)
            if (output / "rum.iso").is_file():
                report["iso_sha256"] = hashlib.sha256((output / "rum.iso").read_bytes()).hexdigest()
            (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: clean build, complete suite and packaged ISO; artifacts in {output}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"release-check: {error}", file=sys.stderr)
        sys.exit(1)
