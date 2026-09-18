#!/usr/bin/env python3
"""Bundle the built ISO, README, and documentation into rum.zip."""

from pathlib import Path
import sys
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile


def package():
    root = Path(__file__).resolve().parent.parent
    iso = root / "build/rum.iso"
    readme = root / "README.md"
    docs = root / "docs"
    output = root / "rum.zip"

    if not iso.is_file():
        raise FileNotFoundError(
            "build/rum.iso is missing. Build it with make or .\\rum.ps1 build first."
        )
    if not readme.is_file():
        raise FileNotFoundError("The root README.md is missing.")
    if not docs.is_dir():
        raise FileNotFoundError("The docs directory is missing.")

    files = [readme, docs, *sorted(docs.rglob("*"))]
    with tempfile.NamedTemporaryFile(
        dir=root, prefix=".rum-", suffix=".zip", delete=False
    ) as temporary:
        temporary_path = Path(temporary.name)
    try:
        with ZipFile(temporary_path, "w", compression=ZIP_DEFLATED) as archive:
            archive.write(iso, "rum.iso")
            for path in files:
                archive.write(path, path.relative_to(root).as_posix())
        temporary_path.replace(output)
    finally:
        temporary_path.unlink(missing_ok=True)
    return output


def main():
    try:
        output = package()
    except OSError as error:
        print(f"package: {error}", file=sys.stderr)
        return 1
    print(f"Created {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
