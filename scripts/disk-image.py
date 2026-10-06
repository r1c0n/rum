#!/usr/bin/env python3
"""Create a new blank, disposable raw image. Never overwrite an existing path."""
import argparse
from pathlib import Path
import sys


def create_image(path, size_mib):
    if not 1 <= size_mib <= 131071:
        raise ValueError("size must be between 1 and 131071 MiB (LBA28)")
    # Exclusive creation also refuses symlinks, including dangling ones. Sparse
    # extension reads as zeroes; only this newly created file is ever resized.
    with Path(path).open("xb") as image:
        image.truncate(size_mib * 1024 * 1024)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="new image path; parent must exist")
    parser.add_argument("--size-mib", type=int, default=16)
    args = parser.parse_args()
    try:
        create_image(args.path, args.size_mib)
    except (OSError, ValueError) as error:
        print(f"rum: {error}", file=sys.stderr)
        return 1
    print(f"rum: created blank {args.size_mib} MiB raw image: {args.path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
