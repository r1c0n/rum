#!/usr/bin/env python3
"""Remove compiler outputs while preserving disks, VM bundles and release evidence."""
import os
from pathlib import Path
import stat
import sys

ROOT = Path(__file__).resolve().parents[1]
DISK_SUFFIXES = {".raw", ".img", ".vmdk", ".vdi", ".vhd", ".vhdx", ".qcow", ".qcow2"}
OBJECT_SUFFIXES = {".o", ".d", ".elf", ".map"}
STAGING = {"isodir", "recovery-isodir", "console-isodir", "console-supervisor-isodir",
           "preemption-isodir", "lifecycle-isodir", "lifecycle-ids-isodir",
           "sleep-isodir", "integration-isodir", "readonly-isodir", "shared-handles-isodir"}


def outputs(root):
    build = root.resolve() / "build"
    if build.is_symlink():
        raise ValueError("refusing to clean a symlinked build directory")
    if not build.exists():
        return []
    files = []
    for directory, children, names in os.walk(build, followlinks=False):
        parent = Path(directory)
        # Generated VM bundles and validated releases are useful independent
        # artifacts. Preserve their complete contents, including copied ISOs.
        children[:] = [name for name in children if not (parent / name).is_symlink()
                       and not name.startswith("vm-images") and name not in {"disks", "release-check", "test-artifacts"}]
        for name in names:
            path = parent / name
            if not stat.S_ISREG(path.lstat().st_mode) or path.suffix.lower() in DISK_SUFFIXES:
                continue
            relative = path.relative_to(build)
            generated = path.suffix in OBJECT_SUFFIXES
            generated |= relative.parts[0] in STAGING and name in {"grub.cfg", "rum.elf"}
            generated |= relative in {Path("rum.iso"), Path("user/programs.txt"), Path("user/include/.abi-stamp"),
                                      Path("arch/i386/linker.ld"), Path("sdk/linker/user.ld"), Path("tools/check-user-elf")}
            generated |= relative.parts[:4] == ("user", "include", "rum", "abi") and path.suffix == ".h"
            generated |= parent == build / "tests" and (name.endswith("-test") or path.suffix == ".iso")
            generated |= parent == build / "user/system" and name.endswith(".elf.tmp")
            if generated:
                # The resolved target must remain in this exact build tree.
                if not path.resolve().is_relative_to(build):
                    raise ValueError(f"refusing to clean a path outside {build}")
                files.append(path)
    return files


def clean(root=ROOT):
    files = outputs(root)
    for path in files:
        path.unlink()
    return len(files)


def main():
    try:
        count = clean()
    except (OSError, ValueError) as error:
        print(f"rum clean: {error}", file=sys.stderr)
        return 1
    print(f"Removed {count} build outputs. Disks, VM bundles and release reports were preserved.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
