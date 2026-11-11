#!/usr/bin/env python3
"""Package the rum application SDK without kernel sources or a cross compiler."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parents[1]


def source_identity(root):
    try:
        location = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=root, text=True, capture_output=True)
        if location.returncode == 0 and Path(location.stdout.strip()).resolve() == root.resolve():
            revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
            dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True))
            return revision, dirty
    except (OSError, subprocess.CalledProcessError):
        pass
    snapshot = root / ".rum-source.json"
    if snapshot.is_file():
        information = json.loads(snapshot.read_text())
        return information["commit"], information["dirty"]
    return None, None  # A source archive need not include Git history.


def package(output=None, root=ROOT):
    output = Path(output) if output else root / "build/rum-sdk.zip"
    if output.suffix.lower() != ".zip" or output.is_symlink() or output.is_dir():
        raise ValueError("SDK output must be a ZIP file, not a directory or symlink")
    if not output.parent.is_dir():
        raise ValueError("SDK output parent must exist")
    entries = {}
    paths = list((root / "sdk/include").rglob("*.h"))
    paths += [root / "sdk" / name for name in (
        "tools/build.py", "tools/check-user-elf.c", "tools/probe-doom.py",
        "template/Makefile", "template/build.ps1", "template/main.c",
        "probes/doom/headless.c", "probes/doom/platform.c", "probes/doom/include/inttypes.h", "probes/doom/include/strings.h")]
    for path in sorted(paths):
        if path.is_symlink():
            raise ValueError(f"SDK input cannot be a symlink: {path}")
        entries[path.relative_to(root / "sdk").as_posix()] = path.read_bytes()
    for source, target in (("build/sdk/lib/start.o", "lib/crt0.o"),
                           ("build/sdk/lib/librum.a", "lib/librum.a"),
                           ("build/sdk/linker/user.ld", "linker/user.ld"),
                           ("sdk/README-sdk.md", "README.md")):
        entries[target] = (root / source).read_bytes()
    revision, dirty = source_identity(root)
    metadata = {"format_version": 1, "sdk_interface_version": 1, "development_series": "0.8",
                "target": "i686-elf", "abi_version": 1, "source_commit": revision, "source_dirty": dirty,
                "required_features": ["private-regions-v1", "monotonic-clock-v1", "filesystem-v1", "typed-object-handles-v1", "typed-object-io-v1", "process-objects-v1", "spawn-object-grants-v1"],
                "files": {name: hashlib.sha256(data).hexdigest() for name, data in sorted(entries.items())}}
    entries["sdk.json"] = (json.dumps(metadata, indent=2) + "\n").encode()
    with tempfile.NamedTemporaryFile(dir=output.parent, prefix=".rum-sdk-", suffix=".zip", delete=False) as stream:
        temporary = Path(stream.name)
    try:
        with ZipFile(temporary, "w", compression=ZIP_DEFLATED) as archive:
            for name, data in sorted(entries.items()):
                info = ZipInfo("rum-sdk/" + name, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                archive.writestr(info, data)
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        print(f"Created {package(args.output)}")
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"SDK: {error}\n")


if __name__ == "__main__":
    main()
