#!/usr/bin/env python3
"""Host image-creation and launcher argument tests; no QEMU required."""
import importlib.util
import argparse
import os
from pathlib import Path
import tempfile
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--boot-output-checks", action="store_true",
                    help="also invoke make to check preservation; run after the ISO build")
options = parser.parse_args()


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / f"{name}.py")
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


creator = module("disk-image")
launcher = module("run-qemu")


def rejects(call):
    try:
        call()
    except (OSError, ValueError):
        return
    raise AssertionError("unexpectedly accepted invalid image")


with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    disk = directory / "test disk,with comma.raw"
    creator.create_image(disk, 1)
    assert disk.stat().st_size == 1024 * 1024 and disk.read_bytes() == bytes(1024 * 1024)
    disk.write_bytes(b"canary" + bytes(512 - 6))
    before = disk.read_bytes()
    rejects(lambda: creator.create_image(disk, 1))
    assert disk.read_bytes() == before
    for size in (0, -1, 131072):
        invalid = directory / f"size-{size}"
        rejects(lambda: creator.create_image(invalid, size))
        assert not invalid.exists()
    boot = directory / "boot.iso"
    boot.write_bytes(bytes(512))
    args = launcher.boot_arguments(boot, disk=disk)
    assert "index=0,media=cdrom" in args[6]
    assert "index=2,media=disk" in args[8] and "disk,,with comma.raw" in args[8]
    assert "format=raw" in args[8] and "werror=report" in args[8]
    assert launcher.boot_arguments(boot, kernel=True) == ["-nodefaults", "-vga", "std", "-kernel", str(boot)]
    system = directory / "rum-system.img"; system.write_bytes(b"separate module")
    assert launcher.boot_arguments(boot, kernel=True)[-2:] == ["-initrd", str(system)]
    assert "-initrd" not in launcher.boot_arguments(boot, kernel=True, system_image=False)
    assert "-initrd" not in launcher.boot_arguments(boot)
    system.unlink()
    assert "readonly=on" in launcher.boot_arguments(boot, kernel=True, disk=disk, read_only=True)[6]
    assert disk.read_bytes() == before
    rejects(lambda: launcher.boot_arguments(boot, disk=boot))
    for output in (ROOT / "build/rum.iso", ROOT / "build/rum.elf", ROOT / "build/rum-system.img"):
        if output.exists():
            rejects(lambda: launcher.validate_disk(output))
            if options.boot_output_checks and os.name != "nt":
                saved = output.read_bytes()
                check = subprocess.run(["make", "run", f"DISK_IMAGE={output}"], cwd=ROOT,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
                assert check.returncode != 0
                assert output.read_bytes() == saved
    rejects(lambda: launcher.boot_arguments(boot, read_only=True))
    rejects(lambda: launcher.validate_disk(directory))
    missing = directory / "missing.raw"
    rejects(lambda: launcher.validate_disk(missing))
    assert not missing.exists()
    disk.write_bytes(b"unaligned")
    rejects(lambda: launcher.validate_disk(disk))
    disk.write_bytes(b"")
    rejects(lambda: launcher.validate_disk(disk))
    if hasattr(Path, "symlink_to"):
        link = directory / "dangling.raw"
        try:
            link.symlink_to(missing)
        except OSError:
            pass  # Windows may not grant symlink privileges.
        else:
            rejects(lambda: creator.create_image(link, 1))
            assert not missing.exists()
print("disk creation and launcher tests passed")
