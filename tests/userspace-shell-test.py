#!/usr/bin/env python3
"""Drive the real userspace console and verify disposable FAT images on the host."""
import argparse
from contextlib import contextmanager
import importlib.util
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "build/test-artifacts/userspace-shell"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


smoke = load("shell_smoke", "scripts/smoke-test.py")
launcher = load("shell_launcher", "scripts/run-qemu.py")
images = load("shell_images", "tests/fat16-write-images.py")


class Console:
    def __init__(self, stream, serial):
        self.stream, self.serial = stream, serial
        self.send, _, self.type = smoke.guest_keyboard(stream, serial)

    def wait(self, suffix, timeout=10):
        deadline = time.monotonic() + timeout
        while not self.serial.read_bytes().endswith(suffix.encode()):
            text = self.serial.read_text(errors="replace")
            if "rum kernel panic" in text or time.monotonic() > deadline:
                raise AssertionError(f"Expected {suffix!r}: {text[-3000:]}")
            time.sleep(0.02)

    def command(self, text, expected=None):
        start = self.serial.stat().st_size
        self.type(text + "\n")
        self.wait("> ")
        result = self.serial.read_bytes()[start:].decode(errors="replace")
        if expected is not None:
            assert expected in result, (text, expected, result)
        return result


@contextmanager
def boot(qemu, ram, label, disk=None, iso=False, options="", readonly=False, marker="rum userspace shell."):
    with tempfile.TemporaryDirectory(prefix="rum-shell-qmp-") as temporary:
        serial, monitor = Path(temporary) / "serial", Path(temporary) / "qmp"
        arguments = launcher.boot_arguments(ROOT / ("build/rum.iso" if iso else "build/rum.elf"),
            kernel=not iso, disk=disk, read_only=readonly)
        if options:
            arguments += ["-append", options]
        process = subprocess.Popen([qemu, "-m", f"{ram}M", "-display", "none", "-no-reboot", "-no-shutdown",
            "-serial", f"file:{serial}", "-qmp", f"unix:{monitor},server=on,wait=off", *arguments],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 45
            while True:
                text = serial.read_text(errors="replace") if serial.exists() else ""
                if marker in text and (text.endswith("> ") or (options == "rum.recovery" and text.endswith("rum_boot_ok\n"))):
                    break
                if "rum kernel panic" in text or process.poll() is not None or time.monotonic() > deadline:
                    raise AssertionError(f"{label}: boot failed: {text[-5000:]}")
                time.sleep(0.02)
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
                connection.settimeout(10)
                connection.connect(str(monitor))
                with connection.makefile("rwb") as stream:
                    json.loads(stream.readline())
                    smoke.qmp_command(stream, "qmp_capabilities")
                    yield Console(stream, serial)
                    smoke.qmp_command(stream, "quit")
            process.wait(timeout=10)
        finally:
            if process.poll() is None:
                process.kill(); process.wait(timeout=5)
            if serial.exists():
                (ARTIFACTS / f"{label}.serial.log").write_bytes(serial.read_bytes())


def commands(console):
    console.command("help", "cat <path>")
    console.command("about", "rum OS v0.3.0")
    console.command("echo island life", "\r\nisland life\r\n> ")
    console.command("pwd", "\r\n/\r\n> ")
    console.command("write local.txt RAM file bytes")
    console.command("cat /local.txt", "\r\nRAM file bytes\r\n> ")
    console.command("write empty.txt")
    console.command("cat empty.txt", "cat empty.txt\r\n> ")
    console.command("ls /", "shell.elf\r\n")
    console.command("cat /", "is a directory.")
    console.command("cd local.txt", "not a directory.")
    console.command("run absent", "file or directory not found.")
    console.command("run local.txt", "not a supported executable.")
    console.command("run hello", "Hello from rum userspace!\r\nProgram exited with status 0.")
    console.command("run nonzero", "Program exited with status -37.")
    result = console.command("run fault", "Program stopped after a user fault.")
    assert "0x" not in result and "vector=" not in result
    console.type("run readline Z\n")
    console.wait("ZEnter text: ")
    console.type("child owns the keyboard\n")
    console.wait("Child read: child owns the keyboard\r\nProgram exited with status 5.\r\n> ")
    console.command("echo parent has input", "\r\nparent has input\r\n> ")
    console.type("run readline\n")
    console.wait("Enter text: ")
    console.send("ctrl", "c")
    console.wait("Program cancelled.\r\n> ")
    console.type("run spin\n")
    time.sleep(0.2)
    console.send("ctrl", "c")
    console.wait("Program cancelled.\r\n> ")
    console.type("half a command")
    console.send("ctrl", "c")
    console.wait("^C\r\n> ")
    console.command("echo restored", "\r\nrestored\r\n> ")
    console.command("clear", "\x1b[2J\x1b[H> ")
    console.type("snake\n")
    console.wait("rum_snake_started\r\n")
    console.send("q")
    console.wait("rum_snake_quit\r\n> ")


def disk_commands(console):
    console.command("ls //disk/./DOCS", "NOTE.TXT\r\n")
    console.command("cd /disk/DOCS")
    console.command("pwd", "\r\n/disk/DOCS\r\n> ")
    console.command("cat ./NOTE.TXT", "\r\nA\\x00\\xFF\\x1B\\x0D\r\n> ")
    console.command("cat EMPTY.TXT", "cat EMPTY.TXT\r\n> ")
    console.command("ls NOTE.TXT", "not a directory.")
    console.command("cat MISSING.TXT", "file or directory not found.")
    console.command("cd ../../", "invalid path, name or arguments.")
    console.command("pwd", "\r\n/disk/DOCS\r\n> ")
    console.command("run /disk/HELLO.ELF", "Hello from rum userspace!")
    console.command("run /disk/BAD.ELF", "not a supported executable.")
    console.command("pwd", "\r\n/disk/DOCS\r\n> ")
    console.command("write NEW.TXT persistent shell bytes")
    console.command("cat NEW.TXT", "\r\npersistent shell bytes\r\n> ")
    console.command("mkdir CHILD")
    console.command("cd CHILD")
    console.command("write LEAF.TXT leaf")
    console.command("cd ..")
    console.command("rm CHILD", "directory is not empty.")
    console.command("rm CHILD/LEAF.TXT")
    console.command("rm CHILD")
    console.command("cd /")
    console.command("cat local.txt", "RAM file bytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    args = parser.parse_args()
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rum-shell-disk-") as temporary:
        disk, source = Path(temporary) / "fat16.raw", Path(temporary) / "source"
        images.build(disk)
        images.command("mmd", "-i", str(disk), "::/DOCS")
        for name, contents in (("DOCS/NOTE.TXT", b"A\0\xff\x1b\r\n"), ("DOCS/EMPTY.TXT", b""),
                               ("BAD.ELF", b"bad executable"), ("HELLO.ELF", (ROOT / "build/user/ramfs/hello.elf").read_bytes())):
            source.write_bytes(contents)
            images.command("mcopy", "-i", str(disk), str(source), "::/" + name)
        original = disk.read_bytes()
        for ram, iso in ((16, False), (64, True)):
            disk.write_bytes(original)
            with boot(args.qemu, ram, f"{ram}m-{'iso' if iso else 'elf'}", disk=disk, iso=iso) as console:
                commands(console); disk_commands(console)
            assert images.file_bytes(disk, "/DOCS/NEW.TXT") == b"persistent shell bytes"
            assert images.file_bytes(disk, "/DOCS/NOTE.TXT") == b"A\0\xff\x1b\r\n"
            audit = images.audit(disk)
            assert "/DOCS/CHILD" not in audit["directories"]
            end = images.fat.geometry(original)["total"] * 512
            assert disk.read_bytes()[end:] == original[end:]
            print(f"PASS: {ram} MiB userspace shell, both mounts, foreground status/fault/input/Ctrl+C, host FAT bytes", flush=True)
        before = disk.read_bytes()
        with boot(args.qemu, 16, "readonly", disk=disk, options="rum.disk-readonly") as console:
            console.command("cat /disk/DOCS/NEW.TXT", "persistent shell bytes")
            console.command("write /disk/DOCS/NEW.TXT changed", "read-only filesystem.")
        assert disk.read_bytes() == before
        with boot(args.qemu, 64, "missing-disk") as console:
            console.command("ls /disk", "disk is not mounted.")
            console.command("cat /readme.txt")
            for _ in range(2): console.command("exit", "Restarting the userspace shell.")
            console.command("exit", "Kernel recovery shell.")
            console.command("echo recovery works", "recovery works")
        for program, marker in (("/missing.elf", "Cannot load the userspace shell."),
                                ("/readme.txt", "Cannot load the userspace shell."),
                                ("/nonzero.elf", "Userspace shell stopped repeatedly."),
                                ("/fault.elf", "Userspace shell stopped repeatedly.")):
            with boot(args.qemu, 16, "fallback-" + Path(program).stem, options="rum.shell=" + program,
                      marker=marker) as console:
                console.command("echo recovery works", "recovery works")
        with boot(args.qemu, 64, "explicit-recovery", options="rum.recovery", marker="rum_boot_ok") as console:
            console.command("echo selected recovery", "selected recovery")
    print("PASS: persistence after reboot, read-only/missing disk, shell restart/load/fault fallback and explicit recovery", flush=True)


if __name__ == "__main__":
    main()
