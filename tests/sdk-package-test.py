#!/usr/bin/env python3
"""Build from an unpacked SDK outside the repository and verify its manifest."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from zipfile import ZipFile
from unittest.mock import patch

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
    spec = importlib.util.spec_from_file_location("sdk_export", ROOT / "scripts/package-sdk.py")
    exporter = importlib.util.module_from_spec(spec); spec.loader.exec_module(exporter)
    with tempfile.TemporaryDirectory() as snapshot:
        snapshot = Path(snapshot)
        with patch.object(exporter.subprocess, "run", side_effect=FileNotFoundError):
            assert exporter.source_identity(snapshot) == (None, None)
            (snapshot / ".rum-source.json").write_text(json.dumps({"commit": "a" * 40, "dirty": False}))
            assert exporter.source_identity(snapshot) == ("a" * 40, False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    before = (ROOT / "build/rum.elf").read_bytes() if (ROOT / "build/rum.elf").exists() else None
    with tempfile.TemporaryDirectory(prefix="rum external sdk ") as temporary:
        directory = Path(temporary)
        sdk = prepare(ROOT / "build/rum-sdk.zip", directory)
        metadata = json.loads((sdk / "sdk.json").read_text())
        assert "typed-object-io-v1" in metadata["required_features"]
        assert "process-objects-v1" in metadata["required_features"]
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
        # Replacing an SDK can preserve ZIP timestamps. The project's cached
        # validator must follow changed header contents rather than old mtimes.
        contract = sdk / "include/rum/abi/elf.h"
        original = contract.read_bytes(); timestamp = contract.stat()
        contract.write_bytes(original.replace(b"0x01000000u", b"65536u"))
        os.utime(contract, ns=(timestamp.st_atime_ns, timestamp.st_mtime_ns))
        subprocess.run(["python3", str(sdk / "tools/build.py"), "--cross-prefix", args.cross_prefix,
                        "--output", str(project / "build/example.elf"), str(project / "main.c")], check=True, capture_output=True)
        padded = project / "build/padded.elf"
        padded.write_bytes(executable.read_bytes())
        with padded.open("ab") as output:
            output.truncate(65537)
        checker = next((project / "build/example-objects").glob("check-user-elf"))
        rejected = subprocess.run([str(checker), str(padded)], capture_output=True)
        assert rejected.returncode != 0, "changed SDK limits must invalidate the cached host validator"
        contract.write_bytes(original); os.utime(contract, ns=(timestamp.st_atime_ns, timestamp.st_mtime_ns))
        stress = project / "mixed.c"; shutil.copyfile(ROOT / "tests/sdk-integration.c", stress)
        mixed = project / "build/mixed.elf"
        subprocess.run(["python3", str(sdk / "tools/build.py"), "--cross-prefix", args.cross_prefix,
                        "--output", str(mixed), str(stress)], check=True, capture_output=True)
        assert mixed.stat().st_size > 65536, "integration image must exercise larger executable loading"
        shutil.copyfile(mixed, args.output.with_name("sdk-stress.elf"))
        if args.doom_source:
            subprocess.run(["python3", str(sdk / "tools/probe-doom.py"), "--source", str(args.doom_source.resolve()),
                "--output", str(ROOT / "build/doom-probe"), "--cross-prefix", args.cross_prefix], check=True)
    if before is not None:
        assert (ROOT / "build/rum.elf").read_bytes() == before, "SDK app build must not rebuild the OS"
    print("PASS: SDK manifest, public-only headers and external build with spaces and no OS rebuild")


if __name__ == "__main__":
    main()
