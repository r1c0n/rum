#!/usr/bin/env python3
"""Repackage a clean release on this host and boot its exact extracted ISO."""
import argparse
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, stderr=subprocess.PIPE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release", type=Path, required=True, help="successful build/release-check/<commit>-<time> directory")
    parser.add_argument("--qemu", default="qemu-system-i386")
    args = parser.parse_args()
    release = args.release.resolve()
    source = json.loads((release / "result.json").read_text())
    if source.get("passed") is not True or not re.fullmatch(r"[0-9a-f]{40}", source.get("commit", "")):
        parser.error("the clean release check must pass before host validation")
    commit = git("rev-parse", "HEAD").decode().strip()
    if git("status", "--porcelain").strip():
        parser.error("commit source changes before verifying host packaging")
    # Completing the release checklist can change documentation after the code
    # gate. Reject every other difference; preserve both identities in evidence.
    git("merge-base", "--is-ancestor", source["commit"], commit)
    changed = git("diff", "--name-only", source["commit"], commit).decode().splitlines()
    if any(path != "README.md" and not path.startswith("docs/") for path in changed):
        parser.error("executable/test/build source changed since the clean release check; run it again")
    iso = release / "rum.iso"
    digest = hashlib.sha256(iso.read_bytes()).hexdigest()
    if digest != source["iso_sha256"]:
        parser.error("the release ISO no longer matches its validated SHA256")
    system_disk = release / "rum-system.raw"
    if not system_disk.is_file() or hashlib.sha256(system_disk.read_bytes()).hexdigest() != source.get("system_disk_sha256"):
        parser.error("the prepared system disk is missing or differs from the validated SHA256")
    output = release / ("host-" + platform.system().lower())
    output.mkdir(parents=True, exist_ok=True)
    report = {"passed": False, "host": platform.system(), "python": platform.python_version(),
              "commit": commit, "tests_commit": source["commit"], "documentation_changes": changed, "iso_sha256": digest}
    spec = importlib.util.spec_from_file_location("release_host_tools", ROOT / "tests/process-tools-test.py")
    tools = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tools)
    tools.shell.ARTIFACTS = output
    try:
        with tempfile.TemporaryDirectory(prefix="rum-host-release-") as temporary:
            work = Path(temporary)
            snapshot = git("archive", "--format=tar", commit, "README.md", "docs", "scripts/package.py", "tests/package-test.py")
            with tarfile.open(fileobj=io.BytesIO(snapshot)) as archive:
                for member in archive.getmembers():
                    target = work / member.name
                    if not target.resolve().is_relative_to(work.resolve()) or not (member.isdir() or member.isfile()):
                        raise ValueError("unsupported package source entry: " + member.name)
                    if member.isdir():
                        target.mkdir(parents=True, exist_ok=True)
                    else:
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(archive.extractfile(member).read())
            (work / "build").mkdir()
            shutil.copy2(iso, work / "build/rum.iso")
            packaged = subprocess.run([sys.executable, str(work / "tests/package-test.py")], cwd=work,
                                      capture_output=True, text=True)
            (output / "package.log").write_text(packaged.stdout + packaged.stderr)
            assert packaged.returncode == 0, packaged.stdout + packaged.stderr
            with ZipFile(work / "rum.zip") as archive:
                archive.extract("rum.iso", work / "extracted")
            extracted = work / "extracted/rum.iso"
            assert hashlib.sha256(extracted.read_bytes()).hexdigest() == digest
            version = (ROOT / "assets/disk/readme.txt").read_text().splitlines()[0]
            disk = work / "system.raw"
            shutil.copy2(system_disk, disk)
            with tools.shell.boot(args.qemu, 64, "packaged-iso-64m", iso=True, image=extracted, disk=disk) as console:
                console = tools.Console(console.stream, console.serial)
                console.command("about", version)
                pid = tools.launch(console, "spin")
                assert pid in tools.listing(console)
                console.command("echo host package works", "host package works\r\n")
                console.command(f"kill {pid}", "Cancellation requested")
                assert pid not in tools.listing(console)
                sleeper = tools.launch(console, "sleep 100")
                deadline = time.monotonic() + 10
                while f"[{sleeper}] sleep: exited with status 0." not in console.serial.read_text():
                    assert time.monotonic() < deadline, "no child completion notice"
                    time.sleep(0.02)
                console.wait("/> ")
                console.command("recovery", "Kernel recovery shell.")
                diagnostic = console.command("diag")
                assert "Processes: 0 | 0 user tables, 0 user pages" in diagnostic
                assert "Sessions: 0 live, 0 closing, 0 members | 0 session refs, 0 process refs" in diagnostic
                console.type("snake\n"); console.wait("rum_snake_started\r\n")
                console.send("q"); console.wait("> ")
            shutil.copy2(work / "rum.zip", output / "rum.zip")
            report["passed"] = True
    finally:
        (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: {report['host']} native Python packaging and QEMU boot of exact ISO {digest}")


if __name__ == "__main__":
    main()
