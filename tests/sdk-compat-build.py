#!/usr/bin/env python3
"""Build an unchanged external application with the preserved 0.7 release SDK."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
RELEASE_COMMIT = "110836a41e60823015f2de6caf594afc03f06471"


def build(archive, output, cross_prefix):
    spec = importlib.util.spec_from_file_location("compat_package", ROOT / "tests/sdk-package-test.py")
    package = importlib.util.module_from_spec(spec); spec.loader.exec_module(package)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rum released sdk ") as temporary:
        work = Path(temporary)
        sdk = package.prepare(archive, work)
        metadata = json.loads((sdk / "sdk.json").read_text())
        assert metadata["development_series"] == "0.7" and metadata["abi_version"] == 1
        assert metadata["source_commit"] == RELEASE_COMMIT and metadata["source_dirty"] is False
        project = work / "legacy application"; shutil.copytree(sdk / "template", project)
        result = subprocess.run(["make", "RUM_SDK=" + str(sdk), "CROSS_PREFIX=" + cross_prefix],
                                cwd=project, capture_output=True, text=True)
        output.with_suffix(".build.log").write_text(result.stdout + result.stderr)
        assert result.returncode == 0, result.stdout + result.stderr
        shutil.copy2(project / "build/example.elf", output)
    evidence = {"sdk_commit": RELEASE_COMMIT, "sdk_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
                "application_sha256": hashlib.sha256(output.read_bytes()).hexdigest()}
    output.with_suffix(".json").write_text(json.dumps(evidence, indent=2) + "\n")
    print("PASS: unchanged application built externally with the clean released 0.7 SDK", flush=True)
    return evidence


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "build/tests/sdk07-app.elf")
    parser.add_argument("--cross-prefix", default=str(ROOT / ".tools/cross/bin/i686-elf-"))
    args = parser.parse_args()
    build(args.sdk.resolve(strict=True), args.output, args.cross_prefix)
