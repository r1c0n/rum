#!/usr/bin/env python3
"""Build from an unpacked SDK outside the repository and verify its manifest."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]


def prepare(archive, directory):
    with ZipFile(archive) as package:
        names = package.namelist()
        assert len(names) == len(set(names))
        assert all(name.startswith("rum-sdk/") and ".." not in Path(name).parts for name in names)
        assert not any("kernel/" in name or name.endswith(".raw") or name.endswith(".iso") for name in names)
        package.extractall(directory)
    sdk = directory / "rum-sdk"
    metadata = json.loads((sdk / "sdk.json").read_text())
    assert metadata["format_version"] == 1 and metadata["target"] == "i686-elf"
    actual = {path.relative_to(sdk).as_posix() for path in sdk.rglob("*") if path.is_file() and path.name != "sdk.json"}
    assert actual == set(metadata["files"])
    for name, digest in metadata["files"].items():
        assert hashlib.sha256((sdk / name).read_bytes()).hexdigest() == digest
    assert not (sdk / "include/rum/task.h").exists()
    return sdk


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cross-prefix", required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "build/tests/sdk-app.elf")
    parser.add_argument("--doom-source", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    before = (ROOT / "build/rum.elf").read_bytes() if (ROOT / "build/rum.elf").exists() else None
    with tempfile.TemporaryDirectory(prefix="rum external sdk ") as temporary:
        directory = Path(temporary)
        sdk = prepare(ROOT / "build/rum-sdk.zip", directory)
        project = directory / "external application"; shutil.copytree(sdk / "template", project)
        result = subprocess.run(["make", "RUM_SDK=" + str(sdk), "CROSS_PREFIX=" + args.cross_prefix], cwd=project,
                                capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "-Ikernel" not in result.stdout and str(ROOT / "kernel") not in result.stdout
        executable = project / "build/example.elf"
        assert executable.is_file()
        symbols = subprocess.check_output([args.cross_prefix + "nm", "--undefined-only", str(project / "build/example.debug.elf")], text=True)
        assert not symbols
        forbidden = subprocess.run([args.cross_prefix + "gcc", "-ffreestanding", "-I" + str(sdk / "include"),
            "-x", "c", "-fsyntax-only", "-"], input="#include <rum/task.h>\n", text=True, capture_output=True)
        assert forbidden.returncode != 0
        shutil.copyfile(executable, args.output)
        if args.doom_source:
            subprocess.run(["python3", str(sdk / "tools/probe-doom.py"), "--source", str(args.doom_source.resolve()),
                "--output", str(ROOT / "build/doom-probe"), "--cross-prefix", args.cross_prefix], check=True)
    if before is not None:
        assert (ROOT / "build/rum.elf").read_bytes() == before, "SDK app build must not rebuild the OS"
    print("PASS: SDK manifest, public-only headers and external build with spaces and no OS rebuild")


if __name__ == "__main__":
    main()
