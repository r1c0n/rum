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
    sdk_archive = release / "rum-sdk.zip"
    if not sdk_archive.is_file() or hashlib.sha256(sdk_archive.read_bytes()).hexdigest() != source.get("sdk_sha256"):
        parser.error("the SDK archive is missing or differs from the validated SHA256")
    output = release / ("host-" + platform.system().lower())
    output.mkdir(parents=True, exist_ok=True)
    report = {"passed": False, "host": platform.system(), "python": platform.python_version(),
              "commit": commit, "tests_commit": source["commit"], "documentation_changes": changed, "iso_sha256": digest}
    spec = importlib.util.spec_from_file_location("release_host_tools", ROOT / "tests/process-tools-test.py")
    tools = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tools)
    tools.shell.ARTIFACTS = output
    spec = importlib.util.spec_from_file_location("host_release_workflows", ROOT / "tests/release-integration-test.py")
    workflows = importlib.util.module_from_spec(spec); spec.loader.exec_module(workflows)
    try:
        with tempfile.TemporaryDirectory(prefix="rum-host-release-") as temporary:
            work = Path(temporary)
            snapshot = git("archive", "--format=tar", commit, "README.md", "docs", "scripts/package.py", "tests/package-test.py",
                           "scripts/package-sdk.py", "sdk")
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
            with ZipFile(sdk_archive) as archive:
                metadata = json.loads(archive.read("rum-sdk/sdk.json"))
                assert metadata["source_commit"] == source["commit"] and metadata["source_dirty"] is False
                assert set(archive.namelist()) == {"rum-sdk/sdk.json"} | {"rum-sdk/" + name for name in metadata["files"]}
                for name, member_digest in metadata["files"].items():
                    assert hashlib.sha256(archive.read("rum-sdk/" + name)).hexdigest() == member_digest
                for member, target in (("lib/crt0.o", "lib/start.o"), ("lib/librum.a", "lib/librum.a"),
                                       ("linker/user.ld", "linker/user.ld")):
                    path = work / "build/sdk" / target; path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(archive.read("rum-sdk/" + member))
            (work / ".rum-source.json").write_text(json.dumps({"commit": source["commit"], "dirty": False}) + "\n")
            sdk_result = subprocess.run([sys.executable, str(work / "scripts/package-sdk.py"), "--output", str(work / "rum-sdk.zip")],
                                        cwd=work, capture_output=True, text=True)
            (output / "sdk-package.log").write_text(sdk_result.stdout + sdk_result.stderr)
            assert sdk_result.returncode == 0, sdk_result.stdout + sdk_result.stderr
            with ZipFile(sdk_archive) as reference, ZipFile(work / "rum-sdk.zip") as native:
                assert reference.namelist() == native.namelist()
                for name in reference.namelist():
                    assert reference.read(name) == native.read(name), name
            shutil.copy2(work / "rum-sdk.zip", output / "rum-sdk.zip")
            report["sdk_sha256"] = hashlib.sha256((output / "rum-sdk.zip").read_bytes()).hexdigest()
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
                console.command("heapdemo /README.TXT", "Heap growth, data preservation and release passed")
                workflows.workflows(console)
                console.type("snake\n"); console.wait("rum_snake_started\r\n")
                console.send("q"); console.wait("/> ")
                assert b"rum_snake_quit\r\n" in console.serial.read_bytes()
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
                workflows.resource_baseline(console)
                console.type("snake\n"); console.wait("rum_snake_started\r\n")
                console.send("q"); console.wait("> ")
            with tools.shell.boot(args.qemu, 64, "packaged-reboot-64m", iso=True, image=extracted, disk=disk) as console:
                console.command("cat /HOME/SAVED.TXT", workflows.WORKFLOW_BYTES.decode().replace("\n", "\r\n"))
                console.command("/APPS/HELLO.ELF", "Hello from rum userspace!")
                console.command("sync")
                console.command("recovery", "Kernel recovery shell.")
                workflows.resource_baseline(console)
            files = tools.shell.images.audit(disk)["files"]
            assert files["/HOME/SAVED.TXT"] == workflows.WORKFLOW_BYTES
            assert files["/HOME/EXACT.ELF"] == files["/APPS/HELLO.ELF"] == files["/RUM/HELLO.ELF"]
            report["checks"] = ["archive bytes", "SDK hashes and native repackaging", "heap and public file I/O", "pipelines/append/copy/rename", "job fault/cancel/restart cleanup",
                                "user Snake", "separate boot persistence", "untouched source system disk"]
            shutil.copy2(work / "rum.zip", output / "rum.zip")
            assert hashlib.sha256(system_disk.read_bytes()).hexdigest() == source["system_disk_sha256"], "validated source disk changed"
            report["passed"] = True
    finally:
        (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: {report['host']} native Python packaging and QEMU boot of exact ISO {digest}")


if __name__ == "__main__":
    main()
