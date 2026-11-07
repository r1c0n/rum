#!/usr/bin/env python3
"""Bundle the built ISO, README, and documentation into rum.zip."""

from pathlib import Path
import argparse
import sys
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile


def package(output=None):
    root = Path(__file__).resolve().parent.parent
    iso = root / "build/rum.iso"
    readme = root / "README.md"
    docs = root / "docs"
    output = Path(output) if output is not None else root / "rum.zip"
    if output.suffix.lower() != ".zip":
        raise ValueError("package output must use the .zip extension")
    if output.is_symlink():
        raise ValueError("package output cannot be a symlink")

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
        dir=output.parent, prefix=".rum-", suffix=".zip", delete=False
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="ZIP output path (default: rum.zip)")
    args = parser.parse_args()
    try:
        output = package(args.output)
    except (OSError, ValueError) as error:
        print(f"package: {error}", file=sys.stderr)
        return 1
    print(f"Created {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
