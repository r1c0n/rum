#!/usr/bin/env python3
"""Check real shell workflows against exact host FAT bytes on disposable disks."""
import argparse
import importlib.util
from pathlib import Path
import re
import struct
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("workflow_shell", ROOT / "tests/userspace-shell-test.py")
shell = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shell)
shell.ARTIFACTS = ROOT / "build/test-artifacts/workflows"
images = shell.images


def seed(disk, source, name, data):
    source.write_bytes(data)
    images.command("mcopy", "-i", str(disk), str(source), "::/" + name)


def run(qemu, disk, ram, iso):
    with shell.boot(qemu, ram, f"workflows-{ram}m", disk=disk, iso=iso) as console:
        console.command("echo island>OUT.TXT")
        console.command("echo life>>OUT.TXT")
        console.command("cat <OUT.TXT", "island\r\nlife\r\n")
        console.command("echo new >OUT.TXT")
        console.command("cat OUT.TXT", "new\r\n")
        console.command("cat INPUT.BIN|cat|cat|cat>COPY.BIN")
        console.command("cat EMPTY.BIN | cat > ZERO.BIN")
        console.command("echo left > FIRST.TXT > SECOND.TXT")
        console.command("echo once > SAME.TXT > SAME.TXT")
        console.command("echo bypass > OVERRIDE.TXT | cat > PIPEEOF.BIN")
        console.command("cat ABSENT.TXT > ERROR.BIN", "file or directory not found.")
        console.command("wflow stderr > STDOUT.TXT", "stderr stays on console\r\n")
        console.command("wflow authority", "pipeline authority preserved")
        console.command("wflow invalid-open", "open flags validated")
        console.command("wflow append RECORD.BIN A | wflow append RECORD.BIN B")
        # Failed middle publication cancels and reaps the already running spinner.
        console.command("spin | absent | cat", "file or directory not found.")
        console.command("spin | fault | cat", "Program stopped after a user fault.")
        console.command("echo x | nonzero", "Program exited with status -37.")
        console.command("nonzero | cat")  # Normal result is the rightmost stage.
        for command in ("echo bad >", "echo x || cat", "echo x|cat|cat|cat|cat", "|cat >INVALID.TXT"):
            console.command(command, "shell: invalid or oversized pipeline.")
        console.command("cd / > INVALID.TXT", "session commands cannot use pipes or redirection.")
        console.command("cat <MISSING.TXT > NEVER.TXT", "file or directory not found.")
        console.command("echo changed > OUT.TXT <MISSING.TXT", "file or directory not found.")
        console.command("echo no >/", "is a directory.")
        for _ in range(3):
            console.type("spin | cat > CANCEL.BIN\n")
            time.sleep(0.15)
            console.send("ctrl", "c")
            console.wait("Program cancelled.\r\n/> ")
        # CPU-bound stages cannot outrun cancellation, even with no pipe I/O.
        console.type("spin | spin\n")
        time.sleep(0.15); console.send("ctrl", "c")
        console.wait("Program cancelled.\r\n/> ")
        console.type("wflow input | cat > KEY.BIN\n")
        console.wait("input ready\r\n"); console.send("z"); console.wait("/> ")
        # A background process completing during wait-any is reported once.
        console.command("start sleep 1000", "started sleep.")
        console.type("sleep 1500 | cat\n")
        console.wait("/> ", timeout=10)
        console.command("echo restored", "restored\r\n")
        processes = console.command("ps", "shell.elf")
        assert re.findall(r"\d+  \d+  \w+  (\S+)", processes) == ["shell.elf", "ps.elf"], processes
        console.command("recovery", "Kernel recovery shell.")
        diag = console.command("diag")
        assert "Pipes: 0 live, 0 buffered bytes | 0 read ends, 0 write ends, 0 active operations, 0 waiters" in diag, diag
        assert re.search(r"Handles: 0 descriptors.*0 open descriptions.*0 aliases", diag), diag
        assert "Processes: 0 | 0 user tables, 0 user pages" in diag, diag
        assert "Sessions: 0 live, 0 closing, 0 members | 0 session refs, 0 process refs" in diag, diag
    assert images.file_bytes(disk, "/INPUT.BIN") == INPUT
    assert images.file_bytes(disk, "/COPY.BIN") == INPUT
    for name, expected in {"ZERO.BIN": b"", "FIRST.TXT": b"", "SECOND.TXT": b"left\n",
                           "SAME.TXT": b"once\n", "OVERRIDE.TXT": b"bypass\n", "PIPEEOF.BIN": b"",
                           "ERROR.BIN": b"", "STDOUT.TXT": b"output bytes\n", "KEY.BIN": b"z",
                           "OUT.TXT": b"", "CANCEL.BIN": b""}.items():
        assert images.file_bytes(disk, "/" + name) == expected, name
    records = images.file_bytes(disk, "/RECORD.BIN")
    assert len(records) == 64 * 32
    seen = {ord("A"): [], ord("B"): []}
    for offset in range(0, len(records), 32):
        record = records[offset:offset + 32]
        tag, sequence = record[:2]
        assert tag in seen and record[2:] == bytes(tag + j for j in range(2, 32))
        seen[tag].append(sequence)
    assert all(value == list(range(32)) for value in seen.values()), seen
    listing = images.command("mdir", "-b", "-i", str(disk), "::/").decode()
    assert not any(name in listing.upper() for name in ("NEVER.TXT", "INVALID.TXT"))
    images.audit(disk)
    with shell.boot(qemu, ram, f"workflows-reboot-{ram}m", disk=disk, iso=iso) as console:
        console.command("cat COPY.BIN > REBOOT.BIN")
    assert images.file_bytes(disk, "/REBOOT.BIN") == INPUT
    print(f"PASS: {ram} MiB workflows: exact binary/reboot bytes, append records, foreground input, faults, cancellation and cleanup", flush=True)


