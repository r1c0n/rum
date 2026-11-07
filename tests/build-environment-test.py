#!/usr/bin/env python3
"""Verify workflow failure ordering, preview safety and selective cleanup."""
import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / f"{name}.py")
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


cli, cleaner = module("rum"), module("clean-build")


def rejects(arguments):
    with patch.object(cli, "run") as commands:
        try:
            cli.dispatch(cli.parser().parse_args(arguments))
        except (OSError, ValueError):
            assert not commands.called, "invalid options must not build or launch"
            return
    raise AssertionError("invalid workflow was accepted")


with tempfile.TemporaryDirectory(prefix="rum-build-env-") as temporary:
    root = Path(temporary)
    disk = root / "my disk, with $(literal).raw"
    disk.write_bytes(b"persistent canary" + bytes(512 - 17))
    original = disk.read_bytes()
    rejects(["run", "--disk", str(root / "absent.raw"), "--dry-run"])
    rejects(["prepare-disk", "--disk", str(disk)])
    rejects(["prepare-disk", "--disk", str(root / "new.raw"), "--size-mib", "1"])
    rejects(["run", "--read-only", "--dry-run"])
    rejects(["build", "--disk", str(disk)])
    rejects(["clean", "--no-build"])
    rejects(["build", "--suite", "host"])
    rejects(["vm-images", "--output", str(root)])
    with patch.object(cli.subprocess, "run") as execute, contextlib.redirect_stdout(io.StringIO()):
        cli.dispatch(cli.parser().parse_args(["run", "--disk", str(disk), "--dry-run"]))
        cli.dispatch(cli.parser().parse_args(["clean", "--dry-run"]))
        assert not execute.called
    with patch.object(cli, "run", side_effect=subprocess.CalledProcessError(7, "make")) as commands, \
            contextlib.redirect_stderr(io.StringIO()), patch.object(cli.os, "name", "posix"):
        assert cli.main(["run", "--dry-run"]) == 7
        assert commands.call_count == 1, "failed builds must not launch QEMU"
    assert disk.read_bytes() == original

    build = root / "build"
    generated = ["kernel/mm/heap.o", "kernel/mm/heap.d", "rum.elf", "rum.iso", "tests/path-test",
                 "user/include/.abi-stamp", "user/include/rum/abi/process.h", "sdk/linker/user.ld",
                 "isodir/boot/rum.elf", "isodir/boot/grub/grub.cfg", "user/programs.txt"]
    preserved = ["personal.raw", "tests/my-disk.img", "nested/vm.vmdk", "vm-images-custom/rum.iso",
                 "vm-images-custom/rum-data.vdi", "release-check/release/rum.elf",
                 "test-artifacts/guest.elf", "notes.txt", "disks/keep.elf"]
    for name in generated + preserved:
        path = build / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"canary")
    external = root / "external"; external.mkdir()
    (external / "keep.o").write_bytes(b"outside")
    try:
        (build / "linked").symlink_to(external, target_is_directory=True)
    except OSError:
        pass  # Windows may not grant symlink creation privileges.
    assert cleaner.clean(root) == len(generated)
    assert all(not (build / name).exists() for name in generated)
    assert all((build / name).read_bytes() == b"canary" for name in preserved)
    assert (external / "keep.o").read_bytes() == b"outside"
    assert cleaner.clean(root) == 0

print("PASS: build preflight, failure propagation, side-effect-free preview and disk-preserving cleanup")
