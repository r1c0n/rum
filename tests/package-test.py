#!/usr/bin/env python3
"""Build and inspect the cross-platform release archive."""

from pathlib import Path
import subprocess
import sys
import tempfile
from zipfile import ZipFile


root = Path(__file__).resolve().parent.parent
iso = root / "build/rum.iso"
output = root / "rum.zip"
subprocess.run([sys.executable, str(root / "scripts/package.py")],
               cwd=root, check=True)

source_paths = [root / "README.md", root / "docs",
                *sorted((root / "docs").rglob("*"))]
expected = {"rum.iso", *(path.relative_to(root).as_posix().rstrip("/")
                         for path in source_paths)}

with ZipFile(output) as archive:
    assert archive.testzip() is None, "release archive has a corrupt member"
    names = archive.namelist()
    normalized = {name.rstrip("/") for name in names}
    assert len(names) == len(normalized), "release archive has duplicate members"
    assert normalized == expected, f"unexpected release contents: {normalized ^ expected}"
    assert not any(name.startswith("build/") or "/debug/" in name or
                   name.endswith((".map", ".o", ".d")) for name in names), \
        "release archive contains a build or debug artifact"
    assert archive.read("rum.iso") == iso.read_bytes(), "packaged ISO differs from build"
    assert archive.read("README.md") == (root / "README.md").read_bytes()
    for path in source_paths:
        if path.is_file():
            assert archive.read(path.relative_to(root).as_posix()) == path.read_bytes(), \
                f"packaged documentation differs: {path}"
    with tempfile.TemporaryDirectory(prefix="rum-release-") as temporary:
        archive.extract("rum.iso", temporary)
        extracted = Path(temporary) / "rum.iso"
        data = extracted.read_bytes()
        assert data == iso.read_bytes() and data[32769:32774] == b"CD001", \
            "release ISO is missing or not an ISO-9660 image"

print(f"PASS: release archive contains rum.iso, README.md and {len(expected) - 2} documentation entries only")
