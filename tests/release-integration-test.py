#!/usr/bin/env python3
"""Check release boots, persistence, full media and errors through the real shell."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import struct
import tempfile
from zipfile import ZipFile

from importlib.util import spec_from_file_location, module_from_spec

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "build/test-artifacts/release-integration"
spec = spec_from_file_location("release_shell", ROOT / "tests/userspace-shell-test.py")
shell = module_from_spec(spec)
spec.loader.exec_module(shell)
shell.ARTIFACTS = ARTIFACTS
images = shell.images
WORKFLOW_BYTES = b"island  bytes\nliteral | > &\n"


def background(console, command, early_fault=False):
    output = console.command(command + " &")
    match = re.search(r"\[(\d+)\] started ", output)
    # Members can run before COMMIT. A fault during construction cancels and
    # collects the partial job synchronously instead of publishing a job ID.
    if not match and early_fault:
        assert "Program stopped after a user fault." in output, output
        return None
    assert match, output
    return int(match[1])


def workflows(console):
    """One production scenario shared by release and native-host ISO checks."""
    console.command("setup", "Workspace ready: /HOME and /APPS.")
    console.command("echo 'island  bytes' | cat > /HOME/NOTES.TXT")
    console.command("echo 'literal | > &' >> /HOME/NOTES.TXT")
    console.command("cp /HOME/NOTES.TXT /HOME/COPY.TXT")
    console.command("mv /HOME/COPY.TXT /HOME/SAVED.TXT")
    console.command("cat /HOME/SAVED.TXT", WORKFLOW_BYTES.decode().replace("\n", "\r\n"))
    console.command("cat /RUM/HELLO.ELF | cat > /HOME/EXACT.ELF")
    console.command("cp /HOME/EXACT.ELF /APPS/HELLO.ELF")
    console.command("/APPS/HELLO.ELF", "Hello from rum userspace!")
    blocked = background(console, "spin | cat > /HOME/CANCEL.BIN")
    console.type(f"fg {blocked}\n"); console.wait(f"Foreground job {blocked}.\r\n")
    console.send("ctrl", "c"); console.wait("Program cancelled.\r\n/> ")
    failed = background(console, "spin | fault | cat > /HOME/FAULT.BIN", early_fault=True)
    if failed is not None:
        console.command(f"wait {failed}", "Program stopped after a user fault.")
    abandoned = background(console, "spin | cat > /HOME/RESTART.BIN")
    console.command("exit", "Type 'help' for commands.")
    console.command("jobs", "No jobs.")
    console.command(f"fg {abandoned}", "job not found.")
    console.command("sync")


def resource_baseline(console):
    diagnostic = console.command("diag")
    for expected in ("Processes: 0 | 0 user tables, 0 user pages",
                     "Sessions: 0 live, 0 closing, 0 members | 0 session refs, 0 process refs",
                     "Handles: 0 descriptors, 0 open descriptions, 0 aliases",
                     "Jobs: 0 live, 0 running, 0 completed, 0 active waits",
                     "Pipes: 0 live, 0 buffered bytes | 0 read ends, 0 write ends, 0 active operations, 0 waiters",
                     "Terminal: normal"):
        assert expected in diagnostic, diagnostic


def recovery(console, active_shell=True):
    if active_shell: console.command("recovery", "Kernel recovery shell.")
    resource_baseline(console)
    console.command("mem", "Heap:")
    console.type("snake\n"); console.wait("rum_snake_started\r\n")
    console.send("q"); console.wait("> ")
    console.command("echo recovered", "recovered")


def canaries(before, after):
    end = images.fat.geometry(before)["total"] * 512
    assert after[end:] == before[end:], "write escaped the BPB volume"


def leave_one_cluster(disk):
    """Create a valid nearly full volume using mtools, not artificial bad clusters."""
    data = disk.read_bytes()
    g = images.fat.geometry(data)
    free = sum(struct.unpack_from("<H", data, g["reserved"] * 512 + c * 2)[0] == 0
               for c in range(2, g["clusters"] + 2))
    filler = disk.with_suffix(".filler")
    filler.write_bytes(b"F" * ((free - 1) * g["cluster"] * 512))
    images.command("mcopy", "-i", str(disk), str(filler), "::/FILLER.BIN")
    filler.unlink()
    images.audit(disk)


def matrix(qemu, ram, iso, temporary):
    kind = "iso" if iso else "elf"
    prefix = f"{ram}m-{kind}"
    disk = temporary / (prefix + ".raw")
    images.build(disk); images.seed_system(disk)
    original = disk.read_bytes()
    original_files = images.audit(disk)["files"]
    note = "rum release bytes  with spaces  "
    with shell.boot(qemu, ram, prefix + "-write", disk=disk, iso=iso) as console:
        console.command('write /notes.txt "' + note + '"')
        console.command("cat /notes.txt", "\r\n" + note + "\r\n> ")
        workflows(console)
        recovery(console)
    assert images.file_bytes(disk, "/NOTES.TXT") == note.encode()
    state = images.audit(disk)
    for name in ("/HOME/NOTES.TXT", "/HOME/SAVED.TXT"):
        assert state["files"][name] == WORKFLOW_BYTES and images.file_bytes(disk, name) == WORKFLOW_BYTES
    for name in ("/HOME/EXACT.ELF", "/APPS/HELLO.ELF"):
        assert state["files"][name] == original_files["/RUM/HELLO.ELF"]
        assert images.file_bytes(disk, name) == original_files["/RUM/HELLO.ELF"]
    for name in ("/HOME/CANCEL.BIN", "/HOME/FAULT.BIN", "/HOME/RESTART.BIN"):
        assert state["files"][name] == b""
    saved = disk.read_bytes()
    with shell.boot(qemu, ram, prefix + "-reboot", disk=disk, iso=iso) as console:
        console.command("cat /notes.txt", "\r\n" + note + "\r\n> ")
        console.command("cat /HOME/SAVED.TXT", WORKFLOW_BYTES.decode().replace("\n", "\r\n"))
        console.command("/APPS/HELLO.ELF", "Hello from rum userspace!")
    assert disk.read_bytes() == saved
    saved_files = images.audit(disk)["files"]
    assert all(saved_files[name] == data for name, data in original_files.items())
    canaries(original, saved)
    with shell.boot(qemu, ram, prefix + "-missing", iso=iso, marker="Kernel recovery shell.") as console:
        console.command("ls /", "unavailable filesystem")
        recovery(console, False)
    damaged = bytearray(original)
    damaged[13] = 3
    disk.write_bytes(damaged)
    with shell.boot(qemu, ram, prefix + "-malformed", disk=disk, iso=iso, marker="Kernel recovery shell.") as console:
        console.command("ls /", "unavailable filesystem")
        recovery(console, False)
    assert disk.read_bytes() == damaged
    disk.write_bytes(original)
    leave_one_cluster(disk)
    before = disk.read_bytes()
    before_files = images.audit(disk)["files"]
    with shell.boot(qemu, ram, prefix + "-full", disk=disk, iso=iso) as console:
        console.command("write /LAST.TXT last cluster")
        console.command("write /FAIL.TXT too full", "filesystem is full.")
        console.command("write /LAST.TXT replacement", "filesystem is full.")
        console.command("cat /LAST.TXT", "last cluster")
        console.command("rm /LAST.TXT")
        console.command("write /REUSED.TXT reused cluster")
        recovery(console)
    assert images.file_bytes(disk, "/REUSED.TXT") == b"reused cluster"
    state = images.audit(disk)
    assert "/FAIL.TXT" not in state["files"] and "/LAST.TXT" not in state["files"]
    assert all(state["files"][name] == data for name, data in before_files.items())
    canaries(before, disk.read_bytes())
    disk.write_bytes(original)
    readonly_image = ROOT / "build/tests/readonly.iso" if iso else None
    with shell.boot(qemu, ram, prefix + "-readonly", disk=disk, iso=iso,
                    image=readonly_image, options="" if iso else "rum.disk-readonly") as console:
        console.command("write /NO.TXT nope", "read-only filesystem.")
        console.command("mkdir /NO", "read-only filesystem.")
        console.command("cat /KEEP.BIN")
        recovery(console)
    assert disk.read_bytes() == original
    print(f"PASS: {prefix} valid/reboot/no-disk/malformed/full/read-only, exact host notes.txt and recovery", flush=True)


def errors(qemu, ram, iso, temporary):
    """QEMU injects EIO below the actual production ATA driver and FAT backend."""
    for event in ("read_aio", "write_aio", "flush_to_disk"):
        label = f"{ram}m-{'iso' if iso else 'elf'}-{event}"
        disk, config = temporary / (label + ".raw"), temporary / (label + ".conf")
        images.build(disk); images.seed_system(disk)
        original = disk.read_bytes()
        state = images.audit(disk)
        rule = '[inject-error]\nevent = "' + event + '"\nerrno = "5"\nonce = "on"\n'
        if event == "read_aio":
            first = struct.unpack_from("<H", original, state["slots"]["/TARGET.BIN"] + 26)[0]
            rule += 'sector = "' + str(state["geometry"]["start"] + first - 2) + '"\n'
        if event == "flush_to_disk":
            rule += 'iotype = "flush"\n'
        config.write_text(rule)
        device = ["-drive", f"if=none,id=rum-errors,file=blkdebug:{config}:{disk},format=raw,cache=writeback",
                  "-device", "ide-hd,drive=rum-errors,bus=ide.1,unit=0,werror=report,rerror=report"]
        with shell.boot(qemu, ram, label, iso=iso, disk_arguments=device) as console:
            if event == "read_aio":
                console.command("cat /TARGET.BIN", "I/O failure.")
                console.command("ls /", "KEEP.BIN")
            else:
                console.command("write /FAIL.TXT injected failure", "I/O failure.")
                console.command("ls /", "I/O failure.")
            recovery(console)
        after = disk.read_bytes()
        canaries(original, after)
        final = images.audit(disk)
        assert final["files"] == state["files"], "unrelated file bytes changed"
        if event == "read_aio":
            assert after == original
        print(f"PASS: {label}, bounded ATA error, console/diagnostics/recovery/Snake remain usable", flush=True)


def packaged(qemu, archive):
    with ZipFile(archive) as package, tempfile.TemporaryDirectory(prefix="rum-release-iso-") as temporary:
        assert package.testzip() is None
        image = Path(temporary) / "rum.iso"
        image.write_bytes(package.read("rum.iso"))
        assert image.read_bytes() == (ROOT / "build/rum.iso").read_bytes()
        for ram in (16, 64, 256, 1152):
            with shell.boot(qemu, ram, f"packaged-iso-{ram}m", iso=True, image=image) as console:
                console.command("hello", "Hello from rum userspace!")
                console.command("cat /README.TXT")
                console.command("ls /rum", "SHELL.ELF")
                console.command("echo 'packaged | bytes' | cat > CHECK.TXT")
                console.command("cat CHECK.TXT", "packaged | bytes\r\n")
                console.command("sync")
        return hashlib.sha256(image.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--packaged-only", action="store_true")
    args = parser.parse_args()
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    if not args.packaged_only:
        with tempfile.TemporaryDirectory(prefix="rum-release-disks-") as temporary:
            for ram in (16, 64):
                for iso in ((False,) if ram == 16 else (True,)):
                    matrix(args.qemu, ram, iso, Path(temporary))
                    errors(args.qemu, ram, iso, Path(temporary))
    digest = packaged(args.qemu, ROOT / "rum.zip")
    (ARTIFACTS / "result.json").write_text(json.dumps({"iso_sha256": digest,
        "packaged_ram_mib": [16, 64, 256, 1152], "packaged_only": args.packaged_only}, indent=2) + "\n")
    print("PASS: exact packaged standalone ISO at 16/64/256/1152 MiB, SHA256 " + digest, flush=True)


if __name__ == "__main__":
    main()
