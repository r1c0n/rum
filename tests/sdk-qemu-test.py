#!/usr/bin/env python3
"""Run an externally built SDK app and optional upstream Doom I/O probe."""
import argparse
import importlib.util
from pathlib import Path
import struct
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("sdk_shell", ROOT / "tests/userspace-shell-test.py")
shell = importlib.util.module_from_spec(spec); spec.loader.exec_module(shell)
shell.ARTIFACTS = ROOT / "build/test-artifacts/sdk"
images = shell.images


def wad_bytes():
    return b"PWAD" + struct.pack("<II", 1, 25) + b"rum\0wad\xffprobe" + struct.pack("<II8s", 12, 13, b"RUMTEST\0")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--doom", action="store_true")
    args = parser.parse_args()
    shell.ARTIFACTS.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rum-sdk-guest-") as temporary:
        disk = Path(temporary) / "sdk.raw"; source = Path(temporary) / "source"
        images.build(disk); images.seed_system(disk)
        images.command("mcopy", "-i", str(disk), str(ROOT / "build/tests/sdk-app.elf"), "::/RUM/SDKAPP.ELF")
        images.command("mcopy", "-i", str(disk), str(ROOT / "build/tests/sdk-stress.elf"), "::/RUM/SDKMIX.ELF")
        legacy = ROOT / "build/tests/sdk07-app.elf"
        if legacy.is_file():
            images.command("mcopy", "-i", str(disk), str(legacy), "::/RUM/SDK07.ELF")
        if args.doom:
            images.command("mcopy", "-i", str(disk), str(ROOT / "build/doom-probe/DOOMIO.elf"), "::/RUM/DOOMIO.ELF")
            source.write_bytes(wad_bytes()); images.command("mcopy", "-i", str(disk), str(source), "::/TEST.WAD")
        original = disk.read_bytes()
        for ram, iso in ((16, False), (64, True)):
            disk.write_bytes(original)
            with shell.boot(args.qemu, ram, f"sdk-{ram}m", disk=disk, iso=iso) as console:
                for _ in range(2):
                    console.command("run /RUM/SDKAPP.ELF /SDKTEST.BIN", "External SDK application passed.")
                    console.command("run /RUM/SDKAPP.ELF --fault", "Program stopped after a user fault.")
                    console.type("run /RUM/SDKAPP.ELF --spin\n"); console.wait("sdk cancellation ready\r\n")
                    console.send("ctrl", "c"); console.wait("Program cancelled.\r\n/> ")
                if legacy.is_file():
                    console.command("run /RUM/SDK07.ELF /COMPAT.BIN", "External SDK application passed.")
                    console.command("run /RUM/SDK07.ELF --fault", "Program stopped after a user fault.")
                    console.type("run /RUM/SDK07.ELF --spin\n"); console.wait("sdk cancellation ready\r\n")
                    console.send("ctrl", "c"); console.wait("Program cancelled.\r\n/> ")
                if args.doom:
                    console.command("run /RUM/DOOMIO.ELF /TEST.WAD", "Doom zone and WAD I/O probe passed.")
                # Repeated FAT writes, memory pressure and fault/cancel rounds
                # can exceed an interactive command's bound on a busy host.
                # Polling still returns immediately when the workload finishes.
                console.command("run /RUM/SDKMIX.ELF", "SDK mixed memory and I/O passed.", timeout=60)
                console.command("echo recovered", "recovered")
                console.command("recovery", "Kernel recovery shell.")
                diag = console.command("diag")
                for expected in ("Processes: 0 | 0 user tables, 0 user pages",
                                 "Memory regions: 0 | dynamic: 0 | buffer leases: 0", "Paging metadata: 0 bytes",
                                 "Handles: 0 descriptors, 0 open descriptions, 0 aliases", "Terminal: normal",
                                 "Objects: 0 allocated | 0 published, 0 constructing, 0 destroying",
                                 "Object refs: 0 owners, 0 table entries, 0 operations"):
                    assert expected in diag, diag
            assert images.file_bytes(disk, "/SDKTEST.BIN") == bytes((i * 17) & 255 for i in range(8193))
            if legacy.is_file():
                assert images.file_bytes(disk, "/COMPAT.BIN") == bytes((i * 17) & 255 for i in range(8193))
                print(f"PASS: {ram} MiB released 0.7 SDK application, exact bytes, faults and cancellation", flush=True)
            for seed in range(1, 6):
                round = 3 if seed <= 3 else 0
                expected = bytes(((i * 37) ^ (i >> 7) ^ (seed * 23) ^ (round * 11)) & 255 for i in range(8193))
                assert images.file_bytes(disk, f"/S{seed}.BIN") == expected
            if args.doom:
                assert images.file_bytes(disk, "/TEST.WAD") == wad_bytes()
            images.audit(disk)
            print(f"PASS: {ram} MiB external SDK application, exact file bytes, recoverable failure, fault/cancel and cleanup", flush=True)


if __name__ == "__main__":
    main()
