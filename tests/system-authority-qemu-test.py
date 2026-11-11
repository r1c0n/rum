#!/usr/bin/env python3
"""Exercise system protection and recovery installs on disposable FAT16 volumes."""
import argparse
import importlib.util
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("authority_shell", ROOT / "tests/userspace-shell-test.py")
shell = importlib.util.module_from_spec(spec); spec.loader.exec_module(shell)
shell.ARTIFACTS = ROOT / "build/test-artifacts/system-authority"
images = shell.images


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    args = parser.parse_args()
    shell.ARTIFACTS.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rum-authority-") as temporary:
        work = Path(temporary); disk = work / "system.raw"; source = work / "source"
        images.build(disk); images.seed_system(disk)
        images.command("mmd", "-i", str(disk), "::/APPS", "::/DATA", "::/DOCS", "::/RUM/SUB")
        executable = ROOT / "build/tests/user/system-authority.elf"
        images.command("mcopy", "-i", str(disk), str(executable), "::/APPS/SHELL.ELF")
        images.command("mcopy", "-i", str(disk), str(executable), "::/RUM/AUTH.ELF")
        for path, contents in (("/RUM/CORE.BIN", b"trusted"), ("/RUM/SUB/CFG", b"cfg"), ("/RUM/TEST.BIN", b"old")):
            source.write_bytes(contents); images.command("mcopy", "-i", str(disk), str(source), "::" + path)
        payload = images.pattern(65537)
        source.write_bytes(payload); images.command("mcopy", "-i", str(disk), str(source), "::/DATA/UPDATE.BIN")
        baseline = images.audit(disk)["files"]
        for ram, iso in ((16, False), (64, True)):
            with shell.boot(args.qemu, ram, f"authority-{ram}m", disk=disk, iso=iso) as console:
                console.command("write /rum/TEST.BIN denied", "access denied")
                console.command("run /APPS/SHELL.ELF disk 00000000", "System authority checks passed.")
                console.command("run /RUM/AUTH.ELF disk 00000000", "System authority checks passed.")
                console.command("run /APPS/SHELL.ELF io 00000000", "Program stopped after a user fault.")
                console.command("cat /RUM/CORE.BIN", "trusted")
                console.command("recovery", "Kernel recovery shell.")
                console.command("install /DATA/UPDATE.BIN /RUM/TEST.BIN", "Installed 65537 bytes.")
                console.command("install /RUM/TEST.BIN /rum/test.bin", "invalid request")
                console.command("install /DATA/UPDATE.BIN /DATA/BAD.BIN", "invalid request")
                diag = console.command("diag")
                for expected in ("Processes: 0 | 0 user tables, 0 user pages",
                                 "Handles: 0 descriptors, 0 open descriptions, 0 aliases",
                                 "Object refs: 0 owners, 0 table entries, 0 operations"):
                    assert expected in diag, diag
            current = images.audit(disk)["files"]
            for path, contents in baseline.items():
                assert current[path] == (payload if path == "/RUM/TEST.BIN" else contents), path
            assert current["/APPS/KEEP.TXT"] == b"Ordinary"
            assert current["/RUM/TEST.BIN"] == payload and "/DATA/BAD.BIN" not in current
            print(f"PASS: {ram} MiB: system writes denied, untrusted ELF names/paths, writable apps, fault recovery and 65537-byte trusted install", flush=True)
        with shell.boot(args.qemu, 16, "authority-persist", disk=disk) as console:
            console.command("cp /RUM/TEST.BIN /DATA/VERIFY.BIN")
            console.command("wc -c /DATA/VERIFY.BIN", "65537 /DATA/VERIFY.BIN")
            console.command("write /RUM/TEST.BIN denied", "access denied")
        assert images.file_bytes(disk, "/RUM/TEST.BIN") == payload
        assert images.file_bytes(disk, "/DATA/VERIFY.BIN") == payload
        images.audit(disk)
        print("PASS: system file survives a separate boot and host FAT audit; normal startup never updates it", flush=True)


if __name__ == "__main__":
    main()
