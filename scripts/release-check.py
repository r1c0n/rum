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
    parser.add_argument("--workers", type=int, choices=range(1, 5), default=4)
    parser.add_argument("--compat-sdk", type=Path, default=ROOT / "build/releases/0.7.0-110836a/rum-sdk.zip",
                        help="preserved clean 0.7.0 SDK archive for backwards-compatibility validation")
    args = parser.parse_args()
    if os.name == "nt":
        parser.error("Run this build check in Ubuntu/WSL; the package test also runs on Windows.")
    if not 1 <= args.jobs <= 64:
        parser.error("--jobs must be between 1 and 64")
    if not args.compat_sdk.is_file():
        parser.error("supply --compat-sdk with the released 0.7.0 rum-sdk.zip; compatibility cannot be skipped for release")
    args.compat_sdk = args.compat_sdk.resolve()
    os.environ["RUM_COMPAT_SDK"] = str(args.compat_sdk)
    if capture("git", "status", "--porcelain"):
        parser.error("Commit source changes before checking the exact release snapshot.")
    commit = capture("git", "rev-parse", "HEAD")
    output = ROOT / "build/release-check" / (commit[:12] + "-" + time.strftime("%Y%m%d-%H%M%S"))
    output.mkdir(parents=True, exist_ok=True)
    report = {"commit": commit, "passed": False,
              "compat_sdk_sha256": hashlib.sha256(args.compat_sdk.read_bytes()).hexdigest()}
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
            (work / ".rum-source.json").write_text(json.dumps({"commit": commit, "dirty": False}) + "\n")
            # A new temporary tree has no stale objects, staged user ELFs or disk images.
            make = ["make", f"-j{args.jobs}", f"TEST_JOBS={args.workers}", f"CROSS_PREFIX={args.cross_prefix}", f"QEMU={args.qemu}"]
            run(make + ["all"], work, output / "build.log")
            # The suite builds once and bounds independent QEMU workers itself.
            run(make + ["test"], work, output / "tests.log")
            for suffix in (".elf", ".json", ".build.log"):
                shutil.copy2(work / ("build/tests/sdk07-app" + suffix), output / ("sdk07-app" + suffix))
            run([sys.executable, "scripts/prepare-disk.py", "build/rum-system.raw"], work, output / "system-disk.log")
            # Use the same clean program assets and ISO for a separate, fresh
            # VM download; never package an image from the caller's build tree.
            run([sys.executable, "scripts/prepare-vm-images.py"], work, output / "vm-images.log")
            report["passed"] = True
        finally:
            artifacts = work / "build/test-artifacts"
            if artifacts.exists():
                shutil.copytree(artifacts, output / "test-artifacts", dirs_exist_ok=True)
            for name in ("rum.iso", "rum.elf", "rum-system.raw", "rum-sdk.zip"):
                source = work / "build" / name
                if source.is_file():
                    shutil.copy2(source, output / name)
            if (work / "rum.zip").is_file():
                shutil.copy2(work / "rum.zip", output / "rum.zip")
            if (work / "build/vm-images").is_dir():
                shutil.copytree(work / "build/vm-images", output / "vm-images")
            report["elapsed_seconds"] = round(time.monotonic() - started, 1)
            if (output / "rum.iso").is_file():
                report["iso_sha256"] = hashlib.sha256((output / "rum.iso").read_bytes()).hexdigest()
            if (output / "rum-sdk.zip").is_file():
                report["sdk_sha256"] = hashlib.sha256((output / "rum-sdk.zip").read_bytes()).hexdigest()
            if (output / "rum-system.raw").is_file():
                report["system_disk_sha256"] = hashlib.sha256((output / "rum-system.raw").read_bytes()).hexdigest()
            if (output / "vm-images/rum-vm-images.zip").is_file():
                report["vm_images_sha256"] = hashlib.sha256((output / "vm-images/rum-vm-images.zip").read_bytes()).hexdigest()
            (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: clean build, complete suite and packaged ISO; artifacts in {output}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"release-check: {error}", file=sys.stderr)
        sys.exit(1)
