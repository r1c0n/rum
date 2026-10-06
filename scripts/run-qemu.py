#!/usr/bin/env python3
"""Boot rum, optionally attaching an existing raw image as secondary IDE master."""
import argparse
from pathlib import Path
import subprocess
import sys

MAX_DISK_BYTES = (1 << 28) * 512


def validate_disk(path):
    path = Path(path).resolve(strict=True)
    if not path.is_file():
        raise ValueError("disk image must be a regular file")
    size = path.stat().st_size
    if not size or size % 512 or size > MAX_DISK_BYTES:
        raise ValueError("raw disk size must be a nonzero multiple of 512 bytes, at most 128 GiB")
    return path


def drive(path, *, index, media, read_only=False):
    # QEMU's keyval parser quotes a literal comma with a doubled comma.
    filename = str(Path(path).resolve()).replace(",", ",,")
    options = f"file={filename},format=raw,if=ide,index={index},media={media}"
    if media == "disk":
        options += ",cache=writeback,werror=report,rerror=report"
    if read_only:
        options += ",readonly=on"
    return ["-drive", options]


def boot_arguments(image, kernel=False, disk=None, read_only=False):
    image = Path(image).resolve(strict=True)
    # Suppress QEMU's implicit empty secondary CD-ROM; VGA is explicit. The PC
    # still supplies its PS/2 controller, PIT and PIC, and callers add serial.
    arguments = ["-nodefaults", "-vga", "std"] + (["-kernel", str(image)] if kernel else
                 ["-boot", "d"] + drive(image, index=0, media="cdrom"))
    if disk is not None:
        path = validate_disk(disk)
        if path.samefile(image):
            raise ValueError("boot image and writable disk must be different files")
        arguments += drive(path, index=2, media="disk", read_only=read_only)
    elif read_only:
        raise ValueError("--disk-read-only requires --disk")
    return arguments


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--image", type=Path, default=Path("build/rum.iso"))
    parser.add_argument("--kernel", action="store_true")
    parser.add_argument("--disk", type=Path, help="existing raw image; never created or resized")
    parser.add_argument("--disk-read-only", action="store_true")
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()
    try:
        arguments = boot_arguments(args.image, args.kernel, args.disk, args.disk_read_only)
        command = [args.qemu, "-name", "rum OS", "-m", "64M", "-serial", "stdio",
                   "-no-reboot", "-no-shutdown"] + arguments
        if args.debug:
            command += ["-S", "-s"]
        return subprocess.call(command)
    except (OSError, ValueError) as error:
        print(f"rum: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
