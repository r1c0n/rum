#!/usr/bin/env python3
"""Release generation must refuse failed, mismatched or tampered evidence."""
import hashlib
import contextlib
import io
import importlib.util
import json
from pathlib import Path
import tempfile
from unittest.mock import patch
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("release_assets", ROOT / "scripts/release-assets.py")
assets = importlib.util.module_from_spec(spec); spec.loader.exec_module(assets)
COMMIT = "a" * 40
IDENTITY = {"version": "0.8.0", "tag": "v0.8.0", "commit": COMMIT}


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value))


def rejects(callback):
    try:
        callback()
    except (ValueError, OSError):
        return
    raise AssertionError("invalid release was accepted")


def fixture(root):
    release = root / "evidence"; release.mkdir()
    (release / "rum.iso").write_bytes(b"standalone ISO")
    (release / "rum-system.raw").write_bytes(b"fresh disposable system disk")
    with ZipFile(release / "rum.zip", "w") as archive:
        archive.write(release / "rum.iso", "rum.iso")
        archive.writestr("README.md", "rum")
    with ZipFile(release / "rum-sdk.zip", "w") as archive:
        archive.writestr("rum-sdk/sdk.json", json.dumps({"source_commit": COMMIT, "source_dirty": False}))
    (release / "vm-images").mkdir()
    with ZipFile(release / "vm-images/rum-vm-images.zip", "w") as archive:
        archive.write(release / "rum-system.raw", "rum-data.raw")
    report = {"commit": COMMIT, "passed": True, "elapsed_seconds": 200, "compat_sdk_sha256": "b" * 64,
              "system_disk_sha256": assets.digest(release / "rum-system.raw")}
    for _, (relative, field) in assets.PAYLOADS.items():
        if field:
            report[field] = assets.digest(release / relative)
    write_json(release / "result.json", report)
    write_json(release / "test-artifacts/suite/result.json", {"passed": True, "jobs": 4, "seconds": 140,
               "suites": [{"name": "coverage", "returncode": 0}]})
    for host in ("Linux", "Windows"):
        write_json(release / ("host-" + host.lower()) / "result.json",
                   {"passed": True, "host": host, "commit": COMMIT, "tests_commit": COMMIT, "iso_sha256": report["iso_sha256"]})
    return release


with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    (root / "assets/disk").mkdir(parents=True)
    (root / "assets/disk/readme.txt").write_text("rum OS v0.8.0\n")
    def fake_git(*arguments):
        return "" if arguments[0] == "status" else COMMIT
    with patch.object(assets, "ROOT", root), patch.object(assets, "git", fake_git), patch.object(assets.subprocess, "run") as ancestry:
        assert assets.metadata("v0.8.0") == IDENTITY
        ancestry.assert_called_once_with(["git", "merge-base", "--is-ancestor", COMMIT, "origin/master"], cwd=root, check=True)
        rejects(lambda: assets.metadata("v0.8.1"))
        rejects(lambda: assets.metadata("v0.8.0; echo unsafe"))
        with patch.object(assets, "git", return_value="uncommitted"):
            rejects(lambda: assets.metadata("v0.8.0"))

for failure in (None, "clean", "suite", "windows", "host", "source", "iso", "disk", "sdk", "mutable"):
    with tempfile.TemporaryDirectory() as temporary, patch.object(assets, "metadata", return_value=IDENTITY):
        root = Path(temporary); release = fixture(root); output = root / "downloads"
        if failure in ("clean", "source"):
            report = assets.read_json(release / "result.json")
            report["passed" if failure == "clean" else "commit"] = False if failure == "clean" else "c" * 40
            write_json(release / "result.json", report)
        if failure == "suite":
            report = assets.read_json(release / "test-artifacts/suite/result.json")
            report["suites"][0]["returncode"] = 1
            write_json(release / "test-artifacts/suite/result.json", report)
        if failure in ("windows", "host"):
            report = assets.read_json(release / "host-windows/result.json")
            report["passed" if failure == "windows" else "host"] = False if failure == "windows" else "Linux"
            write_json(release / "host-windows/result.json", report)
        if failure in ("iso", "disk", "sdk"):
            path = release / {"iso": "rum.iso", "disk": "rum-system.raw", "sdk": "rum-sdk.zip"}[failure]
            path.write_bytes(b"changed")
        if failure == "mutable":
            with ZipFile(release / "rum.zip", "a") as archive:
                archive.writestr("personal.raw", b"must not be published")
        if failure:
            rejects(lambda: assets.stage(release, output, "v0.8.0")); assert not output.exists()
        else:
            assets.stage(release, output, "v0.8.0")
            assert {p.name for p in output.iterdir()} == assets.ASSETS
            assert assets.verify(output, "v0.8.0")["files"]["rum.iso"] == hashlib.sha256(b"standalone ISO").hexdigest()
            rejects(lambda: assets.stage(release, output, "v0.8.0"))
            rejects(lambda: assets.verify(output, "v0.8.1"))
            (output / "rum.iso").write_bytes(b"tampered")
            rejects(lambda: assets.verify(output, "v0.8.0"))

print("PASS: six release assets, clean/tag/version provenance, both native hosts, failure rejection and checksum tampering")

spec = importlib.util.spec_from_file_location("release_check", ROOT / "scripts/release-check.py")
checker = importlib.util.module_from_spec(spec); spec.loader.exec_module(checker)
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary); (root / "sdk.zip").write_bytes(b"preserved SDK input")
    existing = root / "build/existing"; existing.mkdir(parents=True)
    (existing / "keep.raw").write_bytes(b"persistent canary")
    def capture(*arguments):
        return COMMIT if "rev-parse" in arguments else ""
    for output in (root / "outside", root / "build", existing):
        with patch.object(checker, "ROOT", root), patch.object(checker, "capture", capture), \
                patch.dict(checker.os.environ), patch("sys.argv", ["release-check.py", "--compat-sdk", str(root / "sdk.zip"),
                           "--output", str(output)]), patch.object(checker.subprocess, "check_output") as archive, \
                contextlib.redirect_stderr(io.StringIO()):
            try:
                checker.main()
            except SystemExit as error:
                assert error.code == 2
            else:
                raise AssertionError("invalid evidence directory accepted")
            archive.assert_not_called()
    assert (existing / "keep.raw").read_bytes() == b"persistent canary" and not (root / "outside").exists()
print("PASS: CI evidence output rejects existing/outside paths before building and preserves disks")
