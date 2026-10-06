#!/usr/bin/env python3
"""Check every volume against the production backend through a host block device."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("fat_images", ROOT / "tests/fat16-images.py")
images = importlib.util.module_from_spec(spec)
spec.loader.exec_module(images)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=ROOT / "build/tests/fat16")
    parser.add_argument("--reuse", action="store_true", help="Use previously generated image fixtures")
    args = parser.parse_args()
    cases = json.loads((args.directory / "manifest.json").read_text()) if args.reuse else images.generate(args.directory)
    for case in cases:
        image = args.directory / case["image"]
        digest = hashlib.sha256(image.read_bytes()).digest()
        listing = args.directory / f"{case['name']}.listing"
        result = subprocess.run([str(ROOT / "build/tests/fat16-test"), str(image), case["phase"], case["path"], str(listing)],
                                check=True, capture_output=True, text=True, timeout=20)
        assert f"rum_fat_result: {case['phase']} {case['error']}\n" in result.stdout, result.stdout
        if case["phase"] == "valid":
            assert sorted(listing.read_text().splitlines()) == case["listing"], case["name"]
        assert hashlib.sha256(image.read_bytes()).digest() == digest, "read-only test changed an image"
        print(f"PASS: host FAT16 {case['name']}", flush=True)


if __name__ == "__main__":
    main()
