#!/usr/bin/env python3
"""Verify explicit disk preparation, installed bytes and overwrite refusal."""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("installer", ROOT / "scripts/prepare-disk.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu")  # shared runner compatibility; no guest required
    parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="rum-disk-prepare-") as temporary:
        disk = Path(temporary) / "system.raw"
        installer.prepare(disk)
        before = hashlib.sha256(disk.read_bytes()).digest()
        for name in (ROOT / "build/user/programs.txt").read_text().splitlines():
            data = subprocess.check_output(["mcopy", "-i", str(disk), "::/RUM/" + name.upper(), "-"])
            assert data == (ROOT / "build/user/system" / name).read_bytes(), name
        try:
            installer.prepare(disk)
            raise AssertionError("existing image was accepted")
        except FileExistsError:
            pass
        assert hashlib.sha256(disk.read_bytes()).digest() == before
        for size in (0, 1, 15, 129):
            try:
                installer.prepare(Path(temporary) / "invalid.raw", size)
                raise AssertionError("invalid size accepted")
            except ValueError:
                pass
            assert not (Path(temporary) / "invalid.raw").exists()
        subprocess.run(["fsck.fat", "-n", str(disk)], check=True, capture_output=True, timeout=20)
    print("PASS: FAT16 system disk, exact installed programs, clean FAT, invalid sizes and overwrite refusal")


if __name__ == "__main__":
    main()
