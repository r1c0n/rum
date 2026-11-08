#!/usr/bin/env python3
"""Validate user file tools and persistence on disposable FAT16 disks."""
import argparse
import importlib.util
from pathlib import Path
import re
import struct
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("file_tools_shell", ROOT / "tests/userspace-shell-test.py")
shell = importlib.util.module_from_spec(spec); spec.loader.exec_module(shell)
shell.ARTIFACTS = ROOT / "build/test-artifacts/file-tools"
images = shell.images
PAYLOAD = bytes((i * 37 ^ i >> 3) & 255 for i in range(3 * 4096 + 173))


def run(qemu, disk, ram, iso):
    baseline = images.audit(disk); original = disk.read_bytes()
    with shell.boot(qemu, ram, f"file-tools-{ram}m", disk=disk, iso=iso) as c:
        c.command("filetest validate", "file metadata and rename ABI verified")
        c.command("stat /HOME", "file or directory not found.")
        c.command("stat /APPS", "file or directory not found.")
        c.command("touch /HOME"); c.command("setup", "not a directory."); c.command("rm /HOME")
        c.command("touch /APPS"); c.command("setup", "not a directory.")
        c.command("stat /HOME", "Type: directory"); c.command("rm /APPS")
        c.command("setup", "Workspace ready: /HOME and /APPS.")
        c.command("setup", "Workspace ready: /HOME and /APPS.")
        c.command("touch TARGET.BIN"); c.command("touch EMPTY.BIN"); c.command("touch EMPTY.BIN")
        c.command("stat BINARY.BIN", f"Type: file\r\nSize: {len(PAYLOAD)} bytes")
        c.command("stat /HOME", "Type: directory")
        c.command("touch /HOME", "is a directory.")
        c.command("cp BINARY.BIN ./binary.bin", "source and destination are the same file.")
        c.command("cp -f BINARY.BIN /BINARY.BIN", "source and destination are the same file.")
        c.command("cp / BOGUS.BIN", "is a directory.")
        c.command("cp BINARY.BIN /HOME", "is a directory.")
        c.command("cp BINARY.BIN TOOLONG99.BIN", "path or name is too long.")
        c.command("cp BINARY.BIN COPY.BIN"); c.command("cp BINARY.BIN COPY.BIN", "already exists.")
        c.command("cp -f TARGET.BIN COPY.BIN"); c.command("cp -f BINARY.BIN COPY.BIN")
        c.command("cp TARGET.BIN EXIST.BIN")
        c.command("mv COPY.BIN EXIST.BIN", "already exists.")
        c.command("mv -f COPY.BIN EXIST.BIN"); c.command("mv EXIST.BIN /HOME/MOVED.BIN")
        c.command("cp TARGET.BIN AGAIN.BIN"); c.command("mv --copy AGAIN.BIN /HOME/COPYMOVE.BIN")
        c.command("cp EMPTY.BIN ZERO.BIN")
        c.command("mv ZERO.BIN /FULL/ZERO.BIN")  # Requires directory growth.
        c.command("mv /HOME /OTHER", "is a directory.")
        c.command("mv TARGET.BIN /MISSING/../NO.BIN", "file or directory not found.")
        c.command("cp /RUM/HELLO.ELF /APPS/HELLO.ELF")
        c.command("/APPS/HELLO.ELF", "Hello from rum userspace!")
        c.command("cd /HOME"); c.command("cp ../TARGET.BIN LOCAL.BIN"); c.command("mv LOCAL.BIN OTHER.BIN"); c.command("cd /")
        output = c.command("start filetest hold", "started filetest.")
        pid = int(re.search(r"\[(\d+)\] started filetest", output)[1])
        deadline = time.monotonic() + 10
        while b"file pin ready\r\n" not in c.serial.read_bytes():
            assert time.monotonic() < deadline; time.sleep(0.01)
        c.command("mv TARGET.BIN BUSY.BIN", "file or directory is in use.")
        c.command("cp -f BINARY.BIN TARGET.BIN", "file or directory is in use.")
        c.command("mv -f /HOME/OTHER.BIN TARGET.BIN", "file or directory is in use.")
        c.command("touch TARGET.BIN")
        c.command(f"kill {pid}", "Cancellation requested")
        c.command("sync")
        c.command("sync extra", "Usage: sync")
        c.command("recovery", "Kernel recovery shell."); diag = c.command("diag")
        assert "Handles: 0 descriptors, 0 open descriptions, 0 aliases" in diag and "Processes: 0 | 0 user tables, 0 user pages" in diag, diag
    state = images.audit(disk)
    for name, expected in (("/BINARY.BIN", PAYLOAD), ("/HOME/MOVED.BIN", PAYLOAD), ("/FULL/ZERO.BIN", b""),
                           ("/EMPTY.BIN", b""), ("/HOME/OTHER.BIN", baseline["files"]["/TARGET.BIN"]),
                           ("/HOME/COPYMOVE.BIN", baseline["files"]["/TARGET.BIN"]),
                           ("/APPS/HELLO.ELF", baseline["files"]["/RUM/HELLO.ELF"])):
        assert state["files"][name] == expected and images.file_bytes(disk, name) == expected, name
    for name in ("/COPY.BIN", "/EXIST.BIN", "/AGAIN.BIN", "/BUSY.BIN", "/BOGUS.BIN"):
        assert name not in state["files"], name
    slot = baseline["slots"]["/TARGET.BIN"]
    assert disk.read_bytes()[slot:slot + 32] == original[slot:slot + 32], "touch changed an existing FAT entry"
    for name, contents in baseline["files"].items():
        assert state["files"][name] == contents, name
    images.command("mdir", "-i", str(disk), "::/HOME")
    with shell.boot(qemu, ram, f"file-tools-reboot-{ram}m", disk=disk, iso=iso) as c:
        c.command("stat /HOME/MOVED.BIN", f"Size: {len(PAYLOAD)} bytes")
        c.command("/APPS/HELLO.ELF", "Hello from rum userspace!")
    print(f"PASS: {ram} MiB file tools, metadata validation, binary copies, rename/replace, busy files, workspace and separate reboot", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--qemu", default="qemu-system-i386")
    args = parser.parse_args(); shell.ARTIFACTS.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rum-file-tools-") as temporary:
        disk, source = Path(temporary) / "disk.raw", Path(temporary) / "source"
        images.build(disk); images.seed_system(disk, {"FILETEST.ELF": ROOT / "build/tests/user/file-tools.elf"})
        source.write_bytes(PAYLOAD); images.command("mcopy", "-i", str(disk), str(source), "::/BINARY.BIN")
        original = disk.read_bytes()
        for ram, iso in ((16, False), (64, True)):
            disk.write_bytes(original); run(args.qemu, disk, ram, iso)
        disk.write_bytes(original)
        with shell.boot(args.qemu, 16, "file-tools-readonly", disk=disk, options="rum.disk-readonly") as c:
            c.command("sync")
            c.command("touch TARGET.BIN")
            c.command("stat TARGET.BIN", "Volume: read-only")
            for command in ("touch NEW.BIN", "cp TARGET.BIN NEW.BIN", "mv TARGET.BIN NEW.BIN", "setup"):
                c.command(command, "read-only filesystem.")
            c.command("echo usable", "usable\r\n")
        assert disk.read_bytes() == original
        # Fill every remaining cluster through mtools; no orphan allocations.
        g = images.fat.geometry(original); table = original[g["reserved"] * 512:(g["reserved"] + g["fat"]) * 512]
        free = sum(struct.unpack_from("<H", table, i * 2)[0] == 0 for i in range(2, g["clusters"] + 2))
        source.write_bytes(b"F" * (free * g["cluster"] * 512))
        images.command("mcopy", "-i", str(disk), str(source), "::/FILLER.BIN")
        with shell.boot(args.qemu, 16, "file-tools-full", disk=disk) as c:
            c.command("cp TARGET.BIN NEW.BIN", "filesystem is full.")
            c.command("mv TARGET.BIN /FULL/MOVED.BIN", "filesystem is full.")
            c.command("mv TARGET.BIN RENAMED.BIN")
            c.command("sync")
            c.command("echo usable", "usable\r\n")
        images.audit(disk)
        assert images.file_bytes(disk, "/RENAMED.BIN") == bytes((i * 17 + 3) & 255 for i in range(1537))
    print("PASS: read-only/full media preserve source bytes and shell usability; same-directory rename needs no free clusters", flush=True)


if __name__ == "__main__": main()