INPUT = bytes((i * 37 ^ (i >> 3)) & 255 for i in range(4096 * 3 + 173))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    args = parser.parse_args()
    shell.ARTIFACTS.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rum-workflow-") as temporary:
        disk, source = Path(temporary) / "disk.raw", Path(temporary) / "source"
        images.build(disk)
        images.seed_system(disk, {"WFLOW.ELF": ROOT / "build/tests/user/workflow.elf"})
        seed(disk, source, "INPUT.BIN", INPUT)
        seed(disk, source, "EMPTY.BIN", b"")
        original = disk.read_bytes()
        for ram, iso in ((16, False), (64, True)):
            disk.write_bytes(original)
            run(args.qemu, disk, ram, iso)
        disk.write_bytes(original)
        with shell.boot(args.qemu, 16, "workflows-readonly", disk=disk, options="rum.disk-readonly") as console:
            console.command("echo no >OUT.TXT", "read-only filesystem.")
            console.command("cat INPUT.BIN | cat >OUT.TXT", "read-only filesystem.")
            console.command("echo still usable | cat", "still usable\r\n")
        assert disk.read_bytes() == original
        # Fill through mtools so every reserved cluster belongs to a valid file.
        data = disk.read_bytes(); g = images.fat.geometry(data)
        table = data[g["reserved"] * 512:(g["reserved"] + g["fat"]) * 512]
        free = sum(struct.unpack_from("<H", table, c * 2)[0] == 0 for c in range(2, g["clusters"] + 2))
        seed(disk, source, "FILLER.BIN", b"F" * (free * g["cluster"] * 512))
        with shell.boot(args.qemu, 16, "workflows-full", disk=disk) as console:
            console.command("echo no >FULL.TXT", "filesystem is full.")
            console.command("echo still usable | cat", "still usable\r\n")
        images.audit(disk)
    print("PASS: read-only/full media preserve the shell and existing volume", flush=True)


if __name__ == "__main__":
    main()
